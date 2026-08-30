# 039 Viewport RenderTarget Generation

## 目标

把离屏 Viewport resize 从“等待整个 Device 空闲后原地修改 target”改为完整的 generation 事务：

```text
prepare candidate -> create all GPU owners -> commit active owner -> retire old owner
```

本步只处理 editor 场景离屏目标，不把 runtime swapchain、ImGui swapchain target 和 format-dependent pipeline 一起重写。

## 改造前

`SceneRenderer::apply_pending_viewport_resize()` 在 debounce 满足后调用 `RenderContext::wait_idle()`，再修改同一个
`MultiTarget` 的 extent。真正的 image/image view/framebuffer 重建推迟到下一次 `begin_render_target()`：

- 每次稳定 resize 都阻塞所有 queue 和不相关 GPU 工作；
- active 对象先进入 dirty 状态，资源创建和引用切换不是一个明确事务；
- Image 与 ImageView 已有可恢复工厂，但 FrameBuffer 和 MultiTarget 只能强失败，无法做到候选失败时保留旧目标；
- ImGui 检测到任意 image view 变化后一次性释放全部 frame-slot descriptor，可能释放仍被其他 in-flight slot 使用的 set。

## Generation 表达

没有新增一个只为 ImGui 或 Viewport 服务的 `RenderTargetGeneration` 类。一个完全构造、之后不再原地 resize 的
`MultiTarget` 对象本身就是 generation；`SceneRenderer` 只把 active owner 从 `unique_ptr` 改为 `shared_ptr`，以接入已有的
通用 `GpuRetirementQueue`。

这个选择保留了清晰边界：

- `MultiTarget` 仍是 engine render 概念，可被未来其他多帧离屏 pass 复用；
- generation 是对象替换和生命周期规则，不需要再包一层重复转发尺寸、framebuffer 和 image view 的类型；
- retirement queue 继续只认识 `shared_ptr<void> + GpuCompletionPoint`，不增加 Viewport 特例。

## Prepare 与 Commit

`FrameBuffer` 现在与 Image/ImageView 一样提供两级静态工厂：

- `create()` 用于不可恢复的初始化路径；
- `try_create()` 返回 `GpuResourceResult`，只在原生 framebuffer 成功后发布 wrapper。

`RenderTarget::try_create_multi_target()` 对每个 frame slot 依次使用预算受限的 Image、ImageView、FrameBuffer 可恢复工厂，所有
附件先落在候选 `MultiTarget` 内。任意一步失败都会析构尚未发布的局部 owner，并返回具体 Vulkan result；active target 没有被修改。
全部成功后 `SceneRenderer` 设置 clear value，再一次性交换 active shared owner。

这也让 `FrameBuffer → ImageView → Image` 的父子所有权链在失败路径成立：不会产生带空 Vulkan handle 的公开对象，也不会先销毁
旧 target 再尝试分配新 target。

## Retire 与 ImGui Frame Slot

每帧开始录制时，`SceneRenderer` 把当时的 active RenderTarget owner 加入本帧实际资源集合。提交后，该集合连同 Mesh/Texture
一起绑定 graphics queue 返回的 completion point。旧 generation 可能同时出现在多个 pending batch 中，只有所有引用它的提交完成后
才会真正析构；不使用固定 N 帧延迟。

ImGui descriptor 有独立的最后使用边界。旧 target 的 C++ owner 安全并不代表可以立即 `RemoveTexture()`，因为 descriptor set 本身
可能仍在先前 UI submission 中。现在 Editor 只更新当前 frame slot 的 `TextureBinding`：`begin_frame()` 已先等待该 slot fence，所以替换
这个 slot 的 descriptor 是安全的；其他 slot 保留旧绑定，并在各自下一次成为 ready slot 时替换。绑定继续共享持有 ImageView/Sampler，
因此其底层 image 生命周期也覆盖 descriptor 的实际存活期。

## 错误与范围

- 非零 extent、非空 attachment 等编程前置条件仍是 `LOG_FATAL`；
- 显存预算、Image/ImageView/Framebuffer 原生创建失败是可恢复结果，记录错误并继续使用旧 generation；
- 失败后保留请求，经过现有 debounce 可再次尝试；下一步增加最大目标尺寸，减少异常 DPI/窗口造成的持续超预算请求；
- swapchain 与 ImGui swapchain-dependent 重建仍保留当前 idle 基线，后续按 core/dependent generation 单独设计。

## 验证

- `ResourceValidationTest` 固定 Image、ImageView、FrameBuffer 和 MultiTarget 的 recoverable factory 类型契约，并确认 FrameBuffer
  不能绕过工厂直接构造；
- `cmake --build --preset dev-debug --parallel`；
- `ctest --preset dev-debug --output-on-failure`。

手工拖拽 Viewport、观察 validation layer 与实际显存压力下的失败恢复需要可用 Vulkan 窗口环境，本步不把它伪装成无 GPU 单元测试。

## 下一步

在纯 `ViewportLayout` 层加入可测试的最大物理分辨率约束：结合设备 capability 上限和编辑器策略等比缩小目标，避免超大窗口、
异常 framebuffer scale 或固定预设触发无上限 generation 分配。之后再决定进入 Viewport 交互闭环，还是单独启动更大的 swapchain
generation 改造。
