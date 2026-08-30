# 资产管线边界

本文记录 Comet 当前阶段 3 资产链路的职责边界。当前已完成 Texture、Texture 基础导入设置与显式重新导入、Material 同步加载和
Material 编辑保存的纵向切片；Mesh、后台导入、`Library/` 导入产物和文件监听热重载仍属于后续工作。

## 项目目录

```text
<ProjectRoot>/
├── assets/           # 源资产与相邻的 .meta，进入版本控制
├── Library/          # 可重建导入产物与缓存，不进入版本控制
└── ProjectSettings/  # 项目设置，进入版本控制
```

仓库根目录当前同时作为 app/editor 的示例项目根目录。`assets/textures/` 中的示例纹理和
`assets/materials/` 中的示例材质都带有已提交的 `.meta`，启动扫描不会重新生成其身份。Editor 自身的字体等私有资源位于 `editor/resources/`，不进入项目
`AssetDatabase`，也不生成 `.meta`。

当前 `.meta` v2 固定保存 `version`、`guid` 和 `type`。Texture 还必须保存类型化 `importer` 映射：
`color_space` 取 `srgb` 或 `linear`，`flip_y` 取布尔值；缺失字段、未知字段、未知枚举值以及资产类型与设置
不匹配都会使该资产拒绝进入索引。新建 Texture 资产会写入 `srgb`、不翻转的默认设置。

## 当前数据流

```text
ProjectPanel
    ↓ 只读 AssetRecord / 请求刷新
AssetDatabase
    ↓ AssetHandle → AssetRecord(type, relative path, validated import settings)
AssetManager
    ↓ 校验类型并调用 Importer
    ├── TextureImporter(settings) → TextureData(RGBA CPU pixels + SRGB/UNORM format)
    └── MaterialSerializer → MaterialData(template + Texture Handle properties)
                               ↓ AssetManager 递归解析 Texture Handle
    ├── ResourceManager：TextureData → Runtime Texture
    └── Material：template + 已解析 Texture → Runtime Material
                               ↓ AssetManager 发布
AssetRegistry
    ↓ 唯一按 AssetHandle 缓存并向 SceneResolver 提供运行时对象
```

Material 的编辑路径与加载路径复用同一格式契约：

```text
ProjectPanel → SelectionService(AssetHandle) → Inspector
    → Texture Handle 选择仅在值变化事件发生时提交 MaterialData
    → AssetManager::update_material(MaterialData)
    → 构建候选 Runtime Material → MaterialSerializer::save(.mat)
    → 替换 AssetRegistry 条目；任一步失败则 Inspector 恢复旧选择
```

Texture 的设置编辑与运行时创建也复用同一导入入口：

```text
ProjectPanel → SelectionService(AssetHandle) → Inspector
    → Checkbox/Selectable 仅在值变化事件发生时提交
    → AssetManager::reimport_texture(TextureImportSettings)
    → TextureImporter 构建候选 TextureData → ResourceManager 构建候选 Runtime Texture
    → AssetDatabase::update_import_settings() 保存 .meta 并更新索引
    → 替换 AssetRegistry Texture，并刷新当前已加载且引用旧 Texture 的 Material
```

候选 Texture 的解码或 GPU 创建失败时不会修改 `.meta`、数据库索引或已发布对象。`.meta` 保存成功后才发布候选对象；
当前单线程 Registry 保证经过类型预检的同类型替换不会竞争失败。

## 代码组织

`engine/src/asset/import/` 保存“外部格式 → Comet CPU 数据”的导入器；
`engine/src/asset/serialization/` 保存 Comet 自有资产格式和资产元数据的读写器。场景序列化与运行配置加载仍留在
各自的 `scene/` 和 `common/` 模块，因为它们不是 AssetManager 管理的资产格式。

## 职责

- `AssetDatabase`：扫描 `assets/`，通过 `AssetMetadataSerializer` 校验/生成 `.meta`，维护 Handle、项目相对路径和已校验 Importer 设置的索引；设置更新先成功写入 `.meta`，再更新内存记录。
- `AssetMetadataSerializer`：只负责 `.meta` 的 YAML 读写和类型/设置契约校验；`AssetMetadata` 本身仍是独立于文件格式的数据类型。
- `AssetManager`：协调数据库、Importer、依赖解析、运行时对象组装和 `AssetRegistry` 发布；Material 数据更新、显式重载和 Texture 重新导入都会先完整构建候选对象，失败时保留旧对象。Texture 替换后会刷新当前已加载且直接引用旧对象的 Material。
- `TextureImporter`：唯一接触 Texture 源文件路径的解码边界，应用色彩空间和垂直翻转设置，输出不包含 GPU 对象的 `TextureData`。
- `MaterialSerializer`：确定性读写 Comet 原生 `.mat` 与不包含运行时对象的 `MaterialData`；Texture 属性只保存项目 `AssetHandle`。
- `Material`：运行时材质保存模板身份和已解析属性；不读取 `.mat`，当前渲染管线是否支持该模板由 `SceneResolver` 在提交边界判断。
- `ResourceManager`：使用 Device 从 CPU 数据创建 Texture/Mesh，并维护 Shader/Sampler 等设备级共享资源；不认识 `AssetHandle`、`MaterialData`、`.meta` 或源文件路径。
- `AssetRegistry`：作为唯一的 Handle 缓存，保存已发布运行时对象的带类型共享引用，并允许同类型候选对象替换；Scene 中仍只保存 Handle。
- `ProjectPanel`：显示 Asset Database 的快照和扫描问题，并向共享 Selection 发布 Asset Handle；不自己访问文件系统或创建 GPU 资源。
- `Inspector`：根据共享 Selection 显示 Entity 或 Asset；Material 编辑器只允许从已索引 Texture 中选择属性，模板身份仍只读；Material 和 Texture 控件都只在值变化事件发生时自动提交，失败时恢复旧值。更新成功或失败统一写入 Logger 并由 Log 面板展示，Inspector 只显示当前资产的加载或字段校验错误。

## 生命周期

`AssetManager` 持有 `AssetDatabase` 和对 `AssetRegistry`/`ResourceManager` 的非拥有引用，因此 app/editor 必须在
`Engine` 销毁前释放它。`AssetRegistry` 是按 Handle 保存运行时资产的唯一生命周期根；Material 等运行时对象可以
通过共享引用保持其 Texture 依赖。`Engine` 关闭时先等待 Device idle，再清理 Registry，最后由 Renderer 释放
ResourceManager 和 Device 级共享资源。

Material/Texture 替换只交换 Registry 中的 `shared_ptr`，已取得旧对象的当前帧和 descriptor frame slot 仍可自然持有到结束；
这条同步、用户触发的编辑器路径不等同于文件监听热重载，也不替代后续的 revision、completion 和 retirement 机制。

Project 刷新仍只重建源资产索引，不会自动卸载、重导入或替换已发布的运行时资源。自动失效传播需要后续的
依赖图、revision、completion token 和 GPU retirement 机制支持；当前 Texture 显式重新导入只刷新已加载的直接 Material 依赖。

`filter`/`wrap` 由运行时 Sampler 消费，mipmap 需要 Image mip-level 和上传链路共同支持，因此没有作为只落盘但
不生效的字段提前加入 Texture Importer 契约。
