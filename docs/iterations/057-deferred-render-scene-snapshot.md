# 057：Deferred RenderScene Snapshot

## 目标

修正 Editor UI 修改与场景渲染快照之间的一帧时序偏差：Renderer 完成 FrameSlot 等待、swapchain acquire 和 overlay prepare 后，再让 Engine 从当前活动 Scene 提取 `RenderScene`，使 Inspector 与 Gizmo 的 Transform 修改在同一帧进入 Mesh draw。

## 原始时序及问题

改造前：

```text
Engine update callbacks
  -> SceneExtractor::extract(Scene)
  -> Renderer::on_render(prebuilt RenderScene)
       -> begin_frame / acquire
       -> overlay prepare (ImGui edits Scene, camera, Selection, DebugDraw)
       -> SceneResolver
       -> scene draw / overlay draw / submit
```

Overlay prepare 虽然已经位于 `SceneResolver` 前，所以 editor camera 能同帧生效，但 `RenderScene` 更早就在 Engine 中复制完毕。Inspector 或 Translation Gizmo 修改
`TransformComponent` 后，bounds/Gizmo 按最新 Scene 位置绘制，Mesh draw 却仍消费旧 model matrix，视觉上持续慢一帧。

这不是 DebugDraw、Gizmo 或 Transform 缓存问题，而是场景快照建立在错误的编排边界。

## 新时序

`Renderer::render_frame()` 不再接收已经构造好的 `RenderScene`，而是接收一个窄的 `RenderSceneProvider`：

```text
Engine update callbacks
  -> Renderer::render_frame(provider)
       -> collect completed uploads
       -> begin_frame / wait slot / acquire / apply target resize
       -> overlay prepare
            ImGui edits Scene
            camera / Focus / Gizmo / DebugDraw
       -> provider()
            SceneExtractor::extract(current active Scene)
       -> SceneResolver
       -> picking
       -> scene draw / overlay draw / submit
```

Engine 的 provider lambda 每次被调用时读取 `m_scene`，所以 overlay 内的 New/Open 等 Scene owner 替换也不会让本帧继续提取旧 Scene。SceneExtractor 更新 world transforms 后复制出的 model/camera snapshot 与同帧工具状态一致。

## 为什么用延迟 Provider

另一种做法是公开 `Renderer::begin_frame()` 与 `Renderer::render(scene)` 两段 API，让 Engine 在中间提取场景。但这会把“已经 acquire 且 command buffer 已开始”的半开帧状态暴露给调用方，要求每条异常/提前返回路径都正确结束或放弃 frame，也更容易重复调用或漏调用。

延迟 provider 保持 Renderer 对完整帧事务的单一所有权：

- begin 失败时清理一次性 DebugDraw 且不调用 provider；
- begin 成功后 Renderer 保证按 prepare → snapshot → resolve → record → submit 顺序消费；
- Engine 仍独占 Scene/SceneExtractor，不把 Scene 指针交给 Renderer；
- Renderer 只认识返回值 `RenderScene`，没有反向依赖 Scene/ECS。

`std::function<RenderScene()>` 每帧只调用一次，lambda 为小捕获；在当前 Scene extraction 成本与编辑器阶段下，这个间接调用远小于暴露半开帧状态带来的复杂度。后续如果 profiling 证明需要，可替换为项目内 function-ref，而不改变时序契约。

## 行为变化

- Inspector 的 Transform 值变化同帧进入 SceneExtractor 和 Mesh model matrix；
- Translation Gizmo 的临时 edit、selected bounds 和 Mesh 不再彼此错开一帧；
- editor camera、Viewport request 和 DebugDraw 继续同帧生效；
- overlay 内替换活动 Scene 后，provider 提取新 owner；
- runtime app 没有 overlay，仍在 acquire 成功后正常提取一次场景；
- swapchain out-of-date/recreate 导致 begin 失败时不做无用 Scene extraction。

Viewport scene picking 的命中回调仍发生在 snapshot/resolve 后，而选中 bounds list 已在本帧 overlay prepare 中生成，因此“点击新对象后高亮出现”仍是下一帧。这是 pick result 与工具 producer 的有意事件边界，不涉及 Mesh/Gizmo Transform 错位，也不引入第二次 UI prepare。

## 验证

- Renderer 接口只接受 deferred `RenderSceneProvider`，不再接受 prebuilt `RenderScene`；
- SceneExtractor 现有测试继续覆盖 world transform 与 camera snapshot；
- Translation Gizmo/Command History 定向回归；
- engine、app、editor 和 tests 全量编译；
- 完整 CTest 回归。

接口测试能锁定延迟快照契约；实际 Inspector/Gizmo 与 Mesh 的同帧视觉一致性仍需本地 Vulkan 编辑器交互检查。

## 下一步

在已对齐的帧时序上补齐 Rotation / Scale Gizmo modes、快捷键和 snapping；不再为每种工具增加独立帧通道，继续复用同一 pointer、Controller transaction、DebugDraw 和 Command History 契约。
