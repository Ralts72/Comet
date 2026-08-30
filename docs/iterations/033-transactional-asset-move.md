# 033 资产移动/重命名事务

## 目标

为项目操作层建立可复用的资产移动入口：源文件和相邻 `.meta` 必须成对移动，持久 AssetHandle 保持不变；目标冲突、路径越界、
metadata 不可信、文件系统失败或移动后数据库快照无法提交时，不得让文件和内存索引停在半完成状态。

本步先完成 Engine/AssetManager 后端和自动化测试，Project 面板的 Rename/Move 交互留到下一步；UI 不直接调用
`std::filesystem::rename()`。

## 为什么由 AssetManager 编排

职责分为三层：

```text
Project UI / future CLI
  -> 提供 Handle + assets-relative destination

AssetManager::move_asset
  -> 校验当前 AssetRecord 与 sidecar 身份
  -> 执行 source/.meta 文件事务
  -> 调用同一个 AssetManager::scan
  -> 失败回滚文件，成功沿变化集刷新 Runtime

AssetDatabase
  -> 仍只通过候选快照建立 Handle/path/revision/dependency 索引
```

ResourceManager 不参与，因为移动不创建 Device 资源，也不改变 AssetHandle -> Runtime Resource 缓存职责。ProjectPanel 不持有
ProjectPaths 或文件事务状态。当前实现保留在 AssetManager 实现文件；等创建、复制、删除等操作出现并共享足够规则后，再提取
独立 AssetSourceOperations 服务，避免为单一操作提前增加公共类。

## 前置校验

移动前验证：

- Handle 非零且已索引；
- destination 是规范化后仍位于 `assets/` 内的项目相对文件路径，拒绝绝对路径、`..` 和原子写临时文件名；
- source 与 destination 不同，且不允许通过 Move 改变扩展名；格式转换应由 Importer/转换命令完成；
- source 是普通文件，destination 及其 sidecar 都不存在；
- source sidecar 可完整解析，且其中 Handle/AssetType 与当前数据库记录一致；last-known-good 的损坏 metadata 资产必须先修复；
- destination parent 经 canonical 解析仍在 canonical assets root 内，防止 symlink 或路径组合逃逸。

检查通过后才创建缺失目录。代码记录本次创建的目录，回滚时只删除这些目录且仅在为空时成功，不会删除已有目录或用户文件。

## 双文件事务与扫描提交

文件操作顺序：

```text
rename source -> target
  -> 失败：清理本次空目录，返回
rename source.meta -> target.meta
  -> 失败：target -> source，清理目录，返回
AssetManager::scan()
  -> snapshot_updated：提交移动
  -> 未提交/抛异常：target.meta -> source.meta
                     target -> source
                     清理目录
                     返回原扫描诊断 + rollback 状态
```

同一文件系统内 rename 保持单文件原子性；两个文件无法获得 OS 级整体原子 rename，因此显式补偿回滚是当前跨平台契约。如果回滚
自身失败，报告会列出 source/metadata 哪一步失败，不会谎称数据库已同步。

移动后仍复用完整 scan，而不是直接修改 `m_handles_by_path`：Database 会通过 sidecar 中相同 Handle 识别这是 modified 而不是
removed + added，并统一重建 Material 依赖、Mesh Importer 输入路径、source signature 和 revision。AssetManager 再按已有策略刷新
已经加载的同类型 Runtime Resource。

## Identity 与 Runtime 行为

`.meta` 随源文件移动，因此：

```text
before: materials/test.mat -> handle 42
after:  renamed/moved.mat -> handle 42
```

Scene/Material 中保存的 Handle 无需修改。Material 等已加载对象会按 modified 路径重建并替换；Mesh/Texture 进入现有后台 candidate
链，旧 Runtime 对象在新路径导入成功前保持。

移动后若项目已有另一个相同 GUID，Database 按第 029 步拒绝有歧义快照；move transaction 随即把 source 和 sidecar 恢复原位，
数据库与 Registry 都保持移动前状态。

## 验证

- 正常移动 Material 到新目录：source 和旧 sidecar 消失、目标两文件存在、sidecar Handle 不变；
- scan 报告同一 Handle modified，数据库路径更新，已加载 Material 通过既有刷新链替换；
- destination 已存在时不移动任一文件；
- `../outside.mat` 等越界 destination 被拒绝，assets 外不产生文件；
- 移动后扫描发现重复 GUID 时 snapshot 不提交，source/sidecar 与本次创建目录全部回滚；
- 完整 Debug 构建与 CTest。

## 限制与下一步

- 当前不支持 case-insensitive 文件系统上的纯大小写重命名；可在后续使用同目录临时中转名实现。
- `.gltf` 移到不同目录可能使其内部相对 `.bin` URI 失效。本接口保证 Comet source/sidecar 的身份事务，不擅自解析并重写第三方
  格式内容；Importer 失败时旧 Runtime Mesh 保持，日志指出问题。后续可增加“连同 Importer 输入一起移动”的显式高级命令。
- 当前只移动单个普通文件资产，不移动目录树。

下一步在 Project 面板接入 Rename/Move 对话框：以选中 AssetHandle 调用 `AssetManager::move_asset()`，统一消费返回的 scan report，
刷新 Project/Inspector/Selection，并让 AssetSourceMonitor 精确确认旧/新 source 与 sidecar，避免下一轮轮询重复扫描。
