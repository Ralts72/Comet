# 030 Material 依赖失败安全

## 目标

让 `.mat` 文档暂时无法解析时，Asset Database 既能提交 Material 文件已经变化的事实、推进 revision 并触发重载诊断，又能
保留上一份有效 Texture Handle 依赖，保证旧 Runtime Material 留存期间依赖传播仍然正确。

## 与 Metadata 失败的区别

第 029 步处理的无效 `.meta` 会让资产身份、类型和 Importer 设置都不可信，因此整个 AssetRecord 使用上一份有效状态，revision
不推进。

`.mat` 内容失败时，sidecar 仍然提供可信 Handle 和 Material 类型，源文件状态也确实已经变化：

```text
metadata valid + material document invalid
  -> AssetRecord 身份和路径使用当前 candidate
  -> source signature 使用当前文件状态
  -> AssetRevision 推进
  -> AssetManager 尝试 reload 并输出解析错误
  -> Runtime Material 不替换
  -> dependencies 使用上一份成功解析结果
```

如果把整个 Material record 当作 metadata 一样冻结，修复前无法表达源文件已经变化；如果像旧实现一样把 dependencies 清空，
旧 Runtime Material 仍引用原 Texture，但数据库已经丢失这条关系，两边会出现事实分裂。

## 改造前的问题

Material candidate 初始 dependencies 为空。解析异常后扫描直接跳到下一资产，随后仍提交 record：

```text
Runtime Material 100 -> Texture 42
Database Material 100 -> []
```

此后 Texture 42 热刷新成功时，`get_dependents(42)` 找不到 Material 100；Texture 删除时递归失效也不会覆盖 Material 100。
旧 Runtime 对象虽然被保留，却失去了更新和卸载所需的依赖语义。

## 当前实现

MaterialSerializer 抛出异常后，扫描先记录原诊断，再按同一 Handle 查找上一份 AssetRecord。上一记录同样是 Material 时复制其
已验证 dependencies，随后继续执行统一的依赖存在性检查和反向索引构建。

新建且从未成功解析的 Material 没有 last-known-good 依赖，仍保持空集合；不会从无效 YAML 猜测 Handle。Material 修复后正常
解析当前内容并替换依赖集合，source signature 的再次变化会推进新 revision。

这项状态仍属于 `AssetDatabase`，没有下沉到 Runtime Material：依赖图用于扫描变化集、递归失效和加载编排，Runtime 对象不应
反向承担项目索引恢复。

## 验证

- 有效 Material 初始建立 Texture 正向/反向依赖；
- `.mat` 损坏后扫描报告 issue、Material 出现在 modified 且 revision 推进；
- 同一次提交后正向 dependencies 与 Texture dependents 都保持上一有效集合；
- AssetManager 的同步 reload 失败时 Registry 继续返回同一个旧 Runtime Material；
- 完整 Debug 构建和 CTest。

## 下一步

下一步处理后台 Mesh Importer 的输入 TOCTOU：candidate 应携带本次实际读取的 glTF/外部 buffer 指纹，Owner Thread 在写导入缓存、
登记依赖和创建 GPU Mesh 前验证输入仍匹配；若不匹配，丢弃候选并推进/调度新 revision，不能把旧 CPU Mesh 与新文件签名组合。
