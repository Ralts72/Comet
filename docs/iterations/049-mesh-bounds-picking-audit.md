# 049：拾取方案审计与 Mesh Bounds

## 目标

在实现对象拾取前先检查当前资源和渲染基础，选择不会在下一阶段被推翻的最小方案。本步结论是：

1. 先实现 CPU ray-AABB 拾取；
2. Runtime Mesh 保存 local bounds，但不保留完整 CPU geometry；
3. GPU ID pass/readback 留到需要三角形级精度且 RenderGraph resource contract 已建立后；
4. bounds 是通用 Runtime Mesh 元数据，不放在 Editor 或 ViewPanel。

## 当前条件

### CPU 路径原本缺少什么

`MeshData` 在导入和 GPU 创建阶段拥有 positions/indices，但 `Mesh` 发布后只保存 vertex/index Buffer、计数和 upload completion。CPU 顶点会随
candidate 释放，Runtime 没有 bounds 或 geometry 可供 ray cast。

完整保留 CPU 顶点可以做精确三角形拾取，但会让每个已加载 Mesh 同时维护 CPU/GPU 两份大数据；而当前编辑器只需要可靠选中静态物体，
没有精确点击薄三角形或 sub-mesh 的明确需求。

### GPU ID 路径现在会引入什么

当前传统 RenderPass 只有 color/depth 与可选 MSAA resolve。加入对象 ID 至少需要：

- 整数 ID attachment 及其 format capability；
- fragment shader/pipeline layout 输出变体；
- MSAA 下 ID resolve 或单采样 picking pass 策略；
- image layout/copy barrier 与按 frame slot 的 host-visible readback；
- click 对应哪一帧、哪一 camera/target generation 的 completion 追踪；
- resize、隐藏 Viewport、swapchain 与离屏 generation 切换时的资源退休。

这些正是阶段 5 RenderGraph pass/resource contract 要统一解决的内容。现在单独把它们硬编码进 SceneRenderer，会产生第二套 attachment 与同步
生命周期；同步等待一个像素还会让编辑器交互阻塞 GPU。

## 推荐方案

CPU ray-local-AABB 的 MVP 只需要每个 Runtime Mesh 保存 local bounds。它的精度是包围盒级，但具备以下复用价值：

- Viewport 对象粗拾取；
- Focus Selection 计算中心和尺度；
- 后续 frustum culling；
- debug bounds 绘制；
- 资产导入诊断与缩略图 framing。

因此 bounds 属于 `Mesh` 资源语义，不属于 ImGui 或某个 picking controller。

## 数据契约

新增 `core/geometry.h` 中的 `AxisAlignedBox`，保存 minimum/maximum，并提供：

- 从首个 point 构造；
- include point 扩展；
- finite 且 min <= max 的有效性检查；
- center 与 size。

`calculate_mesh_bounds(MeshData)` 是无 GPU 依赖的纯函数：空 vertices 或任一非有限 position 返回空；否则扫描全部 positions 产生 local AABB。
它不依赖 indices，因为未索引 Mesh 也必须有 bounds，且导入数据中的全部 vertex 都属于该 Runtime Mesh 候选。

`Mesh::try_create()` 在任何 GPU allocation/upload 之前计算 bounds。无效 CPU 数据继续按现有 Mesh 构造前置条件明确失败，不开始 GPU 副作用；
有效 bounds 与 buffers、completion、counts 一起进入私有构造函数。热重载只有完整新 Mesh 发布时才同时替换 bounds 与 GPU owner，不会把新 bounds
配到旧 buffers。

## 为什么不把 bounds 写进导入缓存

当前 mesh cache 已保存 vertices/indices，bounds 是一次线性扫描即可得到的廉价派生数据。此时修改 cache schema、版本和兼容测试只会复制状态，
并增加“缓存 bounds 与 vertices 不一致”的可能。等未来 shipping artifact 不再保存 CPU vertices 时，再把已验证 bounds 作为产物字段持久化。

## 自动化验证

- 多个正负坐标 position 产生正确 min/max/center/size；
- 空 vertices 与 NaN position 不产生 bounds；
- compile-time contract 确认 Runtime Mesh 暴露只读 local bounds；
- 完整 Debug 构建覆盖 engine、app、editor 与 tests；
- 完整 CTest 回归覆盖 importer/cache/后台刷新和 GPU upload。

本步没有可见 UI，也无需手工 GPU 验证。

## 下一步

增加通用 Ray 与 ray-AABB 纯几何测试，然后从当前 ViewProjectMatrix 和纹理像素生成 world ray；对每个 ResolvedRenderItem 将 ray 变换到
local space，选择最近非负命中。Renderer 只编排一次性 request/result，ViewPanel 不读取 AssetRegistry 或 GPU readback。
