# 024 显式 UploadBatch 事务所有权

## 目标

把 UploadManager 中唯一的隐式 active state 改成显式、独立的 UploadBatch scope。每次 Runtime Resource 创建都拥有自己的
CommandContext、staging pages 和目标资源引用；任一 batch 失败或离开作用域只回滚自身工作，不会取消或提交另一个调用方的
未提交上传。

## 原结构的问题

此前 UploadManager 保存：

```text
m_active_context
m_active_resources
```

所有 `enqueue_upload()` 都写入这组全局 active state，`flush_batch()` 和 `abort_batch()` 也只能操作它。Mesh/Texture 虽然遵守
“创建后立即 flush”的调用约定，但接口允许其他工作在中间混入；staging 失败时自动 abort 的范围由时间顺序决定，而不是由
所有权决定。

## 新职责划分

`UploadManager` 现在只负责跨事务共享的设施：

- 创建显式 Batch；
- 管理可复用 staging 空闲池和 memory pressure 策略；
- 接收已 submit 的 pending batch；
- 按 GpuCompletionPoint 回收 pending context/resources/page；
- 析构时等待已提交工作。

`UploadBatch` 与 UploadManager 放在同一头/实现文件中，避免为紧密协作的单一概念拆出额外物理文件。它是通用 graphics upload
事务，不依赖 Mesh、Texture、Asset 或 Editor，后续其他 GPU 数据上传也能复用。

每个 UploadBatch 独占：

```text
unique CommandContext
BatchResources {
  destination Buffer owners
  destination Image owners
  staging page owners
}
```

## 状态机

调用顺序变为：

```text
UploadManager::begin_batch()
  -> batch.try_enqueue_upload(...) one or more times
  -> batch.submit()
       -> ownership moves into UploadManager pending list
       -> batch becomes inactive
```

recoverable enqueue 的 staging allocation 失败会立即 `batch.abort()`；析构一个仍 active 的 batch 也自动 abort。abort discard 未
提交 CommandContext、按正常缓存策略回收该 batch 的 staging pages，并释放该 batch 的目标引用。已经提交的 batch 不可 abort，
其所有权已经移交给 manager，只能等待 completion。

空 batch submit、已 submit/abort 后继续使用均是状态机错误并强失败。UploadBatch 不可默认创建、复制或移动，保证事务固定在
`begin_batch()` 所在作用域；C++17 guaranteed copy elision 允许 manager 直接按值返回这个 scope。UploadManager 记录开放 batch
数量，若 manager 在 batch 之前销毁会明确报错，而不是留下悬空 manager 指针。

## 多 Batch 隔离

多个 batch 可以在同一 Owner Thread 上同时开放。它们从 manager 的 available pool 各自取走 page，page 在 batch 提交、abort
或 completion 回收前不会回到共享池。因此：

- Batch A abort 不会触碰 Batch B 的 context/resources；
- Batch A submit 的 completion 只覆盖 A 的命令与 owner；
- 共享 memory pressure 回收只处理 available pages，不处理任一 open/pending batch 的 page。

当前接口仍不是多线程并发 API；显式 batch 解决的是事务所有权，不改变 UploadManager 的 Owner Thread 语义。

## Runtime Resource 迁移

Mesh 在全部 vertex/index target allocation 成功后创建自己的 batch，依次 enqueue 并 submit。Texture 在 Image/ImageView 完整
后创建自己的 batch。两者直接取得非可选 GpuCompletionPoint；显式 batch 的 submit 不再需要用 optional 表示“manager 当前
可能没有 active 工作”。

旧的 UploadManager 全局 `enqueue_upload/try_enqueue_upload/flush_batch/abort_batch/upload_and_wait` 全部删除，避免新调用点
绕开 scope。

## 验证

- 编译期测试确认 UploadManager 只负责 `begin_batch()`，不再暴露全局 enqueue/abort；
- 编译期测试确认 UploadBatch 提供 Buffer/Image 的强失败与 recoverable enqueue，并返回非可选 completion；
- 编译期测试确认 UploadBatch 不可从外部构造、复制或移动；
- Mesh/Texture 是仅有生产 upload 调用点，并已迁移到各自 batch；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

## 下一步

增加可注入的 staging allocation seam，在不依赖真实 GPU 显存耗尽的情况下验证：Batch A 的第二次 staging growth 失败会释放 A，
同时不改变已开放 Batch B；随后 B 仍可 submit。fault injection 只进入测试配置，不扩散到资产或 Runtime Resource API。
