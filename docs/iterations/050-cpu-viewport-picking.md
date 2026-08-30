# 050：CPU Viewport 对象拾取

## 目标

把第 046 步的屏幕点到纹理像素映射和第 049 步的 Runtime Mesh local AABB 连接起来，形成完整但保持轻量的编辑器选择闭环：

1. ViewPanel 只在 Edit 模式、可见画面内的左键点击事件上产生一次纹理像素请求；
2. Renderer 使用该帧实际提交的 Camera、模型矩阵和 Mesh bounds 完成 CPU ray-AABB 测试；
3. 最近命中的 `EntityId` 通过回调交还 Editor，并复用既有 Selection 服务；
4. 点击画面内空白区域清空选择，工具栏、留白和裁切不可见区域不产生请求；
5. 不增加 GPU ID attachment、readback、同步等待或 ImGui 到引擎的依赖。

## 前后对比

改造前，Viewport 已能把鼠标位置准确映射到当前离屏纹理像素，Runtime Mesh 也已有可复用 local bounds，但两者之间没有查询契约。Hierarchy
只能由自身交互更新 Selection，点击画面没有语义。

改造后，职责链为：

```text
ViewPanel 左键事件
  -> 当前纹理 pixel
  -> Renderer 一次性 pick request
  -> 当前 RenderSubmission + ViewProjectMatrix
  -> world ray -> object local ray -> Mesh local AABB
  -> 最近 EntityId / miss
  -> Editor Selection
```

这里使用的是当前帧即将绘制的 `RenderSubmission`，而不是重新从 Scene、AssetDatabase 或面板缓存拼一份候选集。因此不可渲染、资源尚未解析完成
或没有有效 Mesh 的实体不会被错误选中，拾取看到的对象集合与画面提交保持一致。

## 几何与坐标契约

`core/geometry.h` 增加通用 `Ray` 和 slab ray-AABB 相交函数。它们不依赖 Renderer、Scene 或 Editor，可继续用于 culling、debug query
和其他 CPU 空间查询。相交结果使用非负 ray 参数；射线起点位于盒内时返回 0，平行且位于 slab 外时返回 miss，无效或非有限输入直接拒绝。

`make_world_ray()` 使用左上角为原点的 RenderTarget pixel，并以像素中心采样。pixel 先按当前正高度 Vulkan viewport 变换到 NDC：X/Y
均为 `[-1, 1]`，深度为 `[0, 1]`；随后通过当前 `projection * view` 的逆矩阵得到 near/far world point。透视与正交 Camera 使用同一函数，
正交模式自然产生方向平行、起点不同的射线。

每个候选的 world ray 使用模型矩阵逆变换到 Mesh local space，再与 local AABB 相交。local direction 不重新归一化，使相交参数仍与归一化
world ray 的距离参数一致，非均匀缩放下也能正确比较不同对象的远近。距离在误差范围内相同则选择较小 `EntityId`，避免候选顺序造成不稳定选择。

## 所有权和时序

- ViewPanel 持有的只是当帧一次性 pixel 事件，不保存命中结果，也不读取 Scene 或 Runtime Mesh；
- Editor 在 overlay prepare 阶段消费事件并向 Renderer 提交请求；Play 模式不提交编辑器选择；
- Renderer 在 SceneResolver 产生当前 submission 后、SceneRenderer 录制 draw 前同步执行纯 CPU 查询；
- Renderer 只通过回调返回通用 `ScenePickHit`，不依赖 `Selection` 或 ImGui；
- Editor 收到 hit 时选择实体，收到 miss 时清空 Selection；Hierarchy 与 Inspector 下一次 UI prepare 自然读取同一个 Selection 状态。

整个链路没有每帧轮询。没有点击时不构造 ray 或候选数组；有点击时也不等待 GPU，不存在 readback 的 frame-slot、fence 或 target generation
生命周期问题。Viewport resize debounce 期间，ViewPanel 映射的是当前真实显示纹理，Renderer 使用的也是当前 active RenderTarget 尺寸，二者不会混用
尚未发布的新分辨率。

## 为什么接口位于引擎

`Ray`/ray-AABB 是通用几何能力；`scene_picking` 接受 `ScenePickCandidate` 或 `RenderSubmission`，是无 UI 依赖的场景查询能力，可被其他工具、测试
或未来 runtime query 复用。只有点击策略和 Selection 更新留在 Editor。这样没有为了 ImGui 创建 Engine 类型，也没有让 ViewPanel 获取 GPU/资产所有权。

`Renderer::request_viewport_pick()` 仍是当前单 Viewport 编排入口，而不是长期通用查询系统。未来如果出现多个同步 Viewport，可把 request 与对应
Viewport submission/generation 绑定；如果出现三角形级精度需求，再在阶段 5 的 RenderGraph 资源契约下评估 GPU ID pass，而不是扩张当前 CPU API。

## 自动化验证

- ray-AABB 覆盖最近命中、平行 miss、起点在盒内以及无效输入；
- world ray 覆盖透视中心像素、正交不同像素、越界像素；
- candidate picking 覆盖模型变换、最近命中、等距稳定排序、无效 EntityId、奇异模型矩阵与 miss；
- 完整 Debug 构建覆盖 engine、app、editor 和 tests；
- 完整 CTest 回归覆盖既有渲染、资源和编辑器纯逻辑契约。

真实 UI 点击、Hierarchy/Inspector 视觉同步和 2D/3D 场景选择仍需要在具备显示与 Vulkan 的本地环境手工检查；核心坐标和命中逻辑已有纯测试。

## 下一步

先增加 Focus Selection：从当前 Selection 对应实体的 world transform 与 Mesh local bounds 计算 world bounds/中心和观察距离，使 2D/3D editor
camera 能通过一次按键稳定聚焦选中对象。该步骤继续复用 bounds，且不修改 Scene。之后再审计选中高亮和 gizmo 的渲染/命令边界，把 Transform
修改接入 Undo/Redo，而不是让 gizmo 直接散落写组件。
