# 017：多布局 GPU 材质与 Frame/Material 分层

## 背景与验收项

承接 016 的属性准备边界，完成阶段 5 的 Frame/Material/Object 基础分层和两种实际 GPU 布局验收。
此前虽然布局驱动了纹理映射，但仍只有一条 cube pipeline，相机 UBO 和纹理共处一个按 slot 更新的 descriptor set。
这次必须让两种不同材质布局真正同场景绘制，并证明参数更新不会覆盖上一帧使用的数据。

## 前后对比

| 维度 | 之前 | 现在 |
| --- | --- | --- |
| SceneRenderer | pass/target/WSI，同时处理纹理 descriptor 和每个 Mesh draw | pass/target/WSI，调用 MaterialRenderer 和 DebugRenderer |
| 相机 | 每种材质的 descriptor 都带同一 slot 的相机 buffer | FrameSet = set 0，仅按 slot 创建 |
| 材质 | 每资产 N 个可改写 set | MaterialSet = set 1，每个成功 revision 一个不可变版本，跨 slot 共用 |
| 对象 | push constant model | 保留 push constant model，不额外分配对象 UBO |
| 参数 | Texture 引用 | Texture + scalar + vector4，按手工 std140 布局打包 |
| 绘制 | 一个 pipeline、逐输入顺序绑定 | 按 pipeline/material 排序，同组减少重复绑定 |
| 旧版本 | 当前 slot 替换纹理引用 | 整个 GPU 材质版本由使用它的 FrameSlot 保留 |

## 代码链路

### 1. 文件到 Runtime Material

`MaterialData` 保留 `texture_properties`，增加 scalar/vector property map。
`MaterialSerializer` 的 v1 格式增加 `type: scalar/vector` 与 `value`，旧纹理格式不变。

```yaml
version: 1
template: unlit_color
properties:
  color: {type: vector, value: [0.2, 0.7, 1.0, 1.0]}
  intensity: {type: scalar, value: 1.0}
```

跨类型同名、空名称、非有限浮点数、非四分量向量都拒绝保存/读取；源文件格式不保存 descriptor binding。
`AssetManager::create_runtime_material` 解析纹理后设置数值属性；依赖图仍只收集 Texture Handle。
`Material` setter 仅在值变化时推进 revision，相同值写回不生成新 GPU 版本。

### 2. 手工布局到参数字节

`MaterialLayout` 增加 ScalarProperty/VectorProperty 的名称、offset、默认值和参数块大小。
构造时检查 std140 对齐、范围、名称唯一、字段不重叠和默认值有限；参数块使用 binding 0。
`MaterialRuntimeCache` 创建 `PreparedMaterial::parameters`，缺省字段取布局默认值，padding 清零。

当前生产布局：

| template | MaterialSet | 参数 |
| --- | --- | --- |
| cube_texture | binding 0 参数块，1/2 两张纹理 | tint: vec4 @ 0，blend: float @ 16；块大小 32 |
| unlit_color | binding 0 参数块，无纹理 | color: vec4 @ 0，intensity: float @ 16；块大小 32 |

原 cube GLSL 保留供学习，不再作为生产构建输入；新增 material_mesh.vert / material_textured.frag / material_solid.frag。
原 demo 的白色 tint 和 0.5 blend 保持原默认外观；另提供 `assets/materials/solid.mat` 及稳定 .meta 身份。

### 3. MaterialRenderer 的真实职责

`render/material_renderer.h/.cpp` 是物体材质绘制入口，与 DebugRenderer 并列，不是把 SceneRenderer 字段机械打包。
它选择布局/Shader、建立 pipeline、准备版本资源、排序绘制队列、汇总 Mesh/Texture 上传等待。

```text
SceneRenderer::render_scene_pass
  ├── target begin + viewport/scissor
  ├── MaterialRenderer::render
  │   ├── FrameResources[slot]：写当前相机
  │   ├── MaterialRuntimeCache：CPU 属性/参数快照
  │   ├── prepare_material：GPU 不可变版本
  │   ├── 按 pipeline / AssetHandle 排序
  │   └── bind FrameSet + MaterialSet → push model → Mesh::draw
  ├── DebugRenderer::render
  └── target end → 统一 submit/present
```

CPU 缓存解决属性解析和版本判断，GPU 缓存解决设备对象分配与录制寿命；没有在 AssetManager 添加 Device 依赖。
FrameResources、PipelineState、MaterialResources 都是 MaterialRenderer 私有嵌套类型，按实际寿命组织，不对其他模块暴露内部容器。
新增统计提供本次绘制数、pipeline/material 绑定数、创建版本数、缓存版本数和 FrameSet 数量。

### 4. 不可变版本与在途寿命

`prepare_material` 先创建候选参数 buffer、descriptor pool/set，写完所有绑定，才替换当前缓存。
已发布版本不再 write descriptor，也不再 write 参数 buffer；相机独立更新不会使材质版本失效。
GPU 材质版本持有 PreparedMaterial、Sampler、参数 buffer、pool、PipelineState；PipelineState 保留两种 set layout 和 Pipeline。
每个使用版本的帧调用 `retain_current_frame_resource(material)`；替换缓存不会提前释放在途 owner。
FrameSet 的 pool/layout/buffer 也以同样方式保活，不依赖 MaterialRenderer 容器恰好活到 GPU 完成。

GPU 候选创建发生标准异常/可恢复 buffer 失败时保留旧 GPU 版本，没有旧版本则跳过；同一候选延迟 60 frame serial 再试。
新 revision 不受旧候选重试延迟限制。这里没有模拟设备内存耗尽，不能把正常路径测试宣称为真实 OOM 验收。
未知布局和未使用材质的诊断/缓存会在后续材质渲染周期回收；RenderPass/target 代际规则仍由上层掌握。

## 架构价值与接口修正

- SceneRenderer 不再负责 shader 属性、descriptor 更新和物体 draw，新增布局不会继续使它膨胀。
- 相机、材质、对象三种变化频率有各自更新路径；同一 MaterialSet 跨 slot 共享，不再复制 N 份。
- 旧版本有明确 owner 链，后续 Shader/layout 热切换可以沿用代际保活，而不是修改正在执行的资源。
- FrameScheduler、CommandBuffer、Queue、PipelineManager、Fence、Semaphore 的公开跨共享库接口补齐 COMET_API。
  这些接口本来已在公开头文件中，实际离屏渲染客户端首次暴露隐藏符号链接失败；没有复制实现进测试目标绕过边界。

## 测试结果

- Debug/Release 全量构建及 CTest：各 432 tests 通过。
- 材质相关 19 个测试重复 10 轮，190 次通过，未发现 Vulkan validation 错误。
- 新增 5 个测试：typed 参数往返/依赖；无效数值与重名；std140 packing/default/revision；非法布局；真实像素读回。
- 扩展原跨 slot 测试：8 帧、3 个物体、两种布局；同材质两物体只绑定一次；无变化不创建版本；
  修改纹理、标量、替换 Material 和切换 template 分别使正确版本失效。
- 离屏读回使用两个独立 slot：先提交旧参数帧，再替换 Texture/参数并提交新帧，统一等待后分别读取两张结果。
  旧 Texture 的 weak_ptr 在回收前仍有效，完成回收后失效；新旧帧像素均符合各自参数。
  纹理区域 RGB 从约 (191,0,32) 到 (64,0,96)，纯色区域从约 (26,102,51) 到 (13,51,26)，允许 UNORM 舍入误差 2。
- 图形 fixture 开启 Vulkan validation，断言无 VUID/Validation Error；没有把 API 调用成功等同于颜色正确。
- 原帧内编辑顺序测试纳入本帧新建的材质参数 buffer 数量，仍验证拾取回调的线框当帧分配/绘制。
- 上一步 016 Linux CI 34053125063 成功；本步 CI 待推送后观察。未进行人工桌面 UI 验收。

## 限制、验证方法与下一步

在编辑器中将 `solid.mat` 拖到 MeshRenderer 的 Material 引用，可以切换为纯色；再换回 demo.mat 检查纹理。
数值参数当前由 `.mat` 配置并通过现有刷新/热重载发布，Inspector 暂未新增数值控件，下一项按布局统一生成。
Shader reflection、结构化 PipelineKey、热编译/切换和透明排序尚未完成；材质版本按 revision 整块重建，不是逐字段上传。
当前渲染统计描述最近一次 MaterialRenderer 调用；无有效相机时上层不调用它，诊断界面后续需要明确空帧统计语义。
渲染循环仍在 owner 线程，不因为新增排序队列而引入 RenderThread 或全局 EventBus。
