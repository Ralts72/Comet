# 013 GPU Memory Budget Snapshot

## 目标

在上一轮 optional `VK_EXT_memory_budget` 能力之上增加一个稳定、只读的 Comet 查询边界，让后续 UploadManager、资源流送
和诊断代码能够观察显存压力，同时不依赖 `VmaBudget`、`VmaAllocator` 等第三方实现类型。

本步只建立数据获取契约，不在查询接口里混入缓存淘汰或分配失败策略。

## 类型与职责

`MemoryHeapBudget` 对应一个 Vulkan memory heap，保存：

- VMA block/allocation 数量；
- VMA 已申请 block bytes 和实际 allocation bytes；
- 驱动或 VMA 估算的 usage bytes 与 budget bytes；
- 饱和到零的 `available_bytes()`，避免 usage 超过 budget 时发生无符号下溢。

这些稳定 DTO 位于 `graphics/resource/memory_budget.h`，不包含 Vulkan 或 VMA 头。`MemoryBudgetSnapshot` 保存全部
heap 及 `driver_reported` 标记。该标记为 `true` 只表示
`VK_EXT_memory_budget` 已实际启用、usage/budget 来自对应驱动路径；为 `false` 时数据仍可用于观察和保守回收，但它是
VMA 根据自身分配与 heap size 得出的估算值，不能被解释为精确硬上限。

## 调用边界

```text
Resource/Upload policy
  -> Device::query_memory_budget()
  -> Allocator::query_memory_budget()
  -> vmaGetMemoryProperties() + vmaGetHeapBudgets()
  -> MemoryBudgetSnapshot
```

Allocator 负责第三方结构到 Comet DTO 的转换；Device 只转发查询，不缓存数据，也不拥有回收策略。查询名称显式使用
`query`，强调结果是调用时刻的快照，在并发分配环境中可能立即过期。

`vmaGetHeapBudgets()` 是 VMA 提供的轻量统计接口，不会像 `vmaCalculateStatistics()` 那样遍历详细分配结构。即便如此，
后续消费方仍采用事件驱动采样：只在 staging pool 需要扩张或批量流送决策点查询，不把它变成每帧固定工作。

## 验证

- 接口编译期测试确认 Allocator 返回稳定的 `MemoryBudgetSnapshot`；
- 单元测试覆盖 `available_bytes()` 的正常计算和 usage 超预算时的饱和行为；
- 单元测试确认 snapshot 默认是空的估算状态；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 GPU 上不同 heap 数量、driver-reported 数值和 Validation Layer 行为属于人工验证项，不阻塞提交。

## 当前限制与后续

- snapshot 不做跨 heap 求和；不同 heap 可能代表不同内存域，简单相加会掩盖单个 heap 的压力；
- snapshot 没有定义自动淘汰阈值，也不改变 allocation 失败语义；
- Device 不定时采样或保存历史，避免底层包装器演变成策略管理器；
- 下一步由 UploadManager 在 staging page pool 增长前消费 snapshot，并优先回收当前没有 pending batch 引用的超大 page。
