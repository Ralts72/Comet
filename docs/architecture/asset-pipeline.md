# 资产管线边界

本文记录 Comet 当前阶段 3 资产链路的职责边界。当前已完成 Texture 和 Material 的同步纵向切片；Mesh、后台导入、
`Library/` 导入产物、材质编辑保存和热重载仍属于后续工作。

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

## 当前数据流

```text
ProjectPanel
    ↓ 只读 AssetRecord / 请求刷新
AssetDatabase
    ↓ AssetHandle → AssetRecord(type, relative path)
AssetManager
    ↓ 校验类型并调用 Importer
    ├── TextureImporter → TextureData(RGBA CPU pixels)
    └── MaterialSerializer → MaterialData(template + Texture Handle properties)
                               ↓ AssetManager 递归解析 Texture Handle
    ├── ResourceManager：TextureData → Runtime Texture
    └── Material：template + 已解析 Texture → Runtime Material
                               ↓ AssetManager 发布
AssetRegistry
    ↓ 唯一按 AssetHandle 缓存并向 SceneResolver 提供运行时对象
```

## 代码组织

`engine/src/asset/import/` 保存“外部格式 → Comet CPU 数据”的导入器；
`engine/src/asset/serialization/` 保存 Comet 自有资产格式和资产元数据的读写器。场景序列化与运行配置加载仍留在
各自的 `scene/` 和 `common/` 模块，因为它们不是 AssetManager 管理的资产格式。

## 职责

- `AssetDatabase`：扫描 `assets/`，通过 `AssetMetadataSerializer` 校验/生成 `.meta`，维护 Handle 与项目相对路径的双向索引。
- `AssetMetadataSerializer`：只负责 `.meta` 的 YAML 读写；`AssetMetadata` 本身仍是独立于文件格式的数据类型。
- `AssetManager`：协调数据库、Importer、依赖解析、运行时对象组装和 `AssetRegistry` 发布；当前由 app/editor 组合根持有。
- `TextureImporter`：唯一接触 Texture 源文件路径的解码边界，输出不包含 GPU 对象的 `TextureData`。
- `MaterialSerializer`：读取 Comet 原生 `.mat` 为不包含运行时对象的 `MaterialData`；Texture 属性只保存项目 `AssetHandle`，后续材质编辑保存复用同一格式契约。
- `Material`：运行时材质保存模板身份和已解析属性；不读取 `.mat`，当前渲染管线是否支持该模板由 `SceneResolver` 在提交边界判断。
- `ResourceManager`：使用 Device 从 CPU 数据创建 Texture/Mesh，并维护 Shader/Sampler 等设备级共享资源；不认识 `AssetHandle`、`MaterialData`、`.meta` 或源文件路径。
- `AssetRegistry`：作为唯一的 Handle 缓存，保存已发布运行时对象的带类型共享引用；Scene 中仍只保存 Handle。
- `ProjectPanel`：显示 Asset Database 的快照和扫描问题；不自己访问文件系统或创建 GPU 资源。

## 生命周期

`AssetManager` 持有 `AssetDatabase` 和对 `AssetRegistry`/`ResourceManager` 的非拥有引用，因此 app/editor 必须在
`Engine` 销毁前释放它。`AssetRegistry` 是按 Handle 保存运行时资产的唯一生命周期根；Material 等运行时对象可以
通过共享引用保持其 Texture 依赖。`Engine` 关闭时先等待 Device idle，再清理 Registry，最后由 Renderer 释放
ResourceManager 和 Device 级共享资源。

当前刷新只重建源资产索引，不会自动卸载、重导入或替换已发布的运行时资源。这些行为需要后续的
revision、completion token 和 GPU retirement 机制支持。
