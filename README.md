# Comet 引擎

Comet 是一个基于 C++20、CMake 和 Vulkan 的引擎/编辑器项目。当前项目重点是图形资源封装、基础渲染流程、ImGui 编辑器面板和
GoogleTest 测试基础。

## 项目结构

- `engine/`：引擎核心库，包含 `asset/`、`common/`、`config/`、`core/`、`diagnostics/`、`graphics/`、`render/`、`runtime/` 和 `scene/` 模块；`asset/` 当前提供轻量、不透明的 `AssetHandle` 和带类型校验的最小内存
  `AssetRegistry`，并定义持久化资产身份、类型化 Importer 设置、`.meta` 编解码、失败安全的事务式源资产快照、变化集及 Material/Texture 正反向依赖查询；`core/project_paths` 定义项目根目录与
  `assets/`、`.comet/`、`ProjectSettings/` 的标准路径契约；`.comet/` 再区分可重建 cache 与当前机器的 editor 状态；`graphics/` 按 command、resource、pipeline 和 synchronization 组织 Vulkan 包装，跨组的 Context、Device、Queue、Swapchain 与 RenderPass 保留为顶层编排对象；`graphics/synchronization/GpuCompletionPoint` 表达可跨上传、渲染和延迟销毁复用的非拥有 GPU 完成 token，`graphics/command/UploadManager` 统一 staging、copy/barrier batch 和 completion 生命周期；`render/resource/` 集中放置 Texture/Mesh 的 CPU DTO、程序化 Mesh、Runtime 对象与设备资源创建边界，`render/scene/` 集中放置场景提取、解析和提交渲染流水线。
- `editor/`：ImGui 编辑器入口和面板；Hierarchy 以父子树展示 Scene 并支持拖拽调整层级，Inspector 通过组件描述符
  编辑 Transform、MeshRenderer 和 Camera，并可编辑 Project 中选中的 Material 以及 Texture 导入设置；Edit Viewport 支持 2D/3D 相机控制、左键选择当前渲染对象、空白点击清除选择，并可在 Viewport 聚焦时按 `F` framing 当前选中 Mesh；Project 会在资产源文件树变化时自动刷新，File 菜单可新建、打开和保存 `.scene`；`editor/resources/` 保存不进入
  项目资产数据库的编辑器私有字体等资源。
- `app/`：运行时示例程序入口。
- `assets/`：当前示例项目的源资产及相邻 `.meta`，当前包含 demo Texture、Material 和 glTF Mesh；资产身份进入版本控制。
- `config/`：`common.yaml` 保存共享运行配置，`profiles/` 保存构建/启动环境覆盖。
- `.comet/`：项目本地数据根目录，整体不进入版本控制；`cache/imported/mesh/<AssetHandle>.bin` 保存可重建 Mesh
  产物，缓存命中前会校验格式/Importer 版本、源 glTF 和外部 buffer 内容指纹；同一输入快照还会贯穿 Worker candidate、cache 写入和 Owner Thread 发布验证；`editor/imgui.ini` 保存当前机器的
  ImGui 窗口与 Docking 布局。
- `tests/`：GoogleTest 测试，覆盖数学、配置、Scene/ECS、场景序列化、原子文件写入、Mesh 导入产物、Asset Manager 更新事务、资源参数保护和渲染数据链路。
- `engine/shaders/`：引擎自有 GLSL Shader 源码，构建时编译并嵌入引擎。
- `3rdparty/`：第三方依赖目录，部分依赖通过 Git submodule 拉取，Vulkan Memory Allocator 和 EnTT 以 vendored
  源码形式维护；EnTT 只提交 single header。

## 环境依赖

- CMake 3.31 或更新版本。
- 支持 C++20 的编译器。
- Vulkan 开发环境和 `glslangValidator`，用于编译 shader。
- Git LFS，用于拉取 Texture、字体等二进制资源。
- Git submodules，用于拉取 `fastgltf`、`simdjson`、`glfw`、`glm`、`googletest`、`spdlog` 和 `yaml-cpp`；
  `imgui`、`stb_image`、`3rdparty/VulkanMemoryAllocator/` 和 `3rdparty/entt/` 直接随仓库提交。

初始化依赖：

```bash
git lfs install
git lfs pull
git submodule update --init --recursive
```

## 构建与运行

项目通过 `CMakePresets.json` 维护三种常用构建组合：

- `dev-debug`：Debug 构建 app、editor 和 tests，用于开发、调试与测试。
- `editor-dev`：RelWithDebInfo 只构建 editor，用于日常高性能编辑并保留调试符号和可按配置启用的 Profiler。
- `app-release`：Release 只构建 app，用于运行时性能与发布行为验证。

完整构建 app、editor 和 tests（Debug，不启动程序）：

```bash
./build.sh
```

构建并运行 RelWithDebInfo 编辑器：

```bash
./editor.sh
```

构建并运行 Release 示例程序：

```bash
./release.sh
```

脚本分别调用同名用途的 CMake preset，也可以直接使用 `cmake --preset <name>` 和
`cmake --build --preset <name> --parallel`。底层目标仍可通过以下选项独立组合：

- `COMET_BUILD_APP`：构建运行时示例程序，默认 `ON`。
- `COMET_BUILD_EDITOR`：构建 ImGui 编辑器，默认 `OFF`。
- `COMET_BUILD_TESTS`：构建 GoogleTest 测试，默认 `OFF`。
- `COMET_CONFIG_PROFILE`：选择 `dev-debug`、`editor-dev` 或 `app-release` 运行配置层；必须显式指定，三个 preset 已分别绑定同名 profile。
- `COMET_NATIVE_OPTIMIZATION`：增加面向构建机器 CPU 的优化，默认 `OFF`；需要分发的二进制不应开启。

不使用 preset 手动配置时，必须同时传入 profile，例如
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCOMET_CONFIG_PROFILE=dev-debug`。

## 测试

项目测试目标为 `unit_testing`，通过 CTest 运行：

```bash
./build.sh
ctest --preset dev-debug
```

## 开发说明

运行配置位于 `config/`：`common.yaml` 保存窗口、Vulkan 和渲染的共享设置，`profiles/` 下的配置按顺序覆盖
共享层。运行时启动层根据目标的默认 profile 选择配置文件，`ConfigLoader` 只负责合并显式传入的 YAML 并最终校验为
纯数据 `Config`，再由 `Application` 和 `Engine` 消费；yaml-cpp 只存在于加载器实现中，底层渲染资源不直接读取原始配置。

`diagnostics` 配置域负责日志、Profiler 和 Vulkan Validation 的运行时策略。日志在所有构建中保留
Info/Warn/Error/Fatal，Debug/Trace 只在 Debug 中编译，运行时再由 `diagnostics.log_level` 过滤。Profiler 代码只在
Debug 和 RelWithDebInfo 中编译，并由 `diagnostics.enable_profiler` 决定运行时是否采样；默认 `dev-debug` 开启，
`editor-dev` 关闭。`app-release` 不编译 Profiler，因此对应 profile 不提供 `enable_profiler` 字段，并使用安全默认值
`false`。`Application` 持有 `Diagnostics` 生命周期对象，由它统一完成 Logger/Profiler 的能力协商与关闭；`Engine`
销毁后再汇总 Profiler 数据，最后关闭 Logger。Validation 默认只在 `dev-debug` profile 开启，运行期 Debug Utils Messenger 由
`Context` 持有。三个 profile 暂时都关闭文件日志；发布日志迁移到用户数据目录并加入轮转策略后，再为 `app-release`
开启。`render.max_anisotropy` 表达项目期望的过滤倍率，`1` 表示关闭；

诊断开关遵循“宏控制编译期能力，配置控制运行时策略”：宏用于从二进制中移除 Debug/Trace 日志调用点和 Profiler
采样代码，YAML 只能启用当前构建已经具备的能力。Validation 不需要条件编译，C++ 默认关闭，由 profile 明确决定是否
在启动时请求 Validation Layer；Release 即使配置为开启，也只会在系统存在对应 Layer 和 Debug Utils 时生效。

`engine/src/graphics/vk_capability.h` 集中处理物理设备的队列、扩展、Surface、格式和 Feature
能力检查，以及 Swapchain Request 到最终 Config 的选择；设备按类型与可用能力评分，实际值限制在设备上限内。项目要求
Vulkan 1.3，启动时同时检查 loader 与 PhysicalDevice 版本。Vulkan format、color space 和 present mode 在 YAML 中使用
`bgra8_srgb`、`srgb_nonlinear`、`immediate` 等稳定名称，加载后保存为 Comet 强类型枚举。Queue 提交使用
Synchronization 2，每个 wait/signal 显式携带 semaphore value 和 pipeline stage；`ResourceUsage` 可解析为包含
stage、access、queue owner、layout 与 subresource range 的类型化资源状态，供 Barrier2、UploadManager 和
RenderGraph 复用。CommandBuffer 的显式 image/buffer transition 已使用 `ImageMemoryBarrier2`/
`BufferMemoryBarrier2` 和 `DependencyInfo`，Texture 上传不再传递 Vulkan layout pair。每个 Queue submission 额外 signal 自有 timeline semaphore 并返回单调
`GpuCompletionPoint`；FrameSlot 的 acquire/present 同步仍使用 binary semaphore，阻塞上传只等待自己的完成点。
ResourceManager 独占 UploadManager，Buffer/Image allocation 与内容上传分离；上传数据在可复用 staging page 内按偏移
子分配，pending batch 在 timeline completion 前独占对应 page、CommandContext 和目标资源，完成后整页回池；一个 Mesh
的 vertex/index copy 会合并为一次 Queue submission。空闲池只缓存有限数量的默认大小 page，超大 page 完成后直接
释放；池确实需要增长时才查询 heap budget，高压力下先释放全部空闲 page，并对连续压力只记录一次警告。Runtime
Mesh/Texture 创建不再执行 CPU wait，而是保存对应
`GpuCompletionPoint`；SceneRenderer 从实际 draw 资源生成 Queue wait，按 timeline 去重、合并最大值并在
VertexInput/FragmentShader stage 加入 frame submission。实际录制使用的 Runtime GPU owner 会随 frame completion
进入通用 GpuRetirementQueue，GPU 完成后才释放，避免热重载旧资源早于在途 draw 销毁。
FrameScheduler 以状态机约束 wait slot、成功 acquire 后 begin、记录 submission completion、推进 slot 的顺序，并提供
不随循环 slot 回绕的单调 frame serial。材质 descriptor cache 记录最后使用 serial，只在对应 frame 已完成后回收，
持续编辑或卸载材质不会让 DescriptorPool 和 Texture owner 永久累积。
设备存在 `VK_EXT_memory_budget` 时会将其作为可选能力启用，并为 VMA allocator 设置对应 flag；每帧使用单调 frame
serial 更新 VMA，而不是传递循环 FrameSlot 下标。Allocator/Device 还提供按需查询的 Comet heap budget snapshot，
包含 VMA block/allocation 统计和 usage/budget；扩展缺失时 snapshot 明确标记为估算值。
可恢复上传通过 `GpuResourceResult` 传递 Buffer/Image/staging 创建错误；任一 staging enqueue 失败会 abort 整个尚未提交的
显式 UploadBatch，不会把半套 copy 命令送入 Queue，也不会影响其他开放 batch；Batch submit 后才把 context、目标引用和 page
作为 pending ownership 交回 UploadManager。结果必须由显式 success/failure 工厂产生，不存在“默认未知失败”状态。
UploadManager 的可选 staging growth guard 在新 page 分配前提供无副作用拒绝点，生产默认关闭，测试用它确定性验证 batch
故障隔离。
Runtime Mesh 采用强失败 `create()` 与可恢复 `try_create()` 双轨静态工厂；vertex/index target 全部分配成功后才开始
enqueue，只有 batch flush 产生 ready completion 后才构造并发布 Mesh。Runtime Texture 采用相同事务边界，完整创建
Image/ImageView 并成功 enqueue 后才 flush 和发布。
Vulkan 内存分配由 `engine/src/graphics/resource/allocator.h`
封装，`Device` 独占持有 `Allocator`，`Buffer` 和 `Image` 通过 `AllocationUsage` 表达显存用途并以 `Allocation` 保存
VMA allocation 句柄；关键资源继续使用强失败的 `create_*`，非关键流送路径可显式选择返回 Vulkan 错误的
`try_create_*` 和 `within_budget`；Buffer/Image 工厂先取得完整 allocation 再构造包装对象，失败时不会发布持有空 handle
的对象；ImageView 同样在原生 view handle 创建成功后才构造持有父 Image 的 wrapper；per-frame `CPUBuffer` 使用 persistent mapping
和范围写入。Swapchain 根据实时 Surface capability
和 framebuffer 像素尺寸选择 extent、transform、alpha、usage 与 present mode，窗口最小化时暂停更新并延迟重建。
`engine/src/scene/`
提供基于 EnTT 的 Scene/Entity 和基础组件数据模型；`TransformComponent` 的 Euler 角使用度，局部矩阵顺序为
`T * Rz * Ry * Rx * S`。组件只存储 TRS 数据，并提供不引入额外状态的 `rotate()` 和 `to_matrix()` 值操作；
底层 `Math::wrap_degrees()` 与 `Math::compose_trs()` 会将旋转规范到 `[-180, 180)`，避免长期动画中的角度累积和
大数精度损失。运行时自增 `EntityId` 用于当前 Scene 内查询和 Selection；`UuidComponent` 保存 128-bit
version 4 `EntityUuid`，作为跨保存/加载稳定的实体身份，并支持标准字符串解析、格式化和哈希查询。
`RelationshipComponent` 只保存父实体 ID，子节点由 Scene 查询；Scene 阻止循环层级、递归销毁
子树，并在每次场景提取前按父级优先顺序全量更新 `WorldTransformComponent`。reparent 保持局部 TRS，世界矩阵随
新父节点重新计算；增量 dirty 更新留到 TransformSystem 建立后实现。
`ComponentRegistry` 使用显式 stable ID、pointer-to-member 访问器和属性标记描述 Transform、MeshRenderer 与 Camera；
editor 的 `PropertyEditorRegistry` 按 Bool、Float、Vec3 和 AssetHandle 类型分发控件，Inspector 不再包含组件专用分支。
editor 将同一个 `ComponentRegistry` 提供给 Inspector 和 `SceneSerializer`；serializer 按 stable ID、属性类型和
`serializable` 标记读写带版本字段的 `.scene` YAML，同时显式管理 Name、Entity UUID 与父 UUID 引用；
运行时 `EntityId`、EnTT handle 与派生世界矩阵不会落盘。实体按 UUID 排序，加载时会拒绝重复 UUID、悬空父引用、
层级环、未知字段和不支持的版本；保存时会自动创建缺失的父目录。
`SceneExtractor` 将可渲染实体和 Camera 复制为
`engine/src/render/scene/render_scene.h` 定义的 CPU 侧快照，其中只包含实体 ID、矩阵、Camera 参数和资源 Handle。
`Engine` 使用唯一所有权持有当前活动 Scene 和最小 Asset Registry，并可在保持唯一所有权的前提下交换 Scene。
app/editor 组合根持有 `AssetManager`，它使用 `AssetDatabase` 将稳定 Handle 解析为项目源资产；Mesh 由
`MeshImporter` 通过 fastgltf 将 `.gltf`/`.glb` 静态网格转换为 `MeshData`，Texture 由
`TextureImporter` 按 `.meta` 中经过校验的色彩空间和垂直翻转设置解码为 CPU 像素数据，Material 由
`MaterialSerializer` 读取为只含模板名和 Texture Handle 的
`MaterialData`；Asset Database 从 MaterialData 提取、去重 Texture Handle 依赖，并维护正向/反向索引。外部格式的导入器集中在 `engine/src/asset/import/`，Comet 原生资产与元数据的序列化器集中在
`engine/src/asset/serialization/`。`AssetManager` 递归解析材质依赖、组装运行时 Material 并统一发布到 `AssetRegistry`；
它只依赖由 `ResourceManager` 实现的窄 `RenderResourceFactory`；工厂返回预算受限的类型化 GPU 创建结果，AssetManager 只发布
完整候选，刷新失败时保留上一有效 Runtime Resource。`ResourceManager` 继续负责 Device 相关的 Texture/Mesh 创建和 Shader/Sampler 等渲染侧共享资源，不缓存资产 Handle。`render/resource/` 将这一资源族集中管理，其中 `TextureData`/`MeshData` 仍作为独立 CPU DTO，不与 Runtime Texture/Mesh 类定义混放；`MeshPrimitives` 直接生成 `MeshData`，Mesh 顶点布局不再属于通用数学模块。
Asset Database 扫描先构建候选快照再提交，并报告新增、删除和修改 Handle；Project 刷新据此同步 Runtime Registry 并以事件方式使 Inspector 缓存失效。Scene、Material 和 `.meta` 通过同一原子文本写入函数替换正式文件。Inspector 的 Material Texture 属性以及 Texture 色彩空间、垂直翻转设置只在值变化事件发生时自动保存并更新运行时对象，Texture 重新导入还会刷新已加载的直接 Material 依赖；更新结果统一显示在 Log 面板。
项目资产移动由 `AssetManager::move_asset()` 编排：目标必须是 `assets/` 内的相对路径且保持扩展名，source 与相邻 `.meta` 成对移动并复用同一 scan；目标冲突、路径越界、metadata 身份不匹配或快照无法提交时回滚文件，AssetHandle 不因重命名或移动改变。编辑器可在 Project 面板选中资产后通过 `Move / Rename` 输入 `assets/` 相对目标路径；成功后保持选中项并同步 Inspector、Log 和资产源监视状态。
已有资产的 `.meta` 暂时无法解析或声明类型与源扩展不匹配时，扫描会报告问题但合并该路径上一份有效记录、签名、revision 和依赖，不会把编辑中间态误报为删除；新资产没有可信旧身份时只报告问题。重复 GUID 会拒绝整次有歧义的候选快照并保留当前数据库，不再按路径任意选择一个资产。
Material 文档暂时无法解析时，自身文件变化仍推进 revision 并触发一次失败安全重载，但数据库会保留上一份有效 Texture Handle 依赖和反向索引，使旧 Runtime Material 留存期间仍能正确响应依赖刷新或删除。
Asset Database 另为每次已提交的资产变化分配单调 `AssetRevision`，与仅用于发现磁盘变化的文件签名分离；MeshImporter 报告的项目内、由 glTF 外部引用的 buffer 路径会随 Mesh 缓存持久化，并在加载时恢复为源路径正向/反向索引，因此修改 `.bin` 也会推进所属 Mesh revision。Engine 持有通用 `TaskScheduler`；已加载 Mesh/Texture 在 Project 刷新后由 Worker 执行缓存读取、CPU 导入或图片解码，旧资源在此期间继续可用，app/editor 每帧由 Owner Thread 消费完成结果。只有 revision 仍匹配的候选才会创建 Runtime Resource 并替换 Registry；Mesh 候选还会按需更新导入缓存和源依赖。启动必需资源以及 Inspector 的显式 Texture reimport 仍复用同步入口。
Mesh Worker 对主 glTF 和项目内外部 buffer 在导入前后捕获规范化内容指纹；cache hit 也恢复同一 `ImportInputSnapshot`。Texture Worker 复用相同类型捕获单一源图片。Owner Thread 在 cache、依赖登记、GPU 创建和 Registry replace 的适用边界复核，输入变化时主动推进 revision 并重调度，不会把旧 CPU Mesh/像素与新的文件签名或缓存记录组合。
Editor 通过引擎侧 `AssetSourceMonitor` 以 500ms 间隔观察 `assets/` 的路径、写入时间和大小，只有快照变化才调用同一个 AssetManager scan；目录暂时不可访问时保留上一份成功快照。Material/Texture Inspector 已经同步提交到数据库的自身写入会按精确路径确认，避免重复扫描；监视器不访问 ImGui、Registry 或 Vulkan，未来可在保持上层事件契约时替换为原生文件系统后端。
app/editor 的示例 cube mesh、材质及纹理均从项目资产链路按稳定 Handle 加载；app 创建
主 Camera 与两个具有不同 Transform 的 cube entity；`SceneResolver` 选择并校验主 Camera、根据渲染尺寸
生成 view/projection，同时将 Handle 解析为运行时 `RenderSubmission`。`Renderer` 负责编排帧流程，
`SceneRenderer` 管理 per-frame UBO、材质 descriptor，并通过
push constant 提交每个 draw 的模型矩阵。editor 初始化由 Engine 持有的 Scene；Hierarchy 可创建、递归删除、选择
实体，并以拖拽方式 reparent；Project 和 Hierarchy 共享 Entity/Asset 互斥的 Selection；Inspector 直接编辑 Name、
通过描述符编辑基础组件；Material 的 Texture Handle 属性在选择变化时自动持久化，并以候选对象替换运行时 Material。File 菜单通过路径输入执行 New/Open/Save；Open 先完整加载再替换 Engine 中的 Scene，
New/Open 都会重绑定 Hierarchy 与 Selection 并清除旧选择。editor 中的场景按 frame slot 渲染到可采样的离屏目标，再由 ImGui 合成到 swapchain；
Play 时通过 `SceneSerializer` 创建独立 Runtime Scene，并暂存原 Edit Scene；Stop 会丢弃运行时修改、恢复 Edit Scene，
重新绑定面板并清空 Selection，Play 期间禁用场景文件操作。editor 只保留一个 `ViewportPanel`：Edit 时显示编辑工具，
Play 时同一面板切换为运行画面并隐藏编辑工具。Viewport 通过纯值请求提交可见性、物理像素目标尺寸和 Camera 来源：Edit 使用不属于 Scene、不会序列化的 editor camera，Play 使用 Runtime Scene 的 primary Camera；SceneRenderer 不依赖 EditorMode。ViewPanel 通过可纯测试的布局计算分离 ImGui 逻辑内容区、结合当前平台 framebuffer scale 的 render resolution，以及保持当前纹理宽高比的屏幕坐标 image display rect。Play 工具栏支持 Free、16:9、1280×720、1920×1080 目标与 Fit/1x 显示；固定像素模式下 panel resize 只改变显示，不改变渲染分辨率。最终物理分辨率会结合设备 `maxImageDimension2D` 与 4096 编辑器软上限等比约束。离屏 resize 会先完整创建新的 MultiTarget generation，成功后切换，失败保留旧目标；旧 target 和 ImGui descriptor 分别按真实 submission completion 与 ready frame slot 延迟释放，正常 resize 不再等待整个 Device idle。RenderTarget 的 extent/frame count 在构造后只读，不再通过 dirty/recreate 原地改写。输入焦点仍由 Viewport/editor 交互层管理，等实际相机控制或游戏 Input System 接入时再形成消费契约，不提前进入 Renderer；
runtime app 仍直接渲染到 swapchain。swapchain 重建会先等待全部 graphics frame-slot fence，并在缺少 present completion 时只等待 present queue，再释放 runtime/ImGui framebuffer target；core handle、borrowed images、config 和 current index 先组成完整 `SwapchainGeneration` 候选，再提交 active shared owner。SwapchainTarget 显式共享对应 core，并按 extent/format/image-count compatibility diff 精确重建 editor backend；创建失败或窗口最小化延期时保留旧 core 并恢复 dependent，正常重建不再等待整个 Device idle。初始 RenderPass 使用 swapchain 实际选中的 surface format，而不是配置首选值。关闭时先释放引擎资源，
再关闭日志系统。Viewport 的鼠标坐标统一通过纯布局契约映射到当前实际纹理像素；工具栏、留白、最大边界和 1x 裁切不可见区域不会进入 camera/picking 输入，resize debounce 期间也不会误用尚未发布的请求分辨率。Edit 模式可在画面内以 RMB 环绕、MMB 平移、滚轮缩放 editor camera；2D/3D 会真正切换正交/透视投影，2D 固定观察轴并使用屏幕平移与正交缩放。左键点击只产生一次当前纹理 pixel 拾取请求，Renderer 复用当帧 RenderSubmission 和 Camera 做 CPU ray-local-AABB 最近命中并同步统一 Selection，空白点击清除选择；该路径不依赖 GPU readback，也不把 ImGui 下沉到引擎。Viewport 聚焦且未编辑文本时，`F` 只触发一次 Focus：Editor 组合根解析选中实体的 world transform 与 Runtime Mesh local bounds，通用 geometry 生成 world AABB，现有 camera controller 按 2D/3D 和纹理 aspect framing，不修改 Scene。ImGui 面板逻辑在 SceneResolver 前更新，当前帧直接使用新的 camera snapshot，draw data 仍在场景之后合成。Runtime Mesh 不保留完整 CPU 顶点副本，但会与 GPU buffer 一起保存可供拾取、Focus 和 culling 复用的局部 AABB。Shader
源文件位于 `engine/shaders/glsl/`，构建时由 CMake 调用 `glslangValidator`
编译。贡献者和智能体协作规范见 [AGENTS.md](./AGENTS.md)。

渲染资源的所有权、析构顺序和非拥有依赖约束见
[渲染资源所有权](./docs/architecture/rendering-ownership.md)；资产索引、导入与运行时发布边界见
[资产管线边界](./docs/architecture/asset-pipeline.md)；场景持久化契约见
[场景文件格式](./docs/architecture/scene-format.md)。
