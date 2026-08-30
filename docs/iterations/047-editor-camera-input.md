# 047：Viewport 输入焦点与 editor camera

## 目标

把第 046 步的可见画面坐标契约接到真正的 Edit camera 操作，同时修正 UI 状态产生得晚于场景录制的旧时序。完成后：

- 鼠标只能从 Viewport 当前可见画面激活 camera；
- RMB 拖拽环绕、MMB 拖拽平移、滚轮缩放；
- 操作只修改 editor-only camera，不修改 Scene entity；
- 当前 UI 帧产生的 camera snapshot 在当前场景帧生效；
- camera 数学可脱离 ImGui 和 Vulkan 自动测试。

## 原时序问题

旧 `Renderer::on_render()` 的顺序是：

```text
begin frame
  -> resolve + record scene
  -> end scene render pass
  -> callback 内 ImGui NewFrame + panel update + draw
```

Viewport layout、按钮和未来 camera 输入都在场景已经录完之后才更新。把鼠标处理简单塞入 ViewPanel 虽然能工作，但只能等下一帧的
`Application::on_update()` 再生成请求，形成无必要的一帧延迟；Viewport 可见性与新尺寸也同样滞后。

现在 Renderer 提供不包含 ImGui 类型的 overlay 两阶段回调：

```text
begin frame / wait ready slot
  -> overlay prepare
       -> bind current slot viewport texture
       -> ImGui NewFrame + panels
       -> consume camera input
       -> publish ViewportRenderRequest
  -> resolve + record scene with current request
  -> end scene render pass
  -> overlay render: record prepared ImGui draw data
  -> submit + present
```

prepare 只做 CPU 状态和 descriptor 更新；GPU draw 顺序仍是 Scene 后接 ImGui。app 没有 overlay 时两个 callback 为空，不增加额外依赖。
Editor shutdown 主动清空 callback，Renderer 不保留捕获已销毁 ImGuiContext 的闭包。

## 输入边界

ViewPanel 用 `map_viewport_point_to_pixel()` 与 ImGui 当前 item hover 共同判断激活点：

- toolbar/letterbox/pillarbox/裁切区无法开始操作；
- popup 或其他窗口遮挡时 ImGui item 不 hovered，也不会激活；
- RMB/MMB 必须在画面内按下；
- 拖拽一旦从画面内激活，可越过边缘继续，直到对应按键释放，避免边缘处突然停住；
- 滚轮只在指针当前位于可见画面时消费；
- Play 模式清除所有 editor camera drag 状态，不把输入发给 Runtime Scene camera。

ViewPanel 每个 UI frame 只在存在真实 delta/wheel 时产生一个 `EditorCameraInput`，Editor composition root 取走后立即清空。输入不是持久命令，
也不进入 `ViewportRenderRequest`；Request 只继续承载渲染所需的 camera snapshot。

## Camera controller

新增的 `editor_camera_controller` 是 editor 域的 backend-neutral 纯逻辑，不是 ImGui 辅助类：

- orbit 围绕 `target` 旋转 position，保持距离，并限制接近世界上方向的极点；
- pan 依据当前距离、垂直 FOV 和画面逻辑高度换算 world units per pixel，同时平移 position 与 target；
- zoom 使用指数距离变化并限制在 0.05 到 10000；
- up 固定为世界 Y，当前不引入 roll；
- 非有限输入、退化视线、无效 viewport height/FOV 不污染 camera state。

独立文件是合理的，因为相机操作数学会被 2D/3D 模式、快捷键、focus selection 和未来多 Viewport 复用；它不包含 ImGui、Window、Renderer
或 Scene 依赖。ViewPanel 只负责采样，Editor 负责编排，controller 只负责状态变换。

## 自动化验证

- 空输入不修改 camera；
- orbit 保持 target 与观察距离、维持世界 up；
- pan 对 position/target 应用相同平移并保持距离；
- zoom 改变距离并遵守最小距离；
- 无效 delta、viewport height 和 FOV 不产生状态污染；
- 第 046 步全部映射测试继续通过；
- 完整 Debug 构建与 CTest。

ImGui 鼠标采样和 GPU 画面需要手工交互验证，暂不作为自动化门禁。建议在 editor 中检查 RMB/MMB/wheel、从画面内拖出边缘、Play 模式不响应
以及不同 Viewport 尺寸下 pan 速度；核心几何和 camera 数学已由自动化测试覆盖。

## 下一步

让 2D/3D 按钮从 UI bool 变成真实 camera 投影/控制策略。显式 RenderCamera 应表达 Perspective 与 Orthographic 参数，SceneResolver 根据
snapshot 构建 projection；2D 固定观察轴、禁用 orbit，并用正交高度实现缩放，不通过极小 FOV 模拟正交效果。
