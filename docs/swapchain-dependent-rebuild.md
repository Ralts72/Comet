# Swapchain dependent 重建改动说明

## 这次改动解决什么

这次改动调整的不是 Swapchain 的创建参数，而是 Swapchain core 与依赖它的渲染资源之间的
释放和重建顺序。

一个 swapchain image 会被 `ImageView` 引用，`ImageView` 又会被 `Framebuffer` 引用。因此它们的概念依赖关系是：

```text
Swapchain core
└── borrowed swapchain images
    └── ImageView
        └── Framebuffer / RenderTarget
```

释放时必须按相反方向进行：先销毁 `Framebuffer` 和 `ImageView`，最后才能销毁它们依赖的
old swapchain。

## 改动前的调用链

改动前，`SceneRenderer::recreate_swapchain()` 的主要顺序是：

```text
Swapchain::recreate()
  -> 创建新 swapchain
  -> 替换 borrowed images
  -> 销毁 old swapchain

SceneRenderer
  -> 重建 runtime SwapchainTarget
  -> 重建 FrameScheduler image state
  -> 通知 ImGuiContext 重建 target
```

问题在于，`Swapchain::recreate()` 销毁 old swapchain 时，旧的 runtime `SwapchainTarget` 和 ImGui
swapchain target 仍然存在。它们持有的 framebuffer/image view 仍依赖 old swapchain image，因此形成了
“先销毁父资源，后销毁子资源”的错误窗口。

`Device::wait_idle()` 只能证明 GPU 不再执行使用这些资源的工作，并不会自动修正 CPU 侧 Vulkan
对象的销毁顺序。

## 改动后的调用链

`SceneRenderer` 现在把一个单一的“重建完成回调”拆成两个生命周期回调：

```cpp
set_swapchain_resource_callbacks(release_resources, rebuild_resources);
```

完整顺序变为：

```text
SceneRenderer::recreate_swapchain()
  -> Device idle
  -> 释放 runtime SwapchainTarget（仅 runtime 直接呈现模式）
  -> release_resources()
       -> 释放 editor ImGui swapchain target
  -> Swapchain::recreate()
       -> 创建新 swapchain
       -> 替换 borrowed images
       -> 销毁 old swapchain
  -> 重建 runtime SwapchainTarget
  -> 重建 FrameScheduler per-image state
  -> rebuild_resources()
       -> 重建 editor ImGui swapchain target
```

这样就明确保证了：

```text
先释放 dependent -> 再替换 core -> 最后重建 dependent
```

## `SceneRenderer` 的具体变化

### 1. 拆分回调边界

原来的接口：

```cpp
set_swapchain_recreate_callback(callback);
```

只能在 core swapchain 已经重建完成后发出通知，无法让 editor 在 old swapchain 销毁前释放它的依赖。

新接口同时接收 release 和 rebuild 回调，由 `SceneRenderer` 控制两者与 core 重建的相对顺序。
Engine 层不需要知道 ImGui 类型，Editor 只在组装点注册自己的资源操作。

### 2. 区分 runtime target 和 editor 离屏 target

runtime app 直接渲染到 swapchain，因此 `SceneRenderer` 自己的 `m_render_target` 依赖 swapchain，重建前需要
`reset()`。

Editor 的 `SceneRenderer` 渲染到独立的 offscreen `MultiTarget`。这组资源不依赖 swapchain，所以
`m_uses_offscreen_target` 为 `true` 时不会释放它。Editor 最终呈现 UI 所使用的 ImGui swapchain target
则通过 release 回调单独释放。

## `ImGuiContext` 的具体变化

原来的 `recreate_swapchain()` 同时负责等待、释放和重建，但它被调用时 old swapchain 已经被销毁。

现在拆成：

- `release_swapchain_resources()`：进入重建状态并释放 ImGui `RenderTarget`。
- `rebuild_swapchain_resources()`：使用当前有效的 swapchain 重建 `RenderTarget`。

`m_is_recreating` 表示这两个阶段之间的状态。重建资源前如果没有先执行 release，会直接报告内部
生命周期错误。

ImGui Vulkan backend 只在 swapchain image count 变化时完整重建。如果 image count 不变，只需重建
swapchain target，不必重建 descriptor pool 和 viewport texture binding。

## 窗口最小化时的恢复路径

Swapchain 重建前已经释放 dependent，但当 framebuffer 尺寸为零时，`Swapchain::recreate()` 会返回
`false` 并延期重建。

这个返回发生在替换 active swapchain 之前，所以旧 core 仍然有效。`SceneRenderer` 会在返回 `false`
前基于旧 core 重建刚才释放的 runtime/ImGui dependent，避免让渲染器停留在“core 存在但 target
被释放”的半完成状态。

## Editor 关闭路径的变化

Swapchain 回调使用 lambda 捕获 `this`，最终会访问 `m_imgui_context`。因此 Editor shutdown 现在会先执行：

```cpp
scene_renderer.set_swapchain_resource_callbacks({}, {});
```

然后再销毁 `ImGuiContext`。这防止 `SceneRenderer` 在 Editor 对象已销毁后仍保留一个悬空回调。

## 测试改动

`test_resource_readiness.cpp` 增加了一个编译期 concept，确认 `SceneRenderer` 暴露可注册 release/rebuild
的生命周期边界。这个测试保护的是 Engine 与 Editor 的组装接口；真实 Vulkan 对象的销毁顺序仍需要
validation layer 和实际 resize/最小化操作来验证。

## 这一步没有解决什么

这次只建立了正确的 parent/dependent 生命周期边界，还不是完整的 Swapchain generation 方案：

- `Swapchain::recreate()` 仍会原地覆盖 active handle 和 images。
- `SceneRenderer` 和 `Swapchain` 当前仍可能重复执行 Device idle。
- RenderPass/Pipeline 是否需要随 format 变化重建，还没有进入 compatibility diff。
- old swapchain 还没有根据 graphics completion 和 present completion 延迟退休。
- core 候选的 prepare/create/commit/retire 事务还需要在后续实现。

因此，这次改动的价值是先修正现有同步重建路径中的 Vulkan 父子对象顺序，并为后续把 core 和
dependent 都改成显式 generation 留出可迁移的边界。

## 验证方式

```bash
cmake --build --preset dev-debug --parallel
ctest --preset dev-debug --output-on-failure
```

手动验证时，可在 Vulkan validation layer 开启的情况下反复执行：

1. 拖动 runtime app 窗口改变大小。
2. 拖动 Editor 窗口改变大小。
3. 最小化后恢复窗口。
4. 关闭 Editor，确认没有回调访问已销毁 `ImGuiContext` 的错误。

