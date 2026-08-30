# 012 VMA Memory Budget 可选能力

## 目标

在设备支持时启用 `VK_EXT_memory_budget`，让 VMA 使用驱动提供的 heap usage/budget，而不是只依据 heap size 和自身
allocation 做估算；扩展缺失时保持完全兼容，不把可选能力变成设备筛选硬要求。

## Capability 协商

设备扩展现在分为两组：

- required：`VK_KHR_swapchain`，缺失会拒绝设备；
- optional：`VK_EXT_memory_budget`，存在则加入 logical-device enabled extension 列表，并设置
  `DeviceCapability::memory_budget_enabled`。

Allocator 不再次查询 physical device extension，也不根据“可能支持”自行猜测。Device 只把已经实际启用的 capability
传给 `Allocator::CreateInfo`，后者才设置 `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`。这避免 VMA flag 与 logical
device extension 状态不一致。

项目要求 Vulkan 1.3，`VK_KHR_get_physical_device_properties2` 的功能已进入 core，因此不额外要求同名 instance extension。

## Frame index

启用 memory budget 后，VMA 建议每帧调用 `vmaSetCurrentFrameIndex()`，以低成本刷新驱动预算缓存。调用链为：

```text
FrameScheduler.current_frame_serial
  -> SceneRenderer begin-frame safe point
  -> Device::set_allocator_frame_index()
  -> Allocator::set_current_frame_index()
  -> vmaSetCurrentFrameIndex()
```

传入的是不随 frames-in-flight 回绕的 frame serial，而不是 `[0, N)` 循环 slot index。VMA API 接受 `uint32_t`，Allocator
使用 serial 的低 32 位；该值只用于 VMA 的 frame/budget 刷新，不承担 Comet 的排序判断，Comet 自身继续保存完整
`uint64_t` serial。

扩展未启用时 `set_current_frame_index()` 是无副作用的快速返回，不会错误调用扩展预算路径。

## 验证

- 纯逻辑测试确认 optional extension 仅在 available set 中存在时被选中；
- DeviceCapability 与 Allocator::CreateInfo 默认关闭 memory budget；
- 接口测试确认 Allocator 接受 `uint64_t` frame serial 并暴露 capability 状态；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实设备上的扩展启用日志、VMA budget 刷新和 Validation Layer 属于人工 GPU 验证项，不阻塞提交。

## 当前限制与后续

- 本步只让 VMA 获得更准确预算，没有公开 heap snapshot；
- 没有对 allocation 设置 `VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT`，关键资源失败策略不变；
- 没有根据预算自动淘汰资产或 staging page；
- frame serial 的 `uint32_t` 低位会在约 42 亿次提交后回绕，但 Comet 的 `uint64_t` 排序语义不受影响。

下一步增加轻量 heap budget snapshot，并由 UploadManager 在 page pool 增长前进行诊断和回收；采样保持低频或事件驱动，
不每帧计算 VMA detailed statistics。
