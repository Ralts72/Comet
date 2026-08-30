# 035 Viewport Render Request 与 Editor Camera

## 目标

结束“Edit 与 Play 都隐式使用当前 Scene 主相机”的临时状态，建立单 Viewport 的模式化渲染请求：

- Edit 使用 editor-only camera；
- Play 使用 Runtime Scene primary camera；
- 请求显式携带 Viewport 可见性、目标尺寸、Camera 来源和输入许可；
- SceneRenderer 不读取 EditorMode、ImGui focus 或 EditorState。

本步不实现相机鼠标操控、正交 2D、固定分辨率、多 Viewport 或游戏 Input System，只先建立这些能力必须依赖的稳定边界。

## 数据流

```text
ViewPanel（上一帧 UI 测量）
  -> visible / content size / image focus

EditorState
  -> Edit: EditorCameraState snapshot
  -> Play: Runtime Scene primary

make_viewport_render_request
  -> ViewportRenderRequest（纯值）

Renderer
  -> visible + requested size: 驱动已有 resize debounce
  -> 用当前实际 RenderTarget size 覆盖请求尺寸后交给 SceneResolver

SceneResolver
  -> Explicit: 只用 editor camera，不隐式回退
  -> ScenePrimary: 从 RenderScene 选择 EntityId 最小的 primary camera
  -> RenderSubmission

SceneRenderer
  -> 只消费最终 submission
```

投影使用“当前实际 RenderTarget size”，而不是尚未稳定并应用的目标尺寸。拖动面板时 resize 仍有 debounce；如果直接用 requested size，
在新 RenderTarget 建成前 projection aspect 会先变化一到数帧，导致画面与实际附件尺寸不一致。

## 为什么 Request 放在 Renderer 边界

`ViewportRenderRequest` 是渲染编排契约，不是 GPU 对象，也不是 Scene 组件：

- Scene 只保存可序列化的 runtime Camera entity；
- EditorState 持有 editor camera 的位置、观察点和镜头参数；
- Renderer 决定本帧采用哪种 Camera 来源，并把目标尺寸交给 SceneRenderer 的 resize 原语；
- SceneResolver 负责验证并生成 ViewProjectMatrix；
- SceneRenderer 不知道 Edit/Play，也不拥有 editor camera。

请求与 RenderScene 的 Camera/RenderItem DTO 放在同一头文件，避免仅为几个紧密相关的小值类型再增加 `_data` 文件。它没有虚函数、资源
所有权或服务职责，不是为了 ImGui 创建的新 Engine 类。

## Editor Camera 所有权

`EditorCameraState` 属于 EditorState，默认从 `(0, 0, 3)` 看向原点，并用 `look_at` 产生不可变 RenderCamera 快照。快照：

- 没有 Scene EntityId；
- 不进入 EnTT registry；
- 不参与 `.scene` 序列化和 Play clone；
- Stop 后自然恢复，因为 Edit 请求重新读取同一个 EditorState；
- 后续 orbit/pan/zoom 只修改 EditorState，不会污染场景 Camera Transform。

这不是 Runtime Camera 的第二份组件模型；它只是编辑器观察视图所需的状态，最终仍复用相同 RenderCamera 校验和投影链。

## 相机选择契约

SceneResolver 保留原有 `resolve(RenderScene, size)` 兼容入口，并新增消费完整请求的入口：

- `ScenePrimary` 延续原规则：选择 EntityId 最小的 primary camera，多主相机仅记录一次诊断；
- `Explicit` 必须携带 explicit_camera；缺失时只清屏并诊断，不回退 Scene primary；
- 两种来源复用 render size、FOV、near/far 校验；
- Renderer 每帧复制请求并注入实际 RenderTarget size，避免修改 Editor 持有的请求状态。

禁止显式相机缺失时回退很重要：回退会掩盖 Edit 编排错误，并可能让用户以为移动 editor camera 修改了 Scene Camera。

## 输入与可见性

ViewPanel 每帧开始先清空实际可见性、尺寸和输入状态。只有窗口未折叠且已经绘制 Image/占位 item 后才记录：

```text
input_active = window focused && image item hovered
```

Request 的输入策略为：

- 不可见或画面未获得焦点：`Disabled`；
- Edit 且画面 active：`EditorCamera`；
- Play 且画面 active：`RuntimeScene`。

当前 Input System 尚未建立，因此该字段先定义允许谁消费输入的契约，不在 Renderer 内读取 GLFW。未来转发必须消费这一许可，不能只看
EditorMode。隐藏/折叠请求不会调用 resize；现阶段仍可继续渲染已有离屏目标，彻底跳过隐藏 Viewport 的场景 pass 留到多视图/RenderGraph
调度具备明确语义时处理。

## 验证

- `ViewportRenderRequestTest.EditModeUsesEditorOnlyCamera`：Edit 生成显式相机与 editor input policy；
- `PlayModeUsesRuntimeSceneCamera`：Play 不携带 editor camera；
- `HiddenViewportDisablesInput` 与 `UnfocusedViewportDisablesInputOnly`：可见性、Camera 来源与输入许可彼此不混淆；
- `SceneResolverTest.ExplicitViewportCameraOverridesScenePrimary`：显式相机覆盖 Scene primary 并使用实际 aspect；
- `MissingExplicitCameraDoesNotFallBackToScene`：编排错误不会静默回退；
- 完整 Debug 构建与 CTest。

## 下一步

先在阶段切换点做一次架构复盘，确认阶段 3 资产模块和本步 Viewport 边界没有新增重复状态或越权依赖。之后进入阶段 4B，分离
panel content size、render resolution 和 image display rect，并加入 HiDPI framebuffer scale；相机交互与 2D 正交投影沿本请求扩展。
