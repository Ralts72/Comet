# 018 事务式 Upload Batch

## 目标

让非关键 Runtime Resource 在 upload staging 分配失败时能够安全返回错误，而不是终止进程或把已经录制一半的 Mesh/Texture
上传提交给 Queue。

## 为什么仅有目标资源 try-create 不够

Mesh 至少包含 vertex buffer，通常还包含 index buffer。即使这两个 device allocation 都成功，UploadManager 仍可能在
创建 host-visible staging page 时失败。更危险的顺序是：

```text
enqueue vertex succeeds and records commands
enqueue index staging allocation fails
```

如果只把第二次失败返回给上层，active CommandContext 中仍残留 vertex copy，后续 flush 可能提交一份不再对应任何可发布
Mesh 的半套 batch。因此可恢复语义必须覆盖整个 active batch，而不只是 VMA target allocation。

## 统一结果类型

原本定义在 Allocator 头中的结果模板更名为 `GpuResourceResult<T>`，并迁入独立的
`graphics/resource/resource_result.h`。它现在服务于三层：

- Allocator：返回完整 raw allocation 或 Vulkan error；
- Buffer/Image：返回完整 owning wrapper 或 Vulkan error；
- UploadManager：用 `GpuResourceResult<void>` 表达 enqueue 事务成功或失败。

该类型不包含 VMA handle 或 allocator 依赖，独立文件是跨层稳定契约，不是为单个调用点创建的数据类。

## 可恢复 staging

`Buffer::try_create_upload_buffer()` 与 GPU buffer/image 工厂采用相同顺序：先请求 persistent-mapped allocation，成功后才构造
CPUBuffer。`UploadManager::try_allocate_staging()` 在需要新 page 时把调用方选择的 `within_budget` 继续传到底层；已有 active
或 available page 能满足请求时直接复用，不做额外分配。

强失败 `enqueue_upload()` 固定使用 `within_budget=false`，保持关键和旧调用点的行为。recoverable
`try_enqueue_upload(..., within_budget=true)` 才把预算作为拒绝新 memory block 的条件。

## Abort 顺序

recoverable enqueue 一旦 staging 失败，会自动调用 `abort_batch()`：

```text
staging failure
  -> CommandContext::discard()
  -> free never-submitted command buffer
  -> recycle active staging pages under normal cache policy
  -> release active destination Buffer/Image references
  -> clear active batch
  -> return Vulkan error
```

`discard()` 只允许用于尚未提交的 CommandContext，已提交 context 仍由 pending batch 和 completion 管理。abort 不等待
Device/Queue idle，也不产生 timeline value。pending batch 不能回滚，因为 GPU 可能已经读取其资源，只能按 completion 正常
回收。

UploadManager 析构也复用 `abort_batch()` 清理未 flush 的工作，因此主动取消不再产生“destroyed without submitting”误导日志。

## 验证

- 编译期接口测试覆盖 buffer/image recoverable enqueue、batch abort 和 CommandContext discard；
- 测试确认 upload CPUBuffer 只能由成功 allocation 的静态工厂构造；
- 单元测试覆盖 `GpuResourceResult<void>` 的默认失败与显式成功；
- 原强失败 enqueue、staging page、completion 和 RAII 接口测试保持通过；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 GPU within-budget staging 拒绝与 abort 后 Validation Layer 行为属于人工验证项，不阻塞提交。

## 当前限制与后续

- try-enqueue 失败会取消整个 active batch，不支持 batch 内局部回滚；这是当前每个 Runtime Resource 独立 flush 的正确边界；
- flush/Queue submit 失败仍是强失败，因为 Vulkan submit 失败通常不是可通过资源重试恢复的普通内存压力；
- Mesh/Texture 尚未调用 recoverable target/staging 路径；
- 下一步让 Mesh/Texture 采用“全部 target allocation 成功 → 全部 enqueue 成功 → flush → 构造 wrapper”的事务顺序。
