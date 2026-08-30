# 014 预算感知的 Staging Cache

## 目标

补齐可复用 staging page 的内存上界和预算消费策略，避免一次大批量导入后 UploadManager 永久保留所有 page，同时保持
异步 batch 的 GPU 生命周期安全。

## 改动前

完成的 batch 会把全部 staging page 放回 `m_available_pages`：

```text
upload spike -> many/default + oversized pages
GPU complete -> every page enters available pool
idle         -> every page remains allocated until UploadManager destruction
```

best-fit 可以减少重复分配，但空闲池没有数量上限，单次超大上传也会把按需创建的大 page 永久留在池中。上一轮虽然已经
提供 heap budget snapshot，UploadManager 尚未消费它。

## 改动后

`UploadManager::CreateInfo` 增加两项保守策略参数：

- `max_cached_staging_pages = 4`：只缓存至多四个默认大小 page；
- `memory_pressure_threshold_percent = 90`：新 page 的预计增长使任一有效 heap 达到该比例时视为高压力。

回收和增长分成两个时机：

```text
batch completion
  -> default page && cache below limit -> available pool
  -> oversized page / cache full       -> destroy now

pool miss before creating page
  -> query MemoryBudgetSnapshot
  -> normal pressure -> keep bounded idle cache, create required page
  -> high pressure   -> release all idle pages, warn once, create required page
```

预算查询只发生在 active page 和 available best-fit 都不能满足请求、确定即将增长时。常规帧和成功复用不会查询，因而
不会把统计采样变成固定帧成本。

## 压力计算

`MemoryHeapBudget::reaches_usage_percentage()` 使用当前 usage 加预计 page capacity 与阈值比较。计算使用饱和加法，并以
无溢出的整数方式求阈值；budget 为零或百分比不在 `[1, 100]` 时不把该 heap 当成有效压力信号。

当前 snapshot 不能预先确定 VMA 将为新 upload allocation 选择哪个 memory heap，因此策略对任一有效 heap 的高压力
采取保守回收。它只释放本来就可随时重建的空闲 staging cache，不删除 Runtime Asset，也不拒绝关键分配，所以误判的
代价只是后续可能重新创建 page，不会破坏正确性。

连续高压力只在从正常状态进入压力状态时记录一次 warning；只有后续一次增长采样恢复正常，才允许下一次压力再次记录，
避免批量导入时刷屏。warning 会标明预算来自驱动还是 VMA 估算。

## 生命周期保证

- active batch 中已写入但未提交的 page 不在空闲池，不参与释放；
- pending batch 在 timeline completion 前独占 page，不参与释放；
- 只有 `collect_completed()` 已确认完成并转为空闲所有权的 page 才能缓存或销毁；
- 压力策略不调用 Queue/Device idle，也不引入新的 CPU wait；
- 无论预算是否精确，allocation 失败语义保持不变，没有为关键资源启用 `WITHIN_BUDGET`。

## 验证

- 单元测试覆盖 heap 百分比阈值、预计增长、无符号溢出、零 budget 和非法百分比；
- 接口测试固定默认 page size、空闲 page 上限和 90% 压力阈值；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 GPU 上的 heap 选择、压力 warning 与 Validation Layer 行为属于人工验证项，不阻塞提交。

## 当前限制与后续

- 策略尚不知道新 allocation 的精确 heap，只执行安全但可能偏保守的 cache 释放；
- 尚未把非关键 streaming 资源改为可恢复 allocation 失败，创建新 staging page 失败仍沿用当前 fatal 路径；
- 没有做资产 LRU、mipmap streaming 或 Runtime Asset 淘汰；
- 下一步先复盘完整 GPU 资源生命周期，确认接口和所有权收敛后再设计可恢复失败契约。
