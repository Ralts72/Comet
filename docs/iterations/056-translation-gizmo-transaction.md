# 056：Translation Gizmo Transaction

## 目标

在现有 Selection、DebugDraw 和 Editor Command History 之间建立第一条可操作的 Transform Gizmo 纵向链路：选中实体后显示固定屏幕尺寸的世界坐标移动轴，按轴拖拽实时修改 Transform，释放时只记录一个可撤销命令。

本轮只实现 Global Translation，不提前混入旋转、缩放、本地坐标、吸附或 GPU picking。

## 前后对比

改造前，选中实体只有 world AABB 高亮；Viewport 左键事件直接量化为整数纹理像素并提交场景拾取，无法区分“点击对象”和“操作工具”。

改造后：

```text
ViewPanel continuous pointer event
  -> Editor builds selected-entity gizmo context
  -> TranslationGizmoController
       hover: projected axis hit test
       press: begin transaction
       drag: ray / world-axis closest parameter
       release: one before/after commit
  -> Editor applies Transform translation
  -> existing EntityPropertyEditCommand
  -> Undo / Redo
```

未命中 Gizmo 的左键按下仍走原有 CPU scene picking。命中 Gizmo 后该 press 被工具消费，不会穿透并改变 Selection。

## 模块边界

`editor/src/transform_gizmo.*` 是纯 Editor 工具模块，不依赖 ImGui、Scene、Selection、Renderer 或 Command History：

- `TranslationGizmoFrame` 保存当前 Camera 的 view/projection、纹理分辨率、world origin 和轴长；
- 投影、轴命中、pointer ray、轴参数和 parent-local 换算都是纯函数；
- `TranslationGizmoController` 只维护 hover/active transaction，输入 context/pointer DTO，输出临时 edit 或最终 commit；
- 轴线和箭头写入通用 `DebugDrawList`，没有 gizmo 专用 Vulkan renderer。

Editor 组合根继续负责跨所有者连接：从 Selection/Scene 解析 EntityUuid、local translation、world origin 和 parent inverse，应用 controller edit，并把 commit 交给已有
`EntityPropertyEditCommand`。因此工具状态不会写入 Scene，Controller 也不持有 Entity、组件指针或 Engine 引用。

最初实现曾把完整拖拽状态机直接放在 `editor.cpp`；本轮在提交前将其收敛进现有 transform gizmo 文件，使入口只保留 context 解析和结果应用，也让取消、释放和单命令语义能够独立测试。这是一个有明确生命周期和后续扩展点的 Editor 工具对象，不是只包装一次 ImGui 调用的类。

## 连续坐标与 DPI

`ViewportLayout` 新增连续的 screen point → texture pixel-position 映射。原有场景拾取继续使用向下取整、边界夹取后的整数 pixel；Gizmo hover/drag 使用浮点 pixel，避免细小鼠标移动被整数化。

ViewPanel 只在左键从可见画面内按下时建立 pointer capture。拖拽开始后，即使指针离开画面，仍按完整 image display rect 产生可超出纹理范围的连续坐标，直到 release；Gizmo ray 因而可连续延伸，场景 picking 仍严格拒绝画面外点击。

轴显示长度 84 logical pixels、命中半径 8 logical pixels；Editor 根据当前纹理像素与 display rect 比例换算为 texture pixels。透视相机按 Gizmo origin 的 view depth 计算 world-units-per-pixel，正交相机按 orthographic height 计算，因此 resize、DPI 和相机远近变化不会让工具尺寸随世界尺度漂移。

## 拖拽数学

按下可见轴时，Controller 保存：

- 稳定 `EntityUuid`；
- 开始时 local translation；
- Gizmo world origin、global axis 与起始 closest-axis parameter；
- `world_to_parent`。

每帧把 pointer 转为 world ray，求 ray 与无限 world-axis 两直线的最近参数差，得到 global world displacement。目标 world origin 再通过 `world_to_parent` 转回实体 local translation，所以带旋转/缩放父节点的 child 仍能沿世界轴移动。相机方向与轴近似平行时没有稳定解，该轴不会开始拖拽；奇异 parent inverse 同样不会发布无效 Transform。

当前轴是 Global X/Y/Z。轴使用红/绿/蓝，hover/active 使用黄色；每轴由主线和两条箭头翼组成，仍通过当前 depth-tested DebugDraw pipeline 绘制。

## 事务与取消

拖拽期间 Transform 实时更新，但不逐帧写命令。左键释放时 Controller 输出一次 `{before, after}`，Editor 复用现有
`EntityPropertyEditCommand(entity_uuid, "transform", "translation", ...)` 记录已应用命令。

以下边界会取消并恢复开始值，不产生历史项：

- Selection/实体目标失效或切换；
- Viewport 隐藏、折叠或失去有效 render layout；
- 切换 Edit/Play 前；
- 拖拽期间触发 Undo/Redo。

Scene owner 真正替换后只 reset Controller，因为旧 Scene 已由 session 所有权边界接管，不能再拿新 Scene 解析旧目标。

## 验证

- 透视和正交投影下的轴屏幕尺寸稳定；
- texture pixel 空间的 X/Y 轴命中和稳定 tie-break；
- 与相机平行的轴拒绝不稳定拖拽；
- global axis displacement 可转换为旋转 parent 下的 local translation；
- DebugDraw 生成三组彩色箭头，共 9 条线；
- Controller 从 press 到 release 只产生一个 commit，cancel 恢复 transaction start；
- 连续 Viewport 映射支持 capture 后的画面外坐标，同时保留原整数 picking 契约；
- mode request 在场景切换前可见，使组合根先取消活动事务；
- editor、engine、app、tests 全量构建和 CTest 回归。

## 已知边界与下一步

当前只支持 Global Translation，且 Gizmo 与 selected bounds 共用 depth-tested line policy；轴朝向相机时会因投影退化而不可选，这是数学上明确的退化而不是任意拖拽。Scene extraction 仍早于 overlay prepare，因此 Mesh 视觉可能比实时 Gizmo/高亮慢一个帧。

下一步增加 Gizmo mode 与快捷键，按同一 Controller/transaction 契约实现 Rotation、Scale 和可选 snapping；完成三种 Transform 工具后再评估 overlay/x-ray DebugDraw policy。
