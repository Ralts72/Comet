# 028 资产管线阶段性架构复盘

## 范围

本次复盘覆盖当前阶段 3 已形成的完整刷新链，而不是单看某个类：

```text
AssetSourceMonitor
  -> AssetDatabase candidate snapshot / source signature / AssetRevision
  -> AssetManager pending task / TaskScheduler
  -> Mesh or Texture CPU candidate / MeshImportCache
  -> Owner Thread revision validation
  -> RenderResourceFactory recoverable GPU creation
  -> AssetRegistry replace
  -> dependent Material reload
  -> in-flight GPU owner retirement
```

目标是判断最近新增的状态和类型是否真的表达不同职责，找出失败安全、并发和类型一致性上的缺口，并清理已经与实现不符的
路线图描述。本步不为了减少文件数合并职责明确的类，也不引入新的通用框架。

## 保留的分层

### Monitor 快照与 Database 签名不合并

两者都包含文件状态，但不是重复缓存：

- `AssetSourceMonitor` 保存整个 `assets/` 文件树的粗粒度状态，只回答“是否值得 scan”，不认识 AssetHandle、Importer 或依赖；
- `AssetDatabase` 保存每个资产的源文件、`.meta` 和已知 Importer 输入签名，用于形成准确变化集并推进 AssetRevision。

Monitor 可以改成原生系统事件而不影响 Database；Database 也可以由手动 Refresh、命令行导入或测试直接调用。把两者合并会让
跨平台观察机制侵入资产身份和事务提交。

### 文件签名、AssetRevision 与任务占位不合并

- 文件签名是外部磁盘状态的比较值；
- `AssetRevision` 是数据库每次已提交逻辑变化的单调版本票；
- `pending_assets[handle] = revision` 只去重同一版本的后台任务，并阻止旧任务清除新任务占位；
- `scheduled_tasks` 独占 Future，用于回收任务和报告越过 candidate 捕获边界的异常。

它们的生命周期和失败语义不同。删除其中任意一层都会重新引入“同一变化重复调度”“旧结果覆盖新结果”或 Future 异常无人消费。

### CPU candidate 保持在 AssetManager 实现文件

`MeshImportCandidate` 和 `TextureImportCandidate` 是 Worker 到 Owner Thread 的内部消息，不是可序列化项目格式，也不是 Runtime
Resource。它们放在 `manager.cpp` 匿名命名空间可以避免把调度细节扩散到公共头；现阶段不值得为了共享少量字段再创造公共基类
或 `_data` 文件。

### MeshImportCache 的内容指纹不下沉到 Monitor

Monitor 和 Database 使用便宜的时间/大小信息发现变化；Mesh cache 在真正决定能否复用导入产物时读取并哈希 glTF 与外部
buffer 内容。前者优化常态扫描，后者保证可重建产物正确，两种成本模型应继续分离。

### Registry 与 ResourceFactory 边界成立

`AssetRegistry` 是唯一 Handle -> Runtime Object 缓存；`RenderResourceFactory` 只把 CPU DTO 转换为完整 GPU 对象，并返回可恢复
错误。ResourceManager 没有重新建立 Handle 缓存，Worker 也没有访问 Registry 或 Vulkan。该边界继续保留。

## 本步修正：数据库类型与 Runtime 类型一致性

此前同一个 GUID 的源文件和 `.meta` 若从 Texture 改为 Material 或 Mesh，Asset Database 会正确报告 modified，但 Registry 中
已经加载的旧对象仍以原 C++ 类型存在。新的刷新分支解析不到预期类型，只记录调度/重载失败，导致：

```text
Database: handle 42 -> Material
Registry: handle 42 -> Texture
```

这种状态比暂时没有 Runtime 对象更危险，因为所有后续 resolve 行为取决于调用方请求的 C++ 类型。

`AssetManager::scan()` 现在在处理 modified 资产前校验 Registry 对象是否匹配新的索引类型。不匹配时卸载旧对象并记录原因，
保持 Database/Registry 一致；后续第一次 `load_material/load_mesh/load_texture` 再按新类型正常创建。测试覆盖 Texture GUID 改为
Material 后旧 Texture 被移除、新 Material 可按同一 Handle 加载。

没有让 `AssetRegistry::replace_asset()` 允许跨类型替换：跨类型不是热替换同类 Runtime Resource，而是旧类型生命周期结束后
按新类型重新加载，显式 unregister 更符合语义。

## 发现但不在本步混改的问题

### P1：损坏 `.meta` 会被误当作资产删除

Mesh/Texture 解码失败和 GPU 创建失败已经保留旧 Runtime Resource；完整目录发现失败也不提交数据库快照。但单个已有资产的
`.meta` 解析失败或类型不匹配时，当前候选构建会跳过该资产，随后把旧 Handle 放进 `removed_assets` 并卸载旧 Runtime Resource。
这与“用户正在修复配置时继续使用上一份有效版本”的目标不一致。

下一步应让 Asset Database 区分：

- 物理源文件及 sidecar 确实删除：提交 removed；
- 已有路径的 sidecar 暂时无效：报告 issue，并保留该资产上一份有效记录、revision、依赖和 Runtime 对象；
- 新资产的 sidecar 无效：只报告 issue，不凭空建立身份；
- 重复 GUID 等全局身份冲突：不能按路径排序任意选择一个身份，需要明确阻止有歧义的快照提交。

### P2：Importer 输入仍有 TOCTOU 窗口

Worker 导入与 Owner Thread 发布之间只验证 Database revision。已知依赖在 scan 时可推进 revision，但 glTF 刚修改并引入一个
数据库尚不知道的新 `.bin` 时，新依赖可能在 Worker 读取过程中再次变化。candidate 随后登记当前依赖签名，存在极窄窗口把
旧 CPU 数据与新输入签名组合。

后续应让 candidate 携带本次实际读取输入的内容指纹，Owner Thread 在写缓存和 GPU 发布前核对；不匹配则丢弃 candidate 并
请求新 revision/任务。这个问题应与 Importer 输入清单契约一起设计，不在 metadata 修复中顺带加入。

### P2：Material 扫描刷新仍可能同步加载 Texture

Material 文件本身解析和对象组装很轻，但修改为引用一个尚未加载的 Texture 时，`reload_material()` 会走同步 `load_texture()`，
从而在 Owner Thread 解码图片并创建 GPU 资源。后续应把 Material candidate 与依赖 ready 条件纳入同一异步发布模型；当前先
保留，因为启动必需资源和显式 Inspector 提交仍需要明确的同步入口。

### P3：大型项目监视后端

500ms 递归文件状态采样在当前项目规模合理。只有 profile 证明遍历成为 Editor 卡顿来源后，再替换原生事件后端；该优化不应
早于上述正确性问题。

## 路线图清理

阶段 3 中仍以旧全局 UploadManager 名称描述的 `enqueue_upload/flush_batch` 任务已经被显式 `UploadBatch` 完整替代；staging、
CommandContext 和目标引用也已经由 timeline completion 延迟回收。路线图改为描述现有 batch contract，不再把已完成事项列为
未完成。

## 验证

- 新增 Runtime 类型变化回归测试；
- 既有删除资产测试继续证明真实删除会卸载 Registry 对象；
- 完整 Debug 构建与 CTest；
- 本步只改变 modified 资产的类型不匹配分支，不改变正常 Mesh/Texture 异步刷新和 Material 同类型替换。

## 下一步

第 029 步实现失败安全的 metadata 候选合并，并为损坏 sidecar、类型不匹配、真实删除和重复 GUID 分别建立测试。完成后再处理
Importer 输入指纹的 TOCTOU 窗口。
