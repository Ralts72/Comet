# 024：材质 Shader 布局与驻留资源整组重建

## 背景与验收边界

023 能在编辑器后台编译材质 Shader，但只接受完全兼容的接口。改一个 UBO 字段顺序或纹理 binding 就会被拒绝。
直接放开检查也不正确：旧 PreparedMaterial 的字节位置、旧 descriptor 布局与新 Shader 不再一致，可能错误取值甚至违反 Vulkan 约束。

本项完成阶段 5 的一个独立验收：**已登记材质属性的布局变化能够整组重建，并保护仍在途的旧帧**。
不把新增任意属性、自动推断颜色语义或改写 Frame/Object 的固定 C++ 数据结构计为完成。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| ShaderManager 准备候选时拒绝任何接口变化 | ShaderManager 只管 GPU Shader 候选，消费端决定哪些接口可以重建 |
| MaterialLayout 的 offset、块大小和 binding 完全手工固定 | 从实际 Shader 反射重绑定，手工元数据继续管理语义 |
| 参数 UBO 的 binding 在 GPU 创建／写入路径硬编码为 0 | 两条路径统一读取 MaterialLayout::get_parameter_binding() |
| 兼容热更只提前创建 Pipeline，材质版本下一次 render 才切换 | 热更提前准备所有驻留 CPU／GPU 材质，成功后一次发布 |
| Inspector 始终查内置静态布局 | 编辑器启动／成功热更时交付已发布布局快照；独立面板保留内置回退 |
| 只检查采样 descriptor 的类型和数量 | 同时校验图片维度、数组、深度比较、多采样和浮点／整数类型 |

## 代码级调用链

### 1. 将语义属性对应到真实字节布局

`MaterialLayout::reflect(metadata, shader)` 返回不可变布局。Texture/Scalar/VectorProperty 新增可选 `shader_name`：
空字符串表示使用逻辑属性名；已有 `.mat` 的 `u_Texture0` 显式映射 GLSL 的 `texture0`，不重写资产文件。

反射仅处理 MaterialSet 1，按名字匹配已登记属性，更新：

- scalar/vector 的 offset；
- uniform block 的大小与 binding；
- 每个纹理槽的 binding。

标签、默认值、范围、步长、Color/Vector 语义从原元数据保留。若布局没有实质变化，返回原 shared_ptr；
变化时创建 revision + 1 的新对象，并重新校验 std140 范围、对齐与冲突。revision 溢出明确报错。
未知／缺失／类型不同的参数、额外资源块等情况拒绝，不随意生成一个“看起来能编辑”的字段。

示例：原 `color@0 / intensity@16 / UBO binding 0 / 32 bytes`，可以变成
`intensity@0 / color@32 / UBO binding 5 / 48 bytes`。Material 的逻辑属性名和值不变，CPU 参数重新打包。

### 2. 消费端区分固定契约和可重建部分

`ShaderInterface::has_same_layout(other, ignored_descriptor_set)` 支持忽略指定 descriptor set，
但仍比较阶段、输入输出、push constant 及其他 set。MaterialRenderer 对顶点 Shader 比较全部契约，
对片元 Shader 只允许 MaterialSet 1 进入上述重建路径。交换 Frame 的两个 mat4，即便总字节大小相同也会被拒绝。

`DescriptorBinding` 还拥有图片形状快照；当前 Material 只允许单采样、非数组、非比较的 float sampler2D。
同为 CombinedImageSampler 不能让 Cube、整数图片等悄悄通过。反射数据仍不持有第三方解析器指针。

### 3. 先准备所有版本，再发布

`MaterialRenderer::reload_shaders()` 的执行顺序是：

```text
ShaderManager::prepare_update
  → 检查固定接口
  → 两个 MaterialLayout + DescriptorSetLayout + Pipeline 候选
  → 复制 MaterialRuntimeCache 与驻留材质索引
  → 每个驻留材质：rebind → create_material
  → 所有候选都成功
  → Shader / Pipeline / CPU cache / GPU material 四组 noexcept swap
```

`MaterialRuntimeCache::rebind()` 用保留的源 Material 和新布局重新生成 PreparedMaterial；复制缓存后只修改候选。
`create_material()` 从原 `prepare_material()` 中抽出真正的资源构建逻辑，普通材质编辑与热更共用，不重复维护 descriptor 写入代码。
兼容 Shader 换体时，PreparedMaterial 和 DescriptorSetLayout 不变，可以共享原参数 buffer/pool/set，仅替换版本包装中的 PipelineState。
布局变化则创建新 buffer/pool/set。任一候选准备异常，已发布索引完全不动。

旧 FrameSlot 已经保留 MaterialResources → PipelineState → Shader/Layout；新提交只读取新索引。
没有原地修改旧 descriptor，没有 device-wide wait，没有额外通用退休队列。GPU 完成后既有 slot 回收机制释放旧版本。

`ReloadReport` 记录本次发布新建了多少 Pipeline、材质版本和绑定。热更现在发生在 render 之前，
因此随后 render 的 `material_versions_created` 可以为 0，不能再拿这个逐帧统计代表本次热更工作量。

### 4. Inspector 使用同一份布局语义

SceneRenderer 转交 MaterialRenderer 的已发布布局快照；Editor 在面板创建后及成功发布后调用 `set_material_layout()`。
Inspector 的控件构建和草稿验证都先查该快照，再回退内置定义，不保存 Renderer 指针，不增加每帧解析回调或全局事件总线。
交付布局不会清空正在编辑的 MaterialData，也不触发保存。渲染目标重建时，新的 MaterialRenderer 从当前 Shader 再反射布局，
不会误用嵌入初始 Shader 的 offset/binding。

## 设计理由与架构价值

- GPU 对象缓存不负责猜测使用者的 C++ ABI；可变接口策略放回真正的资源消费端。
- 同一 MaterialLayout 同时指导 CPU 打包、GPU descriptor 和 Inspector，消除三处各自硬编码。
- 反射是物理布局来源，语义元数据是编辑含义来源，两者各司其职，不需要再引入一个全局 LayoutManager。
- 发布点是整组资源事务，而不是逐材质懒更新；错误不会形成部分材质使用新布局、部分材质使用旧布局的混合组。
- 复用现有文件与生命周期：未新增生产目录、专用服务类或回调编排层；下一次正式架构回顾仍是 025。

## 测试结果

- Debug / Release 全量各 478 个 GoogleTest，加独立 `shader_build_contract` 通过。
- 8 个相关测试连续运行 20 次，共 160 次通过；本机图形测试进程串行运行。
- CPU：改变 offset／块大小／binding，验证默认值和颜色语义保留、PreparedMaterial 重打包、候选缓存隔离与 swap。
- CPU：拒绝未知、缺失、增加、改类型的字段；拒绝 Cube、数组、深度比较、整数和多采样材质纹理。
- 实际 ImGui UI：交付新布局标签和默认值后拖动产生正确属性更新，空闲／重复交付不写入，不要求 Apply。
- 实际 Vulkan：两个材质、两个在途 slot，布局及纹理 binding 同时变化；读回旧／新颜色，旧 Pipeline 在 slot 完成后释放。
  驻留材质缺纹理导致整组失败时 Shader 与布局仍为旧版；修复后重新发布成功。兼容热更不分配新的材质绑定。
- SceneRenderer：活动帧内拒绝发布，帧边界幂等发布不创建版本；Frame mat4 顺序变化拒绝发布并保留旧 Shader。
- Vulkan 测试捕获日志，没有 VUID / Validation Error；没有进行人工桌面体验验收。
- 上一项 023 的 Linux CI `34057650139` 已成功；本项 Linux CI 在本次 push 后触发，未提前声称通过。

## 限制与后续方向

- 目前只热更新已登记的两个内置材质布局；自定义布局注册、新属性默认值、图片语义及资产级 variants 仍按路线图扩展。
- 对 shader 字段名有依赖；字段重命名需同时调整显式语义映射，不能从类型相同推断它是哪个属性。
- Frame/vertex/push 的 ABI 不自动重建；普通 2D Material 不能自动接纳 Cube／数组／整数采样。
- 整组 GPU 资源准备在 owner 帧边界同步执行；大量驻留材质可能形成短暂峰值内存和帧延迟。未做 GPU OOM 注入或大场景性能承诺。
- layout revision 只在当前布局演进链内单调，重建 owner 后仍以 shared_ptr 身份失效缓存，不冒充持久全局版本号。
- 文件监听、输入一致性与 debounce 沿用 023 限制。下一项接通 Debug Shader 热更新，并进行 025 架构回顾；
  随后继续 PipelineCache 恢复、多 pass/资源状态与阶段 5 剩余实际渲染验收。阶段 5、6 均未完成。
