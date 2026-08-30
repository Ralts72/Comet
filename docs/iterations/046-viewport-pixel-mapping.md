# 046：Viewport 屏幕点到纹理像素映射

## 目标

阶段 4C 的 camera 控制、对象拾取、gizmo 和选中高亮必须共享同一坐标定义。本步先建立无 ImGui、无 Renderer、无 GPU 依赖的纯映射，
避免每个消费者分别推导 panel 偏移、DPI、留白和裁切。

输入是 `ViewportLayout` 与屏幕逻辑坐标点，输出是当前实际显示纹理上的整数像素；点不属于可见画面时返回空结果。

## 为什么不能只用 contains(image_display_rect)

Fit 模式下 `image_display_rect` 是 panel 内的等比矩形，直接 contains 可以排除 letterbox/pillarbox。但 OneToOne 模式允许完整图像矩形
大于 panel content：ImGui 只显示被窗口裁切后的中间区域，完整 display rect 的其余部分虽然在几何上存在，却不可见也不可点击。

因此布局现在区分：

```text
render_resolution      下一份目标 RenderTarget 的请求尺寸
image_resolution       当前实际显示纹理的尺寸
image_display_rect     完整纹理绘制矩形，可超出 panel
image_visible_rect     display rect 与 panel content 的交集
```

`image_visible_rect` 决定点能否进入交互；`image_display_rect` 决定该点对应完整纹理的哪个位置。两者不能互换：如果用 visible rect
重新归一化，OneToOne 裁切后的左上可见点会错误映射为纹理 `(0, 0)`，而不是被裁切偏移后的内部像素。

## 映射规则

`map_viewport_point_to_pixel()` 执行：

1. 点必须位于 `image_visible_rect`；
2. 当前 `image_resolution` 与完整 display size 必须有效；
3. 用点相对完整 `image_display_rect.min` 的位置计算 `[0, 1)` 归一化坐标；
4. 乘当前纹理分辨率并向下取整；
5. 对浮点边界做最后的 `resolution - 1` 钳制，保证永不产生越界像素。

矩形继续使用左上闭、右下开语义。屏幕点恰好位于右边或下边时返回空，不会把 `u/v = 1` 转成越界坐标。

## 为什么使用 image_resolution

Viewport resize 有 debounce 和事务式 target 创建，请求尺寸与当前纹理在数帧内可能不同。例如工具栏已经选择 1280×720，但仍显示
上一份 1600×900 generation。画面坐标必须映射到 1600×900，否则拾取位置会在切换完成前漂移。

`calculate_viewport_layout()` 因此显式输出 `image_resolution`：有 current texture 时使用它；纹理尚未建立时才回退到本次
`render_resolution`。后续消费者不再自行猜测哪一个尺寸已经发布。

## 排除区域

- toolbar：content origin 之前的点不在 visible rect；
- letterbox/pillarbox：点不在 display rect；
- Fit 的右/下最大边：右下开规则排除；
- OneToOne 裁切区域：点在完整 display rect 但不在 panel 与其交集，返回空；
- 空布局、无纹理分辨率或非有限尺寸：返回空。

## 自动化验证

- 16:9 纹理在 4:3 panel 中映射左上、中心和最后一个像素；
- toolbar、letterbox、右边和下边均返回空；
- 2× HiDPI 下逻辑中心映射到当前物理纹理中心；
- OneToOne 大图的 visible rect 正确裁切，首个可见点映射到被裁切后的内部像素；
- pending 1280×720、current 1600×900 时仍按 current generation 映射；
- 完整 Debug 构建与 CTest。

本步无需手工 GPU/视觉验证；所有行为都是纯几何计算。实际 ImGui 输入消费将在下一步接入，并复用同一函数。

## 下一步

建立 editor camera controller 与 Viewport 输入焦点边界：只有 Edit 模式、Viewport 可见且屏幕点能成功映射时，才把事件交给 camera；
2D/3D 切换也从纯 UI bool 演进为真实投影与操作策略。
