# 031：方向光阴影与真实深度采样链路

## 背景与验收项

030 让 lit_color 受到方向/点/聚光照明，但物体之间不能互相遮挡光线。
本项完成阶段 5 的一个独立验收项：方向光深度图生成 → RenderGraph 同步 → forward 采样 → 可观察的物体投影。
不把这一项等同于级联阴影、所有灯型阴影、PBR 或阶段 5 完成。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Shadow 只在路线图里 | app/editor 共用 Shadow→HDR Scene→tone mapping 三 pass |
| LightComponent 只有照明参数 | 增加 casts_shadow，复用属性编辑、场景保存和克隆 |
| LightingData 为 2064 bytes | 追加阴影矩阵及参数，2144 bytes，由反射测试核对 |
| MaterialRenderer 自己准备灯光 | 帧编排先准备一次灯光/阴影数据，再交给两个 pass |
| FrameSet binding 0/1 为相机/灯光 | 增加 binding 2 阴影 sampler；MaterialSet 不变 |
| Pipeline 写死一个颜色混合附件 | 根据 RenderPass 当前 subpass 的真实颜色附件数创建，包括零颜色深度 pass |
| nearest sampler 仍有隐含线性 mipmap | SamplerDesc 显式记录 mipmap mode，nearest 路径全程 nearest |
| 各 pass 可能重复等待同一上传时间线 | 合并最大 timeline value 和消费阶段并集 |

## 代码级逻辑

### 作者数据、CPU 准备与 GPU ABI

`LightComponent::casts_shadow` 默认 false，注册到现有 ComponentRegistry；Inspector 显示 Cast directional shadow。
它跟随 RenderLight 值快照到 RenderSubmission，不把阴影图、Device 或 Scene 指针放进组件。
点光/聚光保留这个作者值，但本次只有方向光使用；关灯仍由 enabled 在提取阶段过滤。

`ShadowRenderer::prepare(submission)` 先调用 `LightingData::prepare`，保持 32 灯上限、有效性检查和 EntityId 排序。
随后对 resolved Mesh 的 local bounds 应用 model matrix，汇总世界包围盒；不依赖主相机视锥裁剪，因此屏幕外投影者也可影响可见物体。
没有有效相机/几何时关闭阴影，而不是沿用上一帧矩阵。

`LightingData::prepare_shadow(bounds, resolution)` 是纯 CPU 数学：选择已保留灯中首个开启投影且 intensity>0 的方向光，
以场景中心建立 light view，对与世界 up 接近平行的方向改用备用 up，把世界边界拟合到正交投影，并加入边缘余量。
Vulkan Z 范围为 [0,1]；方向光 shader 与采样都使用同一矩阵。无效/退化输入重置 index=-1，不让旧阴影继续生效。
该拟合不是 cascaded shadow，也没有 texel snapping；大范围场景和运动边界的稳定性仍有限。

LightingData 追加 `shadow_view_projection` 和 `shadow_parameters`，后者保存灯索引、归一化深度偏移和 texel 大小。
单灯 cone.z 保存投影请求；原有点光/聚光参数不改变含义。CPU static_assert 与 SPIR-V block 反射核对大小/offset。

### ShadowRenderer 的实际职责

新增 `render/shadow_renderer.h/.cpp`，集中拥有深度 RenderPass、Pipeline 和 D32 MultiTarget，每 slot 一张 1024² 图。
不是仅为缩短 SceneRenderer 成员而打包变量：它有独立的深度录制协议、输入准备和帧资源生命周期。
SceneRenderer 只组合它并把图像接入 RenderGraph，不自己管理阴影 descriptor/push constant 或逐物体 draw。

`shadow_depth.vert` 只读 position，push constant 是 CPU 预乘的 light VP × model（64 bytes）。
fragment 不输出颜色；Pipeline 的颜色附件数由 RenderPass 推导，移除冗余的独立 subpass 数量字段。
深度 pass 清除到 1、写入并 Store；RenderPass 初始/最终都保持 DepthStencilAttachmentOptimal，转换完全由图负责。
未开启阴影时仍清除图，不画几何，后续 descriptor 始终指向有效资源，不复用历史深度内容。

所有 resolved Mesh 暂时都按不透明几何投影，没有 alpha cutout 或逐 Mesh cast/receive 策略。
投影 pass 使用 Mesh 的上传 completion，并保留实际使用的 Mesh/pass/target/pipeline 到 FrameSlot 完成。

### 编排、采样与等待

```text
RenderSubmission
  → ShadowRenderer::prepare → LightingData
  → RenderGraph: shadow（DepthStencilAttachmentWrite）
  → barrier: 深度写 → fragment SampledRead
  → scene: MaterialRenderer（同份 LightingData + 当前 slot depth view）
  → HDR→tone mapping→SDR
```

`MaterialRenderer::render` 消费准备好的 LightingData。每 slot 的 FrameSet binding 2 只在 view 改变时更新，
而 slot 复用前已等待；FrameResources 同时保留 view/sampler。阴影变化不创建 MaterialSet，也不递增材质 revision。
独立使用 MaterialRenderer 时，用内部 1×1 白色中性图保证 sampler descriptor 合法，构造时等待该微小上传；
它不是项目资产或新增程序化纹理 API，正常帧不为阴影执行 CPU wait。

`lighting.glsl` 将世界坐标变换到 light clip，按 shadow pass 的正高度 viewport 转 UV，范围外视为受光。
手动比较 receiver depth 与 depth texture，使用带角度项的偏移及 3×3 PCF；nearest min/mag/mipmap 避免依赖 D32 线性过滤能力。
只有被选中的灯乘阴影可见度，其他灯仍提供原来的照明，unlit 材质不受影响。

`merge_semaphore_wait` 位于已有 graphics/queue，MaterialRenderer、ShadowRenderer 和 SceneRenderer 共用。
相同原生 semaphore 合为一个 wait，timeline 取最大 value，stage 取并集；没有新增队列管理器或跨线程事件。
普通 viewport resize 不重建固定分辨率阴影图；输出仍由现有成对 HDR/SDR target 机制管理。

### 可操作示例

app/editor 的 Key Light 开启投影，Ground 复用已有立方体 Mesh，通过 Transform 缩放为承接面；没有额外生成模型或导入格式。
可以在 Edit 中移动立方体、旋转方向光、切换 Cast directional shadow；独立 app 的立方体动画使用同一阴影链路。
editor 的 Play 使用当前场景 Main Camera，不会自动执行独立 app 的动画代码。
默认关闭新增 Light 的投影，避免旧场景不经选择就增加阴影语义。

## 设计理由与架构价值

- 真正验证 RenderGraph 的跨 pass 深度资源生产/消费，不只是颜色图复制或测试专用图。
- CPU 选择/拟合、GPU owner、作者组件各有明确边界；没有新建 LightManager、EventBus、退休队列或 Shadow ECS System。
- 灯光数据只准备一次，两 pass 使用同一索引和矩阵，避免各自排序选中不同灯。
- 以真实深度 pass 修正通用 Pipeline 附件假设和 Sampler 描述，而非在阴影里直接绕过 Comet 创建原生 Pipeline。
- 共享已有 Mesh、FrameSlot、MultiTarget 和材质绑定；新增类是运行时渲染能力，不是编辑器专用包装。

## 测试结果

- Debug/Release 构建通过，完整 CTest 各 5/5 通过；主集 521 tests，520 通过、同步故障对照按协议跳过。
- 专门 sync-validation 项执行全部 13 个 GPU 图测试；10 个 WSI 故障恢复和两个独立契约测试继续通过。
- 两个新增 CPU 测试覆盖边界全部角点、竖直方向、稳定选择、32 灯上限和无效拟合重置；已有快照及 clone 测试补充投影字段。
- 三个新增 GPU 范围测试：六帧中移动/开关/移除遮挡者的真实像素、wait 合并值/阶段、录制后销毁 ShadowRenderer 的 owner 保留。
- 21 项 Lighting/GPU 测试在同步验证开启下重复 20 轮，共 420 次通过；日志 `/tmp/comet-031-repeat.log`。
- 最终完整结果：`/tmp/comet-031-final-debug.log`、`/tmp/comet-031-final-release.log`；所有 C++ 修改通过 clang-format dry-run，diff --check 通过。
- 首次构建发现 Flags 的 |= 只接受单个枚举，不能接受另一个 Flags；修正为已有二元 | 赋值后重新构建和验证，未运行旧二进制冒充结果。
- 030 的远端 CI `34062462366` 已成功；031 自身远端 CI 以 push 后结果为准。未声称完成手工窗口视觉巡检。

## 限制与后续方向

单张方向光图、固定分辨率和偏移，不包含级联、多灯阴影、静态缓存、透明裁切、PCSS 或大世界稳定拟合。
每帧扫描所有 resolved Mesh 的 bounds，没有空间筛选/dirty 缓存；这与阶段 6 的变换和帧准备优化一起评估。
shadow Shader 暂随构建更新；lighting.glsl 采样逻辑仍在既有材质热编译组中，Frame ABI 不兼容变化继续拒绝热发布。
下一独立验收项为 PBR 材质与高光照明，随后 Bloom、必要诊断和阶段 5 边界回顾；阶段 6 全部约定保持不变。
定期架构回顾仍为 035 或阶段 5 边界（先到者）。
