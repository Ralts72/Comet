# 045：阶段 4B 完成审计

## 目标

阶段 4B 已先后建立 Viewport layout、离屏 target generation、swapchain core/dependent generation 和局部完成等待。本次不再增加
新的生命周期包装，而是检查这些机制落地后是否仍残留旧模型，确保进入交互阶段前只有一套可解释的资源替换规则。

审计集中回答四个问题：

1. `RenderTarget` 是否仍允许绕过 generation 边界原地改写 GPU 结构；
2. 是否存在已经被 `MultiTarget` 覆盖的重复 target 类型；
3. RenderPass 的初始格式是否来自交换链实际结果；
4. runtime format 变化的剩余缺口应现在补临时机制，还是进入后续渲染管线设计。

## 审计发现

### 1. 旧 dirty/recreate 模型仍暴露在公共 API

改造前 `RenderTarget` 提供：

```text
resize / set_frame_count
  -> 修改 active 对象的 extent/frame count
  -> m_needs_recreate = true
  -> 下一次 begin_render_target() 隐式 recreate GPU 资源
```

这套模型来自早期“一个对象长期存在、内部 framebuffer 原地重建”的实现。现在 Viewport resize 已改为在 active owner 之外创建完整
`MultiTarget` 候选，成功后才交换 shared owner；上述 API 已没有生产调用方，却仍允许未来代码重新绕过事务边界。更重要的是，
`begin_render_target()` 作为录制路径不应隐藏 image/view/framebuffer 分配和失败点。

本步删除 `resize()`、`set_frame_count()`、`is_dirty()`、公开 `recreate()` 和 `m_needs_recreate`，并把 extent/frame count 改为
构造后只读字段。现在结构性变化只有一条路径：创建新对象，然后在编排层发布。

### 2. OffscreenTarget 与 MultiTarget 重复

`OffscreenTarget` 是固定 frame count 为 1 的离屏实现，单独维护一套 color/depth image、view 和 framebuffer 创建代码；项目中没有调用方。
`MultiTarget` 已覆盖同一能力，并额外提供可恢复、预算受限、全部资源成功后才发布的创建事务。

本步删除 `OffscreenTarget` 和 `create_offscreen_target()`。单帧离屏目标若未来出现，直接使用 `MultiTarget(frame_count = 1)`，不会再形成
两套资源所有权与错误处理逻辑。

`MultiTarget` 构造函数同时改为私有。调用方必须使用 `create_multi_target()` 或 `try_create_multi_target()`，因此不能观察到尚未执行
`try_initialize()` 的半初始化对象。

### 3. 配置请求不是设备选择后的事实

改造前 `SceneRenderer::m_surface_format` 直接取 `Config::Vulkan::surface_format`。该值只是首选请求；`select_swapchain()` 可以根据 surface
能力回退到另一种格式。此时初始 RenderPass 可能使用请求格式，而 swapchain image 使用实际格式。

现在 SceneRenderer 从 active `SwapchainGeneration::config.surface_format.format` 获取实际选择结果，再转换为引擎 `Format`。由此形成：

```text
Config preferred format
  -> swapchain capability selection
  -> active SwapchainGeneration actual format
  -> SceneRenderer RenderPass/Pipeline source of truth
```

compatibility 测试还补充了 color-space-only 变化，明确 `vk::SurfaceFormatKHR` 的 format 和 color space 共同构成 format compatibility。

### 4. Pipeline generation 不在旧管理器上打补丁

当前 runtime swapchain format 变化会明确终止，因为 RenderPass/Pipeline 尚不能作为完整候选事务创建并切换。这个限制不会被静默忽略，
也不会在不兼容状态下继续渲染。

审计决定把 runtime format/sample-count-dependent Pipeline generation 归入阶段 5，与结构化 `PipelineKey`、RenderGraph pass 生命周期和
descriptor compatibility 一起设计。现在单独增加一个只服务 swapchain 回调的 Pipeline 包装，会在下一阶段马上被第二套生命周期取代，
不具备合理复用价值。

## 前后对比

| 维度 | 改造前 | 改造后 |
| --- | --- | --- |
| target 尺寸变化 | 改 active 对象，下一次 begin 隐式重建 | active 外创建完整候选，再交换 owner |
| extent/frame count | 可写并带 dirty flag | 构造后只读 |
| MultiTarget 创建 | 可直接构造后再 recreate | 只能经强失败/可恢复工厂发布完整对象 |
| 单帧离屏目标 | 独立 OffscreenTarget 重复实现 | 统一为 frame count 1 的 MultiTarget |
| 初始 surface format | 配置首选值 | active swapchain 实际值 |
| runtime format 变化 | 明确拒绝，归属不清 | 明确拒绝，并归入阶段 5 Pipeline generation |

## 自动化验证

- 编译期契约确认 `RenderTarget` 不再支持 resize、frame-count mutation 或 recreate；
- 编译期契约确认 `MultiTarget` 不能绕过工厂直接构造；
- swapchain 纯逻辑测试确认仅 color space 变化也会触发 format compatibility；
- 完整 Debug 构建覆盖 engine、app、editor 和 tests；
- 完整 CTest 回归覆盖资源、交换链、Viewport 和资产链路。

手工 GPU/视觉验证不作为本步自动化门禁。format fallback 与 surface color-space 切换依赖具体 WSI/显示环境，继续由 Vulkan validation 或
后续多平台运行回归验证。

## 下一步

进入阶段 4C，先实现纯 screen-point → RenderTarget-pixel 映射。映射必须以 `image_display_rect` 为唯一可交互区域，排除工具栏、
letterbox/pillarbox 和 1x 裁切不可见区；之后 editor camera 输入与对象拾取都复用同一坐标契约。
