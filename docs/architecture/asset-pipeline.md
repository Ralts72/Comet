# 资产管线边界

本文记录 Comet 当前阶段 3 资产链路的职责边界。当前已完成 Texture、Texture 基础导入设置与显式重新导入、Material 同步加载与
编辑保存，以及 glTF 静态 Mesh 的显式导入、Artifact-only Runtime 加载、Mesh/Texture 后台 CPU 热刷新和 `.comet/cache/` 二进制产物纵向切片，并建立事务式扫描快照、单调资产 revision、Owner Thread 候选发布校验、低频资产源自动监视、手动刷新一致性和原子文件写入；
Mesh 外部 buffer 的导入源依赖也已接入 Asset Database 精确失效。Texture 等其他类型的导入产物仍属于后续工作。

## 项目目录

```text
<ProjectRoot>/
├── assets/                # 源资产与相邻的 .meta，进入版本控制
├── .comet/                # 当前项目、当前机器的本地数据，不进入版本控制
│   ├── cache/             # 可重建导入产物与缓存
│   └── editor/imgui.ini   # 编辑器窗口与 Docking 布局
└── ProjectSettings/       # 项目设置，进入版本控制
```

仓库根目录当前同时作为 app/editor 的示例项目根目录。`assets/textures/`、`assets/materials/` 和
`assets/meshes/` 中的示例资产都带有已提交的 `.meta`，启动扫描不会重新生成其身份。Editor 自身的字体等私有资源位于 `editor/resources/`，不进入项目
`AssetDatabase`，也不生成 `.meta`。`.comet/editor/` 只保存当前用户/机器的编辑器状态，不属于编辑器内置资源。

当前 `.meta` v2 固定保存 `version`、`guid` 和 `type`。Texture 还必须保存类型化 `importer` 映射：
`color_space` 取 `srgb` 或 `linear`，`flip_y` 取布尔值；缺失字段、未知字段、未知枚举值以及资产类型与设置
不匹配都会使该资产拒绝进入索引。新建 Texture 资产会写入 `srgb`、不翻转的默认设置。

## 当前数据流

```text
ProjectPanel
    ↓ 只读 AssetRecord / 请求刷新
AssetDatabase
    ↓ AssetHandle → AssetRecord(type, relative path, validated import settings, dependencies)
    ↓ 每次已提交变化 → 单调 AssetRevision
    ↓ AssetHandle 依赖与 Importer 源路径依赖的正向/反向查询
ImportService（Editor/导入工具路径）
    ├── 当前 Artifact → 校验 Importer 版本和源输入快照
    └── 缺失/过期/损坏 → MeshImporter → MeshArtifact::publish_atomic()
AssetManager（Runtime 加载路径）
    ↓ 校验类型并创建候选 CPU 数据
    ├── MeshArtifact::load() → MeshData(vertices + indices)
    ├── TextureImporter(settings) → TextureData(RGBA CPU pixels + SRGB/UNORM format)
    └── MaterialSerializer → MaterialData(template + Texture Handle properties)
                               ↓ AssetManager 递归解析 Texture Handle
    ├── RenderResourceFactory → ResourceManager：MeshData/TextureData → Runtime Mesh/Texture
    └── Material：template + 已解析 Texture → Runtime Material
                               ↓ AssetManager 发布（后台候选需保持请求时 revision）
AssetRegistry
    ↓ 唯一按 AssetHandle 缓存并向 SceneResolver 提供运行时对象
```

已加载 Mesh/Texture 的 Project 刷新使用后台 CPU 路径：

```text
AssetManager::scan() 提交新 AssetRevision
    ↑ 顶层源文件/.meta 或已登记外部 buffer 发生变化
    → TaskScheduler Worker
        ├── MeshImporter → 内存 MeshArtifactCandidate
        └── TextureImporter → CPU TextureImportCandidate
    → 类型化的线程安全 completion queue
    → AssetManager::process_completions()（Owner Thread）
        → revision 验票
        → Mesh 有效时原子发布 Artifact 并更新源依赖
        → RenderResourceFactory 尝试创建 Runtime Mesh/Texture
        → 再次验票并替换 AssetRegistry
```

Worker 只接收路径、Handle、revision 和已校验 Importer 设置的值拷贝，不访问 `AssetDatabase`、`AssetRegistry` 或 Vulkan。刷新进行中、导入失败以及 Runtime Resource 创建失败时，Registry 中的旧 Mesh/Texture 都保持可用；相同 Handle 的连续变化可以同时完成，但只有当前 revision 会原子发布 Mesh Artifact、进入 GPU 创建并替换 Registry。

Importer 源依赖与 Material 的 AssetHandle 依赖是两套不同关系：前者表示“哪些项目文件参与生成这个资产”，后者表示“哪些资产在运行时引用另一个资产”。`MeshArtifact` 持久保存主 glTF 和外部 buffer 的项目相对路径与内容指纹；`ImportService` 用它判断 Artifact 是否需要重建，`AssetManager` 只从 Artifact 恢复依赖索引和 `MeshData`，不会读取或校验源文件。`.bin` 被视为 Importer 的辅助输入，不作为独立资产建立 Handle 或 `.meta`。

该索引在进程内由显式导入或 Artifact 加载结果恢复，而依赖路径持久化在可重建 Mesh Artifact 中。项目重新打开后，导入服务可以根据其中的输入快照跳过未变化的源资产；Runtime 加载只接受已发布 Artifact，缺失或损坏时明确失败并要求先执行导入。

Material 的编辑路径与加载路径复用同一格式契约：

```text
ProjectPanel → SelectionService(AssetHandle) → Inspector
    → Texture Handle 选择仅在值变化事件发生时提交 MaterialData
    → AssetManager::update_material(MaterialData)
    → 构建候选 Runtime Material → MaterialSerializer 序列化并原子替换 .mat
    → 更新 AssetDatabase 依赖索引 → 替换 AssetRegistry 条目
    → 任一步失败则 Inspector 恢复旧选择
```

Texture 的设置编辑与运行时创建也复用同一导入入口：

```text
ProjectPanel → SelectionService(AssetHandle) → Inspector
    → Checkbox/Selectable 仅在值变化事件发生时提交
    → AssetManager::reimport_texture(TextureImportSettings)
    → TextureImporter 构建候选 TextureData → ResourceManager 构建候选 Runtime Texture
    → AssetDatabase::update_import_settings() 保存 .meta 并更新索引
    → 替换 AssetRegistry Texture
    → AssetDatabase::get_dependents() 查询并刷新当前已加载的直接 Material
```

候选 Texture 的解码或 GPU 创建失败时不会修改 `.meta`、数据库索引或已发布对象。候选 Mesh 导入或 GPU 创建失败时同样保留上一份
已发布 Mesh；后台候选从数据库取得请求 revision，只有 revision 仍为当前版本才允许进入 GPU 创建和发布，过期候选会直接丢弃。`.meta` 保存成功后才发布候选对象；
当前单线程 Registry 保证经过类型预检的同类型替换不会竞争失败。

## 代码组织

`engine/src/asset/import/` 保存“外部格式 → Comet CPU 数据”的导入器、输入指纹和导入协调服务；
`engine/src/asset/artifact/` 保存面向 Runtime 的派生产物格式及其确定性读写；
`engine/src/asset/serialization/` 保存 Comet 自有资产格式和资产元数据的读写器。场景序列化与运行配置加载仍留在
各自的 `scene/` 和 `config/` 模块，因为它们不是 AssetManager 管理的资产格式。`TextureData`/`MeshData` 是与 GPU
对象分离的 CPU 创建数据，Importer 不需要包含 Runtime Texture/Mesh 类定义。相关 DTO、Runtime 对象、工厂接口和管理器统一位于 `engine/src/render/resource/`，而不是按“数据”单独建立宽泛目录。
Mesh 和 Texture 保持分立文件：它们的 CPU 数据、导入契约、Artifact 格式和 GPU 实现都不同；二者只在
`RenderResourceFactory` 这个“CPU 数据 → Runtime GPU 对象”边界统一，避免把无关类型塞进同一个聚合文件或大型分支工厂。

| 要追踪的流程 | 首要阅读入口 |
| --- | --- |
| Runtime 资产加载、缓存和刷新 | `asset/asset_manager.h` |
| 源 Mesh 导入和 Artifact 生成 | `asset/import/import_service.h` |
| Mesh Artifact 文件契约 | `asset/artifact/mesh_artifact.h` |
| CPU 数据到 GPU Mesh/Texture 的统一工厂边界 | `render/resource/resource_factory.h` |
| Vulkan 资源创建和设备级共享资源 | `render/resource/resource_manager.h` |

## 职责

- `AssetDatabase`：扫描 `assets/`，通过 `AssetMetadataSerializer` 校验/生成 `.meta`，维护 Handle、项目相对路径、已校验 Importer 设置以及两类正向/反向依赖索引；Material 的 AssetHandle 依赖由 `MaterialData` 提取，Importer 源依赖由成功导入或 Artifact 加载结果登记。缺失资产引用和错误类型进入扫描报告，`.bin` 等明确的导入辅助输入不单独建档。扫描先完整构建候选快照，再一次替换当前快照并报告新增、删除和修改 Handle；目录发现不完整，或同一 Handle 相对上一快照改变 AssetType 时拒绝提交，类型转换必须分配新 Handle。顶层源文件、`.meta` 和已登记的 Importer 输入签名共同负责检测变化，每次提交的资产变化会获得独立、单调的 `AssetRevision`，供结果发布时验票。Revision 是当前进程内的不透明版本，不持久化，也不承担内容哈希职责。设置更新先成功写入 `.meta`，再更新内存记录。
- `AssetMetadataSerializer`：只负责 `.meta` 的 YAML 读写和类型/设置契约校验；`AssetMetadata` 本身仍是独立于文件格式的数据类型。
- `AssetManager`：持有 Asset Database，并作为当前阶段的入口协调显式导入请求、依赖索引、运行时对象组装和 `AssetRegistry` 发布；Mesh Runtime 加载只读取 Artifact，源格式解析委托给 `ImportService`。Material 数据更新、显式重载、Texture 重新导入和 Mesh/Texture 扫描刷新都会先完整构建候选对象，失败时保留旧对象。已加载 Mesh/Texture 的扫描刷新向 TaskScheduler 提交纯 CPU 工作，由 `process_completions()` 在 Owner Thread 验票后原子发布 Mesh Artifact、更新依赖、尝试创建 Runtime Resource 并替换 Registry。Runtime Mesh/Texture 创建返回类型化 GPU 错误，首次加载失败不注册，刷新失败不替换上一有效对象。扫描提交后会卸载已删除资产及仍依赖它们的 Runtime 对象；资产身份和类型一致性由 AssetDatabase 在提交前保证。
- `AssetSourceMonitor`：低频观察 `assets/` 的项目相对路径、修改时间和大小，只负责判断文件树是否变化；目录暂时不可访问时保留上一基线。Editor 已知写入按精确路径确认，Monitor 不解析 metadata、不分配 Handle，也不直接启动 Importer。
- `TaskScheduler`：Engine 持有的通用固定 Worker 池，提供 FIFO 任务提交、Future、等待空闲和 drain-on-destruction；不认识资产类型、Registry 或 Vulkan。显式传入单 Worker 可让并发测试保持确定性。
- `ImportService`：Editor 和未来构建工具共用的源资产导入边界；检查 Mesh Artifact 的 Importer 版本与输入快照，必要时调用 `MeshImporter` 构建新的内存 Artifact，并提供对应的 Artifact 路径。调用方在 revision 验票后使用 `MeshArtifact::publish_atomic()` 发布；该服务不创建 GPU 对象。
- `MeshImporter`：唯一接触 glTF Mesh 格式的解析边界，使用 fastgltf 读取 `.gltf`/`.glb`，输出不包含 GPU 对象的 `MeshData` 并报告参与导入的外部 buffer 路径；fastgltf 类型不进入 Comet 公共头文件。
- `MeshArtifact`：Mesh 的版本化派生产物及其文件契约；静态 `load()` 校验资产 Handle、文件格式、顶点/索引数据和整体校验和，实例方法 `publish_atomic()` 通过临时文件原子发布自身。它不访问 glTF 或判断源文件是否变化。
- `TextureImporter`：唯一接触 Texture 源文件路径的解码边界，应用色彩空间和垂直翻转设置，输出不包含 GPU 对象的 `TextureData`。
- `MaterialSerializer`：确定性读写 Comet 原生 `.mat` 与不包含运行时对象的 `MaterialData`；Texture 属性只保存项目 `AssetHandle`。`get_asset_dependencies(MaterialData)` 负责生成排序、去重的依赖列表，供扫描和编辑更新共同复用。
- `Material`：运行时材质保存模板身份和已解析属性；不读取 `.mat`，当前渲染管线是否支持该模板由 `SceneResolver` 在提交边界判断。
- `RenderResourceFactory`：AssetManager 尝试创建 Runtime Texture/Mesh 所需的窄接口，返回 `GpuResourceResult`，不暴露 Shader/Sampler 等无关能力。
- `ResourceManager`：实现 `RenderResourceFactory`，对资产创建使用受 memory budget 约束的 try path，并维护 Shader/Sampler 等设备级共享资源；不认识 `AssetHandle`、`MaterialData`、`.meta` 或源文件路径。
- `AssetRegistry`：作为唯一的 Handle 缓存，保存已发布运行时对象的带类型共享引用，并允许同类型候选对象替换；Scene 中仍只保存 Handle。
- `ProjectPanel`：显示 Asset Database 的快照和扫描问题，并向共享 Selection 发布 Asset Handle；不自己访问文件系统或创建 GPU 资源。
- `Inspector`：根据共享 Selection 显示 Entity 或 Asset；Material 编辑器只允许从已索引 Texture 中选择属性，模板身份仍只读；Material 和 Texture 控件都只在值变化事件发生时自动提交，失败时恢复旧值。更新成功或失败统一写入 Logger 并由 Log 面板展示，Inspector 只显示当前资产的加载或字段校验错误。

## 生命周期

`AssetManager` 持有 `AssetDatabase` 和对 `AssetRegistry`/`RenderResourceFactory`/`TaskScheduler` 的非拥有引用，因此 app/editor 必须在
`Engine` 销毁前释放它；析构时会等待自己已提交的任务完成，再释放 completion state。`AssetRegistry` 是按 Handle 保存运行时资产的唯一生命周期根；Material 等运行时对象可以
通过共享引用保持其 Texture 依赖。`Engine` 关闭时先等待 TaskScheduler idle，再等待 Device idle、清理 Registry，最后由 Renderer 释放
ResourceManager 和 Device 级共享资源。

Material/Texture/Mesh 替换只交换 Registry 中的 `shared_ptr`，已取得旧对象的当前帧和 descriptor frame slot 仍可自然持有到结束；
当前 `AssetRevision` 与 completion queue 解决 Mesh/Texture CPU 候选的版本一致性；`AssetSourceMonitor` 只负责触发扫描，二者都不替代异步 GPU completion 和 GPU retirement 机制。

Project 手动刷新或 `AssetSourceMonitor` 发现变化后都复用同一个 `AssetManager::scan()`：成功提交快照后，会通过变化集清理删除资产及其依赖对象、刷新已加载的修改资产，并以事件方式使 Inspector
丢弃同一 Handle 的旧编辑缓存；扫描目录暂时不可访问时三者继续使用上一份有效快照。更通用的递归 AssetHandle 依赖 revision、原生文件事件后端和 GPU retirement 仍属于后续工作。

Scene、Material 和 `.meta` 使用同一原子文本写入函数：先在目标目录写完临时文件，再原子替换正式文件，避免直接截断造成半写文件。
Mesh Artifact 复用同一临时文件替换机制写入二进制数据；发布失败意味着本次导入没有完成，旧 Artifact 和旧 Runtime Mesh 继续有效。
`.comet/cache/` 可以整体删除并通过导入流程重建，不能成为资产身份或源资产的唯一副本；删除后 Runtime 加载不会隐式解析源文件。
`.comet/editor/` 同样不影响项目内容，但删除后会丢失当前机器的窗口与 Docking 布局；需要团队共享的设置仍必须进入
`ProjectSettings/`。

`filter`/`wrap` 由运行时 Sampler 消费，mipmap 需要 Image mip-level 和上传链路共同支持，因此没有作为只落盘但
不生效的字段提前加入 Texture Importer 契约。

当前 Mesh Importer 的有意范围是：一个源文件对应一个 glTF mesh，合并该 mesh 内的多个 triangle-list primitive；
`POSITION` 必须存在，缺少 `NORMAL` 时按三角面生成平滑法线，缺少 `TEXCOORD_0` 时填零。glTF material、node transform、
animation、skin、morph target 和多 mesh 子资产拆分尚未进入 Comet 资产契约，因此本阶段不会隐式导入这些内容。
