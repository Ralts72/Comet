# 资产管线边界

本文记录 Comet 当前阶段 3 资产链路的职责边界。当前只完成 Texture 的同步纵向切片，Material、Mesh、后台导入、
`Library/` 导入产物和热重载仍属于后续工作。

## 项目目录

```text
<ProjectRoot>/
├── assets/           # 源资产与相邻的 .meta，进入版本控制
├── Library/          # 可重建导入产物与缓存，不进入版本控制
└── ProjectSettings/  # 项目设置，进入版本控制
```

仓库根目录当前同时作为 app/editor 的示例项目根目录。`assets/textures/` 中的示例纹理带有已提交的
`.meta`，启动扫描不会重新生成其身份。Editor 自身的字体等私有资源位于 `editor/resources/`，不进入项目
`AssetDatabase`，也不生成 `.meta`。

## 当前数据流

```text
ProjectPanel
    ↓ 只读 AssetRecord / 请求刷新
AssetDatabase
    ↓ AssetHandle → AssetRecord(type, relative path)
AssetManager
    ↓ 校验类型并调用 Importer
TextureImporter
    ↓ TextureData(RGBA CPU pixels)
ResourceManager
    ↓ 按 AssetHandle 创建或复用 GPU Texture
AssetRegistry
    ↓ 按 AssetHandle 向 SceneResolver/Material 提供运行时对象
```

## 职责

- `AssetDatabase`：扫描 `assets/`，校验/生成 `.meta`，维护 Handle 与项目相对路径的双向索引。
- `AssetManager`：协调数据库、Importer、`ResourceManager` 和 `AssetRegistry`；当前由 app/editor 组合根持有。
- `TextureImporter`：唯一接触 Texture 源文件路径的解码边界，输出不包含 GPU 对象的 `TextureData`。
- `ResourceManager`：只消费 `AssetHandle + TextureData`，按 Handle 缓存 GPU Texture；不扫描目录、不解析 `.meta`、不解码文件。
- `AssetRegistry`：保存 Handle 到已发布运行时对象的带类型共享引用，Scene 中仍只保存 Handle。
- `ProjectPanel`：显示 Asset Database 的快照和扫描问题；不自己访问文件系统或创建 GPU 资源。

## 生命周期

`AssetManager` 持有 `AssetDatabase` 和对 `AssetRegistry`/`ResourceManager` 的非拥有引用，因此 app/editor 必须在
`Engine` 销毁前释放它。Runtime Texture 由 `AssetRegistry` 与 `ResourceManager` 缓存共同延长生命周期，
`Engine` 关闭时先清理 Registry，再由 Renderer 释放 ResourceManager 和 GPU 资源。

当前刷新只重建源资产索引，不会自动卸载、重导入或替换已发布的运行时资源。这些行为需要后续的
revision、completion token 和 GPU retirement 机制支持。
