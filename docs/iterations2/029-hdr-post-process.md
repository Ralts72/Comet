# 029：HDR 场景与 fullscreen 输出

## 背景与验收项

028 已经能编排资源转换，但生产场景仍直接写入 8 位显示颜色，图也只有一个 scene pass。
本项把阶段 5 的多 pass 能力用于实际 Shader 生产／消费：场景保留 HDR，再统一产生最终 SDR。
app 与 editor 使用同一条中间链路；原有 Viewport、MSAA、swapchain 恢复和资源寿命契约不能失效。

## 前后对比

| 位置 | 之前 | 现在 |
| --- | --- | --- |
| 场景颜色 | 与 surface 相同的格式 | `R16G16B16A16_SFLOAT`；MSAA resolve 也为 HDR |
| app | Scene 直接写 SwapchainTarget | Scene 写 HDR MultiTarget，再 fullscreen 写 SwapchainTarget |
| editor | Scene 写显示用 MultiTarget | Scene 写 HDR MultiTarget，再 fullscreen 写 SDR MultiTarget |
| 图 | 离屏 scene 后导出 SampledRead | app/editor 都有 scene → tone map 两个 pass，图生成它们之间的 barrier |
| 最终图像接口 | 显示目标 | 仍为显示目标，调用方不需要理解 HDR 中间资源 |
| resize | 替换一个目标 | 两套候选全部准备成功，再成对替换 |

```text
SceneRenderer
  HDR scene pass：MaterialRenderer + DebugRenderer + depth/MSAA
       ↓ RenderGraph：ColorAttachmentWrite → SampledRead
  PostProcessRenderer：fullscreen triangle + tone_map.frag
       ↓ 最终 RenderPass：Present 或 ShaderReadOnly
  app SwapchainTarget / editor SDR MultiTarget → ImGui → swapchain
```

## 代码变化和逻辑

### 1. SceneRenderer 分清中间目标与最终目标

`setup_render_pass()` 和 `setup_offscreen_render_pass()` 调用同一个 `setup_targets(size, offscreen)`。
`m_render_pass` / `m_pipeline_manager` 仍只服务场景 Material/Debug pipeline，不把 fullscreen 的不同 pipeline 状态混进去。
`m_scene_target` 是每 frame slot 的 HDR/depth/resolve；`m_render_target` 始终是最终 SDR。
`get_render_target()` 与 `get_offscreen_color_view()` 没有改变外部含义，SceneResolver 继续使用最终目标尺寸。

`render_scene_pass()` 给图绑定当前 HDR attachment，pass 0 调用现有场景绘制，pass 1 调用后处理。
资源上传 wait 仍由 MaterialRenderer 返回，最后与 acquire 一起提交；没有插入 device idle 或第二次 Queue submit。
图的 CPU Plan 在 setup 时编译，resize 不重新编译。

最终 SDR 输出保留在传统 RenderPass 边界：它实际完成 clear/store/final layout，不能同时让图假定它还保持旧 layout。
因此本步图管理 HDR 附件和 pass 间采样；并未声称所有附件转换都已迁入图。
Present 的 external→color dependency 和 acquire 等待继续沿用 028 的同步修复。

### 2. PostProcessRenderer 是实际 GPU pass，不是变量收纳盒

新增 `render/post_process_renderer.h/.cpp`：拥有固定输出 RenderPass、fullscreen Pipeline、采样器和各 slot 的输入 Binding。
不拥有 Scene、Window、资产身份或帧调度器；可由其他渲染调用方使用同一个 HDR→SDR 输出实现。
`get_render_pass()` 用于创建匹配的输出 target，`render()` 负责该 pass 的录制。
它目前不是任意后处理插件框架，也不把固定两个 Shader 的加载伪装成动态资产系统。

每个 Binding 只包含 HDR View、descriptor pool 和 descriptor。相同 View 复用绑定；resize 后新建绑定，不更新正在使用的 set。
录制时保留实际 Binding、Sampler、layout、Pipeline、RenderPass 和输出 target 到 FrameSlot。
SceneRenderer 保留 HDR target/pass；无需把整个 PostProcessRenderer 挂在帧资源上。
该对象由 SceneRenderer 独占，GPU 对象按需要共享；析构 default 直接留在头文件。

`resize_targets()` 先构造 HDR 候选，再构造 SDR 候选；任何离屏分配失败都不发布半套尺寸。
普通 resize 不等待全部帧，旧资源直到使用它们的 fence 完成才释放。
runtime WSI 重建后按新的 extent 成对重建目标；WSI 暂停、旧 generation 退休和间隔重试继续生效。

### 3. Shader、色彩与坐标

`fullscreen.vert` 用 vertex index 生成覆盖屏幕的一个三角形，无顶点 buffer。
正高度 viewport 保留 framebuffer 和纹理的上下方向；场景仍使用原来的负高度 viewport。

`tone_map.frag` 对线性 HDR 使用 `1 - exp(-max(color, 0) * exposure)`，输出 alpha=1。
当前场景调用固定 exposure=1；pass API 支持有限非负曝光，拒绝非有限值和负数。
这是一条明确的基础映射，不是 ACES、自动曝光或最终艺术参数系统。
场景颜色 1 不再意味着最终输出 1；高亮值 4 等仍能参与映射，不会先被 8 位场景附件截断为 1。

sRGB attachment 由硬件编码；UNORM attachment 由 Shader 使用分段 sRGB 函数编码，避免重复 gamma。
editor 的 SDR 纹理采用与窗口一致的格式：sRGB 纹理采样解码后由 sRGB 窗口重新编码，UNORM 则原样传递已编码值。
两份新生产 Shader 加入显式 CMake 列表；没有删除或格式化预留学习 Shader。

## 设计理由与架构价值

- 光照、阴影和后续 Bloom 可以在显示转换前处理高亮，不再受最终交换链格式约束。
- app/editor 的差异留在输出目标，不维护两套场景绘制逻辑，也不让 Viewport 知道中间 HDR。
- RenderGraph 获得真实附件写入→Shader 采样依赖，而不只是 transfer 测试。
- 后处理的 Pipeline、descriptor、RenderPass 由实际 pass owner 管理；没有全局 EventBus、新基类或通用生命周期队列。
- 成对发布解决多目标 resize 一致性，FrameSlot 继续承担实际在途资源保留。

## 测试结果

- Debug 与 Release 完整构建成功；各 5 个 CTest 项通过。
- 主单元集 507 tests：普通运行的同步故障对照按约跳过，专用 sync-validation 项运行全部 8 个 GPU 图测试。
- 新增真实像素测试：RGBA16F 输入的上下两块不同颜色，包含大于 1 的高亮和接近黑色的线性分段；
  sRGB/UNORM 两类输出，曝光 1 / 0.25 / 0，逐像素比较 CPU 期望值（允许 2 个量化等级误差），检测截断、重复编码和垂直翻转。
- 新增生产 SceneRenderer 测试：MSAA 1/4、HDR clear、尺寸变化、每 slot 输出与旧 View 的在途保留/完成释放。
- 现有 runtime/editor WSI 故障恢复 10 tests、材质/Debug/Shader 热更新与原有全部测试通过。
- GPU 图测试在同步验证开启下重复 20 次，共 160 次；日志 `/tmp/comet-029-repeat.log`。
- 相关 C++ clang-format 检查与 `git diff --check` 通过。
- 上一项 028 的 Linux CI `34060940872` 已成功；本项远端 CI 在 push 后单独确认，不能沿用上一项结果。

## 限制与后续方向

- 这是 SDR 显示输出链路，不支持 HDR10/scRGB 窗口；仅接 RGBA/BGRA 8 位 sRGB/UNORM 输出，其他格式明确拒绝。
- HDR RGBA16F 与所选 MSAA 需要设备支持，未引入自动降级到其他 HDR 格式的策略。
- Debug 世界线也在场景 HDR pass 内，会经过相同映射；Gizmo/ImGui 是后续 UI，不在场景 tone mapping 内。
- Bloom、曝光控件/自动曝光、后处理 Shader 热更新、动态后处理节点均未完成，不提前勾选。
- 已验证 GPU 像素及生产路径，未把手工编辑器视觉巡检宣称为自动化覆盖。
- WSI 的 surface/device 丢失、运行中改变窗口色彩编码/不兼容格式仍需完整 dependent 重建；本步不扩大恢复声明。
- 下一项：LightComponent 与 forward 光照，结合 030 定期架构回顾检查目录、职责、依赖、冗余和生命周期。
