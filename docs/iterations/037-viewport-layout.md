# 037 Viewport 布局与物理分辨率

## 目标

解决旧 ViewPanel 把三个不同概念混成一个 `m_viewport_size` 的问题：

```text
panel content size       ImGui 逻辑坐标，用于 UI 布局
render resolution        GPU RenderTarget 物理像素，用于 resize/projection
image display rect       当前纹理在屏幕上的实际逻辑坐标矩形
```

旧实现直接把 `GetContentRegionAvail()` 截断成整数并请求 RenderTarget，因此 Retina/HiDPI 下 800×600 逻辑单位仍只渲染 800×600 像素；
同时等比缩放的位置只存在于 `render_view_content()` 的局部变量，后续输入映射、拾取和 gizmo 无法复用。

## 为什么增加 ViewportLayout 模块

`viewport_layout.h/.cpp` 不是 ImGui 包装，也不是只为当前按钮服务的类。它是无 GPU、无窗口、无 ImGui 依赖的纯计算模块，输入：

- content origin 与 logical content size；
- 当前 platform viewport 的 framebuffer scale；
- 当前实际 RenderTarget/纹理分辨率。

输出：

- 经过清理的 panel content size；
- 目标 physical render resolution；
- 屏幕逻辑坐标系中的 image display rect。

同一结果将由当前 ViewPanel/Renderer、后续鼠标到纹理像素映射、letterbox 排除、Fit/1x 显示和拾取共同消费，因此值得成为独立模块。
它按职责命名为 `viewport_layout`，没有再增加 `_data` 目录或 facade。

## 计算规则

Free/Edit 当前策略下：

```text
render_resolution.x = round(content_size.x * framebuffer_scale.x)
render_resolution.y = round(content_size.y * framebuffer_scale.y)
```

只要 logical extent 为正，物理 extent 至少为 1；非有限/负 content 归零，无效或非正 framebuffer scale 回退到 1。计算用 double
并钳制到 uint32 范围，避免异常 UI 值造成整数溢出。

显示矩形优先使用当前实际纹理分辨率的宽高比，而不是刚请求、尚未通过 resize debounce 应用的目标尺寸：

```text
current texture 1600×900
panel content    800×600
display rect     800×450，垂直居中，上下各 75 logical units
```

当纹理尚不存在时，使用新算出的 render resolution 作为显示宽高比。非等比 framebuffer scale 也按物理像素宽高比计算显示区域，不假设
x/y scale 永远相等。

`ViewportRect::contains()` 采用左上闭、右下开区间，适合后续像素坐标映射，不会把 max 边界映射到越界像素。

## 接入边界

ViewPanel：

- 每帧从 `GetContentRegionAvail()` 和 `GetCursorScreenPos()` 获取逻辑输入；
- 从 `GetWindowViewport()->FramebufferScale` 获取当前平台窗口比例，而不是只读 main viewport 全局值；
- 保存完整 ViewportLayout；
- 在 image display rect 内绘制纹理或占位 item；
- 对外提供 physical render resolution 和完整 layout。

Editor composition root 只把 `get_render_resolution()` 写入 ViewportRenderRequest。Renderer/SceneRenderer 不包含 ImGui 类型，不知道 DPI
或 panel 坐标；SceneResolver 仍用 resize 真正应用后的 RenderTarget size 计算 projection aspect。

## 测试

- 800×600 logical、2× scale 产生 1600×1200 physical；
- 16:9 当前纹理在 4:3 panel 中产生正确 letterbox 与绝对屏幕 rect；
- 非等比 framebuffer scale 的物理 aspect 和显示区域正确；
- 无效/负/NaN 输入不会创建像素，无效 scale 使用 1× 安全回退；
- `contains()` 排除 letterbox；
- 完整 Debug 构建与 CTest。

## 下一步

在布局计算上增加显式 Play 显示策略：Free、固定宽高比/固定像素分辨率，以及 Fit/1x。固定模式下 panel resize 只改变
image display rect，不改变 render resolution；Edit 默认继续使用 panel physical resolution。策略保持纯测试，UI 只修改枚举/预设值。
