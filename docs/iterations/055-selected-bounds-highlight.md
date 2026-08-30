# 055：Selected Bounds Highlight

## 目标

把上一轮通用 DebugDraw line path 接入编辑器 Selection，让 Edit Viewport 中当前选中的可渲染实体得到即时、与场景深度一致的世界空间包围盒反馈，同时不新增只服务 ImGui 的渲染类。

## 前后对比

改造前，Hierarchy 或 Viewport 拾取已经共享同一个 `SelectionService`，`F` 也能聚焦选中 Mesh，但画面本身没有持续反馈；DebugDraw 已能绘制 world-space line/AABB，却没有任何生产者。

改造后，Editor 的 overlay prepare 在 UI 更新选择之后执行一次轻量连接：

```text
SelectionService
  -> selected Entity
  -> MeshRendererComponent.mesh
  -> AssetRegistry Runtime Mesh local AABB
  -> Scene world matrix
  -> transform_box() world AABB
  -> DebugDrawList::add_box()
  -> Renderer one-frame submission
```

Scene、Selection、AssetRegistry 和 Runtime Mesh 的解析仍留在 Editor 组合根；`ViewPanel` 不读取实体或资源，`DebugDrawRenderer` 也不感知 Selection。

## 复用与职责收敛

原先 Focus Selection 在 `apply_viewport_focus()` 内独立完成实体、Mesh 和 world bounds 解析。本轮将这段连接收敛为 Editor 私有的
`resolve_selected_mesh_world_bounds()`：Focus 和持续高亮复用同一语义，不缓存第二份 bounds，也没有为单一 UI 功能创建新文件或 engine 类。

这个 helper 仍属于组合逻辑，而不是通用引擎抽象：它横跨 Editor Selection、Scene 与 AssetRegistry，抽到 engine 会反向引入编辑器状态；抽成独立
service 则只有一个调用所有者，没有独立生命周期或策略价值。

## 提交规则

只在以下条件全部满足时提交 12 条线：

- 当前为 Edit 模式；
- Viewport 本帧实际可见；
- Selection 当前选择的是仍存在的实体；
- 实体具有 `MeshRendererComponent`；
- Mesh Handle 已解析为有效 Runtime Mesh；
- local AABB 与世界矩阵能产生有限 world AABB。

Play、资产选择、隐藏/折叠 Viewport、缺失组件或尚未加载 Mesh 时均不提交。Renderer 每帧消费后清空 DebugDraw list，因此 Selection 清除后不会残留上一帧高亮。

高亮颜色目前固定为橙黄色，使用现有 depth-tested、depth-write-disabled line pipeline。它表达的是 Mesh world AABB，不修改 Material，不给场景实体添加编辑器专用组件，也不把选择状态写入 RenderScene。

## 帧时序

Hierarchy/Inspector/Viewport UI 在 overlay prepare 内先更新，随后相机输入、Focus、Viewport request 和选中 bounds 都在 SceneResolver 前完成，因此高亮使用本帧 Camera 和目标尺寸。Viewport 点击拾取依赖本帧解析后的 `RenderSubmission`，其 Selection 回调发生在这次 prepare 之后，所以鼠标新选中的高亮从下一帧出现；该路径没有等待、readback 或额外同步。

本轮提交时，Inspector 在同一次 prepare 中修改 Transform 会让高亮领先旧 RenderScene snapshot 一个帧；迭代 057 已将 Scene extraction 延迟到 overlay prepare 之后，现已由同帧最新 world matrix 同时驱动 bounds 和 Mesh model matrix。

## 验证

- DebugDraw 单元测试继续覆盖 AABB 的 12 边确定性、无效输入与 clear 复用；
- editor 完整编译，验证 Selection/Scene/AssetRegistry/DebugDraw 的连接契约；
- engine、app、tests 全量构建；
- 完整 CTest 回归。

本轮没有新增可单独单测的算法；新代码是现有已测组件之间的组合根连接。实际颜色、遮挡效果与操作反馈仍需通过本地 Vulkan 编辑器视觉检查。

## 下一步

在同一 DebugDraw producer 上建立 Transform gizmo 的纯交互状态与屏幕/世界轴命中，不新增 gizmo 专用渲染器；拖拽开始到释放形成单个 Transform 命令，复用现有 Command History 完成 Undo/Redo。
