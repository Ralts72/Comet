# 038 Play Viewport 分辨率与显示策略

## 目标

在第 037 步的 ViewportLayout 上明确分离两组策略：

```text
Resolution Policy：GPU 应该渲染多少物理像素
Display Mode：这些像素如何显示在 ImGui 逻辑内容区
```

Play 工具栏提供 Free、16:9、1280×720、1920×1080，以及 Fit/1x。Edit 不读取这些 Play 设置，继续按 panel 的物理像素大小渲染并
使用 Fit。

## 分辨率策略

`ViewportResolutionPolicy` 是 ViewportLayout 的纯输入：

- `Free`：`logical content × framebuffer scale`，随面板变化；
- `Aspect16By9`：在 panel 可用物理像素内选择最大的 16:9 分辨率，随面板变化，但 Camera aspect 始终 16:9；
- `Fixed`：使用明确的非零像素尺寸，当前 UI 提供 1280×720 和 1920×1080。

Fixed 不依赖 content size 或 DPI，因此 panel resize 只重算显示矩形，不请求新的 RenderTarget。无效的 `(0, 0)` Fixed 配置返回零目标，
不会静默退化为 Free；UI 只产生有效预设，未来自定义输入可在提交前显示校验错误。

16:9 模式先计算 panel 的可用物理像素，再按宽或高约束取最大内接分辨率。例如 1000×800 logical、2× scale 得到 2000×1600
可用像素，目标为 2000×1125。

## 显示策略

- `Fit`：把当前实际纹理等比内接到 panel content，产生 letterbox/pillarbox；
- `OneToOne`：一个纹理物理像素对应一个屏幕物理像素，因此 logical display size 为 `texture resolution / framebuffer scale`。

布局仍优先使用当前纹理分辨率，而不是正在 debounce 的目标尺寸，所以切换预设期间显示矩形和实际采样图像一致。1x 图像大于 panel
时矩形会超出 content 并由 ImGui window clip；这是像素精确预览而不是自动回退 Fit。后续若需要观察边缘，可基于同一 rect 增加平移或滚动。

## UI 与所有权

分辨率/显示选择属于单个 ViewPanel 的观察状态，当前由 ViewPanel 保存：

- 进入 Play 时显示两个 Combo；
- 切回 Edit 时设置保留但不参与布局；
- 再进入 Play 时恢复之前的选择；
- UI 只改变 policy 值，`calculate_viewport_layout()` 统一产生 render resolution/display rect；
- Editor 继续只把 layout 的 render resolution 放入 ViewportRenderRequest。

因此 Combo 不调用 SceneRenderer、不修改 Scene Camera，也不复制 resize 逻辑。未来项目级 Game View 预设若需要持久化，再把 policy 迁入
Editor state/settings；当前不提前写入 ProjectSettings。

## 测试

- 16:9 在非 16:9 panel/HiDPI 下得到最大内接物理分辨率；
- Fixed 在不同 panel size 和 framebuffer scale 下保持相同目标；
- 1x 在 2× framebuffer scale 下以一半 logical size 显示相同物理像素；
- 原有 Free、Fit、无效输入和 display rect contains 测试保持；
- Editor Debug 构建与完整 CTest。

## 下一步

当前 resize debounce 最终仍通过 `Device::wait_idle()` 重建离屏目标。下一步把 Viewport RenderTarget 演进为 generation：新资源先完整创建，
成功后提交切换，旧 generation 绑定最后使用它的 GPU completion 后延迟销毁；创建失败时不覆盖当前目标。本步先不同时重写 swapchain。
