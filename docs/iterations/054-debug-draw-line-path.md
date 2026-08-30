# 054：通用 DebugDraw Line Path

## 目标

建立一个不依赖 Editor/ImGui/Selection 的通用线段提交与渲染路径，为选中 bounds、gizmo、frustum、碰撞体和运行时诊断提供共同基础：

1. `DebugDrawList` 只保存 world-space position/color，不含 Vulkan 类型；
2. 提供通用 line 和 world AABB 12 边生成；
3. `DebugDrawRenderer` 使用独立 line-list pipeline 和无 descriptor 的 view-projection push constant；
4. 动态 vertex buffer 按 FrameSlot 持有，只在对应 slot fence 已等待后写入或增长；
5. DebugDraw 在主场景同一 subpass 尾部录制，默认 depth-tested、depth-write disabled；
6. Renderer 接受一次性 draw list，提交后清空，不把 debug primitive 写入 Scene 或 Material。

## 前后对比

改造前，当前渲染器只能通过固定 textured mesh pipeline 绘制 Runtime Mesh。要显示选中 bounds 只能临时修改材质 Shader、让 ImGui 画错误的
屏幕矩形，或把 gizmo 特例直接写入 SceneRenderer。

改造后：

```text
tool/runtime producer
  -> DebugDrawList (world position + RGBA)
  -> Renderer one-frame submission
  -> SceneRenderer current Camera + current RenderTarget
  -> DebugDrawRenderer
  -> current FrameSlot mapped vertex buffer
  -> line-list pipeline in current scene subpass
```

生产者只描述几何意图；Vulkan shader、pipeline、buffer 和 frame-slot 安全全部留在 render/debug executor。

## CPU 数据契约

`DebugLineVertex` 只有 `Math::Vec3 position` 和 `Math::Vec4 color`。`DebugDrawList` 提供：

- `add_line(start, end, color)`；
- `add_box(AxisAlignedBox, color)`；
- `reserve_lines()`、`clear()`、只读 vertex span 和 line count。

所有 position/color 必须有限，无效 line/box 不产生部分提交。AABB 使用固定八角编号和 12 条边顺序，方便测试、后续 gizmo picking 诊断和确定性捕获。
List 可移动并在每帧复用，不保存 Camera、EntityId、生命周期或后端 handle。

`add_box()` 接受 world AABB 而不是 Mesh/local transform；调用方决定语义。下一步 Editor 会复用 `transform_box()` 产生选中对象 world bounds，未来
frustum/physics 也可以直接提交自己的 world shape。

## GPU 执行路径

新增 `debug_line.vert/.frag`：

- vertex input 为 position/color；
- 唯一 push constant 是 64-byte `projection * view`；
- 无 descriptor set、Texture 或 Material；
- fragment 原样输出 vertex color。

Pipeline 使用 `LineList`、当前 RenderPass 的 MSAA sample count、动态 viewport/scissor、alpha blend，以及 `LessEqual` depth test；关闭 depth write，
避免工具线改变后续可见性。当前只有 depth-tested policy，overlay/x-ray 必须以后以显式第二 pipeline/batch 增加，不能通过强改 clip depth伪造。

DebugDraw 在普通 mesh draw 之后、render pass 结束之前执行，因此与当帧实际 Camera、depth attachment、RenderTarget 尺寸和 MSAA generation 一致；
ImGui 仍在离屏场景 pass 结束后合成，不参与线段坐标。

## FrameSlot Buffer 所有权

每个 FrameSlot 有独立 persistently mapped CPU-to-GPU vertex buffer。`SceneRenderer::begin_frame()` 已先等待当前 slot fence，因此本帧可以安全：

- 覆写该 slot 现有 buffer；
- 若容量不足，替换该 slot buffer；
- 保持其他在途 slot buffer 不变。

容量从 256 vertices 起按 2 倍增长，不每帧按精确大小重分配。`Buffer::try_create_cpu_buffer()` 是新增的可恢复 CPU-to-GPU 工厂；DebugDraw 作为
可选诊断能力使用 `within_budget`，增长失败只跳过本帧 debug lines，不影响主场景。相同失败规模每个 slot 每 120 次使用才重试并记录一次错误，避免
内存压力下每帧刷日志，同时允许预算恢复后重新建立。

Buffer 不需要 GpuRetirementQueue：增长发生前当前 slot fence 已完成，其他 slot 各自持有独立 owner；整个 executor 只在 render pipeline reset 的
全局安全边界销毁。`reset_render_pipeline()` 先销毁 DebugDrawRenderer，再销毁 PipelineManager 和 RenderPass。

## 一次性提交

`Renderer::submit_debug_draw()` 替换当前待提交 list。成功 begin frame 后，overlay prepare 可以为当前帧构建 list；SceneRenderer 消费后 Renderer
立即 clear。若 acquire/recreate 使 begin frame 失败，也会丢弃 pending list，不把旧工具几何错误复用到后续 Camera/target generation。

当前 Editor 尚未产生 list，因此本步不改变默认画面；下一步只需在现有 overlay prepare 中根据 Selection 构造 world box 并调用该 API，不需要再改
Shader、Pipeline 或 frame-slot 生命周期。

## 为什么这是引擎能力

DebugDraw 服务的不只是 ImGui：选择框、gizmo 是第一个消费者，后续 Runtime 的 camera/frustum、physics collider、navigation、lighting 和 culling
诊断都会复用。CPU list 不知道 Editor，executor 不知道 Selection；将其放在 `engine/src/render/debug/` 比放进 `ViewPanel` 或 editor 私有 renderer
更符合所有权，也避免创建只服务一个面板的包装类。

## 自动化验证

- line 产生两个位置/颜色一致的 vertex；
- AABB 产生确定顺序的 12 条边、24 个 vertex；
- NaN line 和无效 bounds 不产生部分数据；
- clear 后 list 可继续复用；
- glslangValidator 成功编译两份 DebugDraw Shader；
- engine、app、editor 与 tests 完整链接，验证 pipeline/layout/buffer API 契约；
- 完整 CTest 回归。

实际 depth、MSAA、颜色混合和 resize 后显示仍需在下一步接入 Selection producer 后做本地 Vulkan 视觉检查。

## 下一步

接入 selected bounds highlight：Editor 在 Edit 模式且 Selection 为带有效 Runtime Mesh 的实体时，计算 world AABB，向一次性 DebugDrawList 添加
12 条高亮线并提交；Play/资产选择/缺失 Mesh 提交空 list。该步骤不再增加渲染类，只连接现有数据所有者。
