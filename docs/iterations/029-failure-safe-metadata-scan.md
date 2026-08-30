# 029 Metadata 失败安全扫描

## 目标

修复资产热刷新中的失败安全缺口：已有资产的 `.meta` 在外部编辑、冲突合并或未完成写入期间暂时无法解析时，不再被
Asset Database 当成物理删除，也不会卸载上一份有效 Runtime Resource。

同时把重复 GUID 从“按路径排序选择第一个”改为“身份有歧义则拒绝整次候选快照”，避免同一个持久身份悄悄指向另一文件。

## 改造前的问题

Asset Database 虽然先构建局部候选再提交，但无效 sidecar 的处理是直接跳过对应源文件：

```text
old snapshot: handle 42 -> textures/a.png
external edit: textures/a.png.meta 暂时无法解析
scan candidate: 不包含 handle 42
diff: handle 42 被报告 removed
AssetManager: unregister handle 42
```

Texture/Mesh 内容解码失败、GPU allocation 失败和完整目录遍历失败都已经保留旧对象，唯独 metadata 编辑中间态会穿透事务
边界并卸载资源，行为不一致。

重复 GUID 的旧策略同样有风险：递归扫描按路径排序后保留第一个资产，意味着添加一个路径更靠前的冲突文件可能让已有 Handle
在一次扫描中改指向另一源文件。

## Last-known-good 候选合并

扫描现在把 source + sidecar 分为三种情况：

```text
有效 metadata
  -> 正常构建 AssetRecord candidate

无效 metadata + 同一路径存在上一份记录
  -> 报告 issue
  -> 将上一份 AssetRecord 合入 candidate snapshot
  -> 保留旧 source signature / AssetRevision / dependencies

无效 metadata + 新路径没有上一份记录
  -> 只报告 issue
  -> 不生成猜测的 Handle，不进入索引
```

保留只按数据库上一份规范化项目相对路径匹配，不尝试从损坏文本中正则提取 GUID，也不凭文件名猜测移动关系。这样恢复的是
已经被数据库验证过的事实，而不是对无效输入做部分解析。

保留的 Material 不重新解析当前文件，继续使用上一份 AssetRecord 中的 Handle 依赖；保留的 Mesh 继续携带上一份 Importer
源依赖。反向索引因此也保持 last-known-good 状态。保留记录不进入 added/modified/removed，revision 不推进，AssetManager 不会
调度无意义的刷新或修改 Registry。

扫描报告仍然 `snapshot_updated = true`，因为其他无歧义资产的新增、修改和删除可以正常提交；`succeeded()` 为 false，Project
和 Log 继续展示具体 sidecar 问题。

## 重复 GUID 的事务策略

重复 GUID 不是单个资产的导入失败，而是全局身份映射不再是一对一。扫描现在收集冲突诊断后返回，不提交任何局部 candidate：

```text
candidate: a.png -> 42
candidate: b.png -> 42
  -> duplicate guid issue
  -> snapshot_updated = false
  -> previous database / revision / Registry 全部保持
```

首次扫描出现冲突时数据库保持为空；已有项目后来出现冲突时保留整份上一快照。用户修复任一 `.meta` 后，下一次监视事件会重新
扫描并正常提交。这里不再用确定性排序掩盖身份错误。

新 GUID 生成除检查本次 candidate 外，也检查当前数据库仍持有的 Handle，避免在一次 metadata 丢失/重建过程中立即复用刚从
候选中消失的旧身份。跨重启的永久 tombstone 不在当前随机 64-bit GUID MVP 范围内。

## 真实删除仍然成立

Last-known-good 只适用于“源文件仍存在，但其 sidecar 无效”。源文件物理删除时不会形成 source candidate，即使留下孤立
`.meta` 也只报告 orphan issue；旧 Handle 仍进入 removed，AssetManager 按原规则卸载 Runtime Resource 及失效依赖。

缺失 `.meta` 也没有从内存数据库静默恢复，因为 sidecar 是项目持久身份的事实来源。现有规则继续为没有 sidecar 的源文件生成
新 GUID；如果用户删除 `.meta`，引用变化是明确的身份重建，而不是依赖 Editor 是否尚未重启的偶然行为。

## 验证

- 首次扫描遇到重复 GUID 时拒绝候选快照，不选择“第一个”资产；
- 已有有效快照后来出现重复 GUID 时保留原路径和 revision；
- 已有 Texture 的 metadata 变为不可解析 YAML 时，保留 AssetRecord、source signature、revision 和 Runtime Texture；
- metadata 可解析但声明类型与源扩展不匹配时采用相同 last-known-good 行为；
- 新资产 metadata 无效时仍只排除该资产，其他有效资产正常进入索引；
- 真实删除资产的既有测试继续报告 removed 并卸载 Registry；
- Debug 全量构建和完整 CTest。

## 下一步

下一步收敛 Material 文档解析失败时的依赖状态。当前旧 Runtime Material 能保留，但候选 AssetRecord 会丢失上一份 Texture Handle
依赖，使后续 Texture 刷新无法传播到它。应沿用本步的 last-known-good 原则保留旧依赖，同时仍推进 Material 自身 revision 并
持续报告文件错误。之后再处理 Worker 实际读取输入指纹与发布之间的 TOCTOU 窗口。
