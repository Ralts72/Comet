# 016 可恢复的 Allocator 创建契约

## 目标

为后续非关键 streaming 资源提供“显存不足时保留旧资源、使用占位或稍后重试”的底层能力，同时保证现有 render target、
frame resource、staging 和初始化资源仍然在创建失败时立即暴露错误，不静默降级。

## 双轨接口

Allocator 现在提供两类入口：

```text
critical caller
  -> create_buffer/image()
  -> success: complete allocation
  -> failure: fatal with resource context

recoverable caller
  -> try_create_buffer/image()
  -> success: ResourceAllocationResult.value
  -> failure: ResourceAllocationResult.result (vk::Result)
```

现有 `create_*` 不复制 VMA 调用，而是委托给 `try_create_*`，失败后统一补充 debug name、大小、usage 和 Vulkan result
并执行强失败。因此两条路径共享资源创建、命名、persistent mapping 校验和失败清理逻辑，不会随时间产生行为漂移。

`ResourceAllocationResult<T>` 默认是 `eErrorUnknown` 的失败状态，并通过 `success()`/`failure()` 工厂维护 value 与 result
的一致性；失败结果不保存 allocation，调用方不能构造“成功状态 + 空 handle”并误发布半初始化 Vulkan 对象。

## Within-budget 语义

`AllocationCreateInfo::within_budget` 默认是 `false`。显式开启时才向 VMA 传递
`VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT`，使需要新建 memory block 且会超过预算的分配返回错误。

该选项与 try/create 是两个正交维度：

- `try_create_* + within_budget=true`：适合可降级、可重试的 streaming 资源；
- `try_create_* + within_budget=false`：适合希望处理真实 OOM、但不把预算当硬上限的工具路径；
- `create_* + within_budget=false`：当前关键资源的默认行为；
- 关键资源不会仅因为预算估算偏保守而被拒绝。

程序员契约错误仍然强失败，例如 device-only allocation 请求 persistent mapping。可恢复接口只处理运行时资源创建错误，
不把非法参数伪装成普通内存压力。

## Persistent mapping 失败

如果 VMA 创建成功但请求的 persistent mapping 没有返回映射地址，Allocator 会立即销毁刚创建的 buffer/image，并返回
`eErrorMemoryMapFailed`。这保证 recoverable path 同样没有泄漏，也不会返回“成功 handle + 空 mapped pointer”的矛盾状态。

## 验证

- 编译期接口测试确认 Allocator 对 buffer/image 都有对应的 `try_create_*` 结果类型；
- 单元测试确认 `AllocationCreateInfo` 默认不启用 within-budget；
- 单元测试确认结果默认失败、显式 `eSuccess` 才成功；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实设备 OOM、VMA within-budget 拒绝和 persistent mapping 异常属于难以稳定注入的人工 GPU 验证项，不阻塞提交。

## 当前限制与后续

- Buffer/Image 包装仍只调用强失败接口，Runtime Resource 尚不能消费失败结果；
- ResourceManager/RenderResourceFactory 仍返回非空资源对象，没有失败类型；
- 尚未定义占位资源与重试队列；
- 下一步先把尝试创建传递到 Buffer/Image 静态工厂，确保失败时连包装对象都不会发布，再接 AssetManager 的保留旧资源策略。
