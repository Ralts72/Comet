# 051：Focus Selection

## 目标

让 Edit Viewport 在获得键盘焦点时通过 `F` 聚焦当前选中的可渲染实体，同时保持现有职责边界：

- ViewPanel 只产生一次性 focus 输入事件，不读取 Selection、Scene 或 Mesh；
- Editor 组合根只在事件发生时解析当前 Selection、实体 world transform 和 Runtime Mesh local bounds；
- 通用几何层把 local AABB 转换为 world AABB；
- 现有 editor camera controller 根据 world bounds、投影模式和当前 viewport aspect 更新 camera；
- 不修改 Scene Camera 或实体 Transform，不增加 Renderer/GPU 工作，也不创建只服务 ImGui 的 Engine 类。

## 前后对比

改造前，点击 Viewport 已能选择对象，但相机只能通过手工 orbit、pan、zoom 找回远处、缩放后很小或 Hierarchy 中直接选中的对象。Mesh bounds
虽然已有复用目标，实际只用于拾取。

改造后，交互链为：

```text
Viewport window focused + F pressed
  -> ViewPanel one-shot focus request
  -> Selection Entity
  -> MeshRenderer.mesh -> AssetRegistry Runtime Mesh
  -> Scene world matrix + Mesh local AABB
  -> world AABB
  -> editor camera focus math
  -> 下一份 explicit camera snapshot
```

无选中实体、选中资产、实体没有 MeshRenderer、Mesh 尚未加载或 bounds/viewport 无效时安全忽略请求。事件不会每帧重试，也不会产生日志刷屏。

## 通用 world bounds

`transform_box()` 位于 `core/geometry.h`，对 local AABB 的八个角执行齐次矩阵变换并重新包围。它拒绝无效 box、非有限结果、零齐次分量以及
除法后溢出的坐标，能够正确处理平移、旋转、非均匀缩放和负缩放。

这里不把 world bounds 缓存在 Scene 或 Mesh：

- Mesh 只拥有与资产内容一致的 local bounds；
- world bounds 同时取决于实体层级和当前 world transform；
- 当前 Scene 每次提取前已经更新 world transforms；Focus 是低频事件，再计算八个角的成本很小；
- 避免引入 transform dirty/revision 尚未建立时无法可靠失效的重复状态。

该纯函数后续可直接服务 world-space culling、debug bounds 和选中高亮，不属于 Editor 专用 API。

## 相机聚焦策略

### 3D Perspective

聚焦保留当前 `position - target` 的观察方向，把 target 移到 world bounds 中心。使用 world AABB 的包围球半径，同时考虑垂直 FOV 和由 aspect
推导的水平 FOV，选择更窄的半视角计算观察距离，并增加 20% framing padding。距离继续服从 editor camera 的最小/最大范围。

使用包围球会比针对每个相机方向投影八角点略保守，但结果稳定、旋转无关，且当前 Focus 是工具交互而非高频渲染路径。出现超大场景或需要更紧
framing 时，可以在同一 controller API 内替换为 view-space corners 计算，不影响 ViewPanel、Selection 或资产边界。

### 2D Orthographic

2D 固定观察 world XY，因此需要显示的高度为：

```text
max(world_bounds.height, world_bounds.width / viewport_aspect) * 1.2
```

controller 更新 `orthographic_height` 并把 target 移到 bounds 中心；`position - target` 的方向和距离保持不变，所以切回 3D 后仍沿聚焦前的
观察方向看向新目标。正交 snapshot 继续使用固定 +Z 观察轴。

## 输入与所有权

`F` 只有在 Viewport window 获得焦点且 ImGui 未请求文本输入时才产生事件，不会抢占路径输入框或其他面板的文本编辑。ViewPanel 每次 render
先清空上帧事件，Editor 在同一个 overlay prepare 中按以下顺序消费：

1. 应用 2D/3D 切换与鼠标 camera input；
2. 应用一次 focus request，使显式聚焦成为该帧最终 camera 状态；
3. 生成 ViewportRenderRequest；
4. 提交可能存在的 pick request。

Focus 不通过 Renderer，是因为它查询的是 Editor Selection 语义并直接修改 editor-only camera；Renderer 不应知道 Selection。CPU picking 仍通过
Renderer，是因为它必须与当帧实际 `RenderSubmission` 候选和 Camera 保持一致。这两个方向看似都属于 Viewport 交互，但数据所有者不同，因此不强行
收敛成一个万能 Viewport service。

## 自动化验证

- local AABB 经平移、90 度旋转和非均匀缩放后产生正确 world center/size；
- 非有限矩阵不会产生 world bounds；
- Perspective focus 更新中心、保留观察方向并给 bounds 留出距离；
- Orthographic focus 同时考虑宽度/aspect，得到带 padding 的正交高度并保留 3D offset；
- 无效 bounds、无效 aspect 不修改 camera；
- 完整 Debug 构建和 CTest 覆盖 engine、app、editor 及既有资源/渲染回归。

真实按键焦点、选中对象后的视觉 framing 和 2D/3D 切换仍需在具备显示与 Vulkan 的本地编辑器中手工确认；核心几何与相机数学已有纯测试。

## 下一步

做一次选中高亮与 gizmo 技术审计。优先明确高亮属于场景渲染 pass 还是 editor overlay、gizmo 如何复用 ViewProjectMatrix/depth，以及 Transform
修改必须经过怎样的命令边界进入 Undo/Redo；不要先把轴绘制和组件写入硬编码进 ViewPanel 或 SceneRenderer。
