# 048：Viewport 2D/3D 真实投影模式

## 目标

原 2D/3D 按钮只修改 ViewPanel 内一个没有消费者的 bool。本步删除这份重复 UI 状态，让按钮产生一次性投影切换事件，并使 editor camera、
RenderCamera snapshot、SceneResolver projection 与输入 controller 形成完整闭环。

完成后的语义：

- 3D：Perspective，RMB orbit、MMB pan、wheel dolly；
- 2D：Orthographic，固定从 +Z 看向 target，MMB 沿屏幕 XY pan、wheel 改变正交高度，RMB orbit 无效；
- Edit 仍只有一个 editor-only camera，不创建 Scene entity 或第二个 Viewport；
- Play 仍使用 Runtime Scene primary camera，不读取 ViewPanel 的观察模式。

## 通用 RenderCamera 投影契约

`RenderCamera` 新增 `CameraProjection`：

```text
Perspective  -> vertical fov_degrees
Orthographic -> vertical orthographic_height
both         -> view_matrix + near/far clip
```

投影模式属于 render camera snapshot，而不是 ImGui 枚举。`SceneResolver` 继续是唯一构造 projection matrix 的位置：

- Perspective 使用垂直 FOV 与 RenderTarget aspect；
- Orthographic 以 `height / 2` 得到上下边界，以 `height / 2 * aspect` 得到左右边界；
- 两种模式共享非零 render size 与合法 near/far 校验；
- Perspective 单独校验 `(0, 180)` FOV，Orthographic 单独校验正的有限 height。

runtime `CameraComponent` 当前仍按默认 Perspective 提取；通用 snapshot 和 resolver 已不限制来源，后续 Scene Camera 投影属性可在组件描述系统
具备 enum 属性后自然接入。这里不为单一字段临时增加 bool 或绕过 serializer/Inspector 的隐藏配置。

## Editor camera 状态

`EditorCameraState` 保存投影模式和 `orthographic_height`。2D snapshot 不覆盖 3D position，而是根据已有 position-target 距离临时构造：

```text
view_position = target + (0, 0, previous_distance clamped inside near/far)
view_up       = (0, 1, 0)
```

因此切入 2D 始终得到稳定的 XY 正视图；切回 3D 时原 orbit 方向和距离仍在。2D pan 会同时平移 position/target，使返回 3D 后仍以平移后的
区域为中心，不出现 target 与 position 相互漂移。

ViewPanel 不再保存 `m_2d_mode`，也不提供无调用方的 get/set。两个 Selectable 直接读取 `EditorState.camera.projection` 显示选中状态，点击只产生
`CameraProjection` 事件；Editor composition root 在同一 overlay prepare 中先应用投影事件，再应用可能存在的 camera input。

## 2D 控制数学

正交模式不能复用 3D camera position 推导 pan basis。用户可能从任意斜视角切入 2D，而实际 snapshot 已固定正对 XY；如果 controller 仍用旧
3D forward，鼠标水平拖动会在 world Z 上产生分量，与屏幕方向不一致。

本步明确使用：

```text
forward = (0, 0, -1)
right   = (1, 0, 0)
up      = (0, 1, 0)
world_units_per_pixel = orthographic_height / visible logical height
```

正交 wheel zoom 指数缩放 `orthographic_height` 并限制在 `[0.01, 10000]`；它不改变 position-target 距离。orbit delta 在正交模式被忽略。

## 自动化验证

- RenderCamera 默认仍为 Perspective，并具有稳定默认正交高度；
- SceneResolver 对 2:1 RenderTarget 和高度 10 构造 `[-10, 10] × [-5, 5]` 正交投影；
- 非正/非有限正交高度被拒绝，Perspective 原有 FOV 校验保持；
- editor 2D snapshot 从斜向 3D position 生成固定 +Z view，并保留 orthographic height；
- 从斜向位置进入 2D 后，orbit 不改变 offset，pan 不产生 Z 分量，wheel 只缩放正交高度；
- 完整 Debug 构建与 CTest。

真实 UI 选中样式、2D/3D 切换画面和鼠标操作需要手工视觉验证，暂不作为自动化门禁；投影和控制数学已由纯测试覆盖。

## 下一步

在实现拾取前先做一次小型技术审计：确认 Mesh 是否已有 CPU bounds/geometry 可用于 ray cast，以及 GPU ID attachment 在当前传统 RenderPass 下会
引入哪些资源、同步和 readback 所有权。选择能复用后续架构的最小方案，不在 ViewPanel 内直接读 GPU，也不建立只服务一次点击的通用系统。
