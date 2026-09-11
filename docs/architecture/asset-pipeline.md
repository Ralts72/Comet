# 资产管线

本文描述当前实现；未落地的扩展统一放在[路线图](../engine-roadmap.md)。

## 目录和身份

- `assets/`：项目源资产和相邻 `.meta`，进入版本控制。
- `.comet/cache/`：可重建导入产物；`.comet/editor/imgui.ini`：本机编辑器布局。两者不提交。
- `editor/resources/`：编辑器私有字体等资源，不进入 AssetDatabase，不生成 .meta。
- `ProjectSettings/`：后续项目设置的预留约定，当前未建立完整项目 manifest。

当前 .meta v2 保存 version/guid/type；Texture 另有 importer.color_space（srgb/linear）和 flip_y。
未知字段、缺失字段或不匹配的类型/设置会被拒绝。新 Texture 默认为 srgb、不翻转。
Handle 可随源文件和 .meta 一起移动，但同一 Handle 不可改变 AssetType；类型转换需要新身份。

## 按问题找入口

以下路径均相对 `engine/src/`。

| 入口 | 负责 | 不负责 |
| --- | --- | --- |
| `asset/asset_manager.h` / AssetManager | 协调加载、重载、导入与发布 | 自己解析 glTF、持有 Device |
| `asset/database.h` / AssetDatabase | 扫描、身份、revision、依赖索引 | GPU 对象缓存 |
| `asset/registry.h` / AssetRegistry | 唯一 Handle → Runtime 对象缓存 | 路径、文件解析 |
| `asset/import/import_service.h` / ImportService | 检查输入、准备 Mesh Artifact | GPU 创建 |
| `asset/import/*_importer.h` | 外部格式 → Comet CPU 数据 | 资产发布策略 |
| `asset/artifact/mesh_artifact.h` | 版本化 Mesh 产物读写与原子发布 | 读取源 glTF 判断过期 |
| `asset/serialization/` | .meta/.mat 的严格读写 | 渲染绑定 |
| `asset/source_operations.h` | source + sidecar 移动、校验、回滚 | 格式解析 |
| `asset/source_monitor.h` | 低频观察源文件树变化 | 分配身份、启动 Importer |
| `render/resource/resource_factory.h` / RenderResourceFactory | 窄的 CPU → Runtime Mesh/Texture 创建接口 | 材质组装、身份与缓存 |
| `render/resource/resource_manager.h` / ResourceManager | 实现工厂，管理上传和 Shader/Sampler 共享资源 | AssetHandle 与 .meta |

Mesh/Texture 的 CPU DTO、Runtime 对象和工厂集中于 render/resource，但仍按类型分文件：
二者的导入、GPU 布局和产物契约不同，不为减少文件数合并为大分支工厂。
MaterialData 是可序列化的 template + Texture Handle 参数；当前 Runtime Material 保存解析后的 Texture 引用。
Scene Serializer 和 ConfigLoader 留在各自模块，不强行纳入 AssetManager。

## 三种加载路径

```text
源 glTF/.glb + 外部 buffer
  → ImportService → MeshImporter → MeshArtifact::publish_atomic()
  → AssetManager::load_mesh() → MeshArtifact::load() → MeshData
  → RenderResourceFactory → Runtime Mesh → AssetRegistry

Texture 源文件 + TextureImportSettings
  → AssetManager::load_texture() → TextureImporter → TextureData
  → RenderResourceFactory → Runtime Texture → AssetRegistry

原生 .mat
  → AssetManager::load_material() → MaterialSerializer → MaterialData
  → 解析 Texture Handles → Runtime Material → AssetRegistry
```

- Mesh Runtime 只读已发布 Artifact，不校验源文件、不回退解析 glTF。缓存缺失/损坏需先导入。
- MeshArtifact 保存源路径/内容指纹，ImportService 据此判断重建；.bin 是辅助输入，不单独生成 Handle/.meta。
- Texture 仍直接解码源文件，TextureArtifact 后置；不要把当前链路误读为所有资产均有 Artifact。
- MeshImporter 当前只支持一个 glTF mesh，合并 triangle-list primitives；POSITION 必需，
  缺 NORMAL 生成平滑法线，缺 TEXCOORD_0 填零。node transform/material/animation/skin/morph/多 mesh 子资产尚未导入。
- fastgltf 类型不进入 Comet 公共头文件。

## 扫描、后台刷新与发布

```text
Project Refresh / AssetSourceMonitor
  → AssetManager::scan() → AssetDatabase 候选快照
  → 提交变化集与单调 revision
  → 已加载 Mesh/Texture：Worker 生成 CPU 候选
  → process_completions()：owner 验 revision
  → Mesh 原子发布 Artifact / 更新源依赖
  → 尝试创建 Runtime GPU 对象 → 再验票 → 替换 Registry
```

扫描区分单文件诊断与无法信任的全局快照：坏 .meta 的条目不会凭空沿用旧 AssetRecord；
目录发现不完整或同一 Handle 改变类型时拒绝整个候选快照。revision 是进程内版本，不持久化，也不是内容 hash。
删除会卸载对应 Runtime 对象及受影响依赖；已加载 Material 的修改走同步重载。

Worker 只接收路径、Handle、revision、导入设置的值拷贝，不访问数据库、Registry、ImGui 或 Vulkan。
过期候选丢弃；解码/GPU 创建失败不替换旧 Runtime 对象。Mesh Artifact 与 Runtime 发布是两个边界：
Artifact 已成功发布后若 GPU 创建失败，旧 Runtime Mesh 仍保留，磁盘产物可以已更新。

EditorAssets 在成功提交扫描快照后收集 Mesh Handles，下一次 update 通过 `import_mesh_async(IfNeeded)`
提交后台检查／导入，不依赖选择或 UI 按钮。有效 Artifact 复用且不重写；缺失、损坏或过期时重建。
首次扫描只记录请求，避免与启动阶段同步准备关键资源竞争；同步准备成功后移除对应待处理请求。
Project 右键 Reimport 走 Force 模式；同 Handle + revision 请求合并，自动检查期间的强制重建意图不会丢失。
未加载模型只发布 Artifact 和源依赖，不分配 GPU；已加载模型继续安全替换 Runtime，失败保留旧对象。
扫描事件后会检查项目内所有已索引 Mesh，也覆盖尚未成功导入、未登记外部 buffer 依赖的模型；
无事件帧不遍历或检查产物，失败不会每帧自动重试。大项目的检查范围和任务预算仍需后续优化。

依赖索引分两类：

- AssetHandle 依赖：例如 Material → Texture。
- Importer 源路径依赖：例如 Mesh → 外部 buffer；由成功导入或 Artifact 加载恢复。

数据库依赖查询返回借用 span，修改数据库后不可保留或继续遍历。
Texture 后台刷新和显式重导入共用 `reload_loaded_material_dependents()`：
先复制直接依赖 Handle，再重载已加载 Material；重载会重建依赖索引，因此不能直接迭代原始 span。
不主动加载尚未使用的材质；失败的材质保留旧 Runtime 对象与旧 Texture 引用。

## 编辑与移动

- Material：Inspector 值变化 → update_material → 构建候选 → 原子保存 .mat → 更新依赖 → 替换 Registry。
- Texture：设置变化 → reimport_texture → 解码/GPU 候选 → 保存 .meta → 发布 Texture → 刷新已加载材质。
- 控件按变化事件提交，不逐帧保存；失败恢复旧控件值。加载/字段错误显示在 Inspector，更新日志只进入 Log。
- 移动：Project 提交 Handle 和项目相对目标 → 校验边界/扩展名/身份 → 成对移动 source/.meta →
  候选数据库扫描 → 可信则提交，否则补偿回滚。两个文件不能获得单次 OS 原子 rename；
  回滚自身失败必须报告具体诊断，不声称成功。普通 Mesh 移动不等于重写 glTF 外部 URI。
- 成功后保留 Selection，按事件失效 Inspector 缓存，并向 Monitor 确认精确变动路径，避免再次识别自身写入。

## 生命周期与文件写入

AssetManager 持有数据库，借用 Registry、RenderResourceFactory 和 TaskScheduler；必须先于这些依赖销毁，
并在析构时等自己的任务结束。TaskScheduler 是通用固定 Worker 池，不认识资产。
Registry 保存 Runtime 的共享引用；替换条目不影响仍持有旧对象的 Material 或在途帧。
GPU ready/retention 规则见[渲染所有权](rendering-ownership.md)，资产 revision 不代替 GPU completion。

Scene/.mat/.meta 的文本保存和 MeshArtifact 二进制发布共用临时文件替换机制，不直接截断正式文件。
缓存可以删除并重新导入，但不是身份或源文件的唯一副本；删除 editor 本地状态会丢失布局，不影响项目内容。
Sampler filter/wrap、mipmap 和格式迁移等未实现能力只在路线图维护。
