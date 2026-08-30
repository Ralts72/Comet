# 011 Material Descriptor 生命周期

## 目标

把现有 per-frame UBO/descriptor 的安全更新与 FrameScheduler completion contract 对齐，并修复 Material descriptor cache
只增不减的问题；在没有 transient allocation 负载前，不创建空的 DescriptorArena 或 TransientBufferArena。

## 现状判断

SceneRenderer 已经为每个 FrameSlot 创建独立 view/projection CPUBuffer，MaterialDescriptorState 也为每个 material handle
分配 `frame_slot_count` 个 descriptor set 和对应资源引用。当前 slot 在写 UBO、更新 descriptor 前一定经过 fence wait，
因此它们的更新安全性已经成立，不需要再包一层只转发索引的 arena。

真正缺口是 `m_material_descriptors` 永不删除。材质卸载、场景切换或持续创建临时 material handle 后，DescriptorPool、
DescriptorSet 数组和 Texture owner 都会永久留在 SceneRenderer。

## Completed frame serial

FrameScheduler 现在在每次实际 fence wait 后读取该 FrameSlot 的 `last_submission_serial`，推进单调的
`completed_frame_serial`：

```text
wait slot fence(serial S)
  -> completed_frame_serial = max(completed_frame_serial, S)
```

swapchain image 仍被其他 slot 占用时发生的 fence wait 也走同一入口。因为 graphics submissions 按 queue 顺序执行，
serial S 完成意味着更早的 graphics frame serial 也已完成。

`is_frame_serial_complete()` 因此表达实际完成事实，而不是用 `current - frames_in_flight` 猜测。serial 0 表示从未提交，
可直接视为无在途引用。

## Descriptor cache 回收

MaterialDescriptorState 新增 `last_used_frame_serial`，每次实际准备 descriptor set 时写入当前 frame serial。下一帧在：

1. `wait_for_current_slot()`；
2. GpuRetirementQueue collection；
3. Material descriptor collection；
4. acquire/begin/record；

的安全顺序中，删除 `last_used_frame_serial <= completed_frame_serial` 的未继续使用状态。

持续每帧使用的材质，其 last-used serial 总是晚于 completed serial，不会抖动重建；停止使用的材质则在最后一次相关
submission 完成后销毁整组 DescriptorPool，并释放缓存的 Texture/UBO shared owner。

## 为什么暂不创建 Arena

通用 arena 有价值的前提是存在大量同生命周期的小 allocation、需要批量 reset。目前：

- UBO 是每 slot 一个长期 persistent mapping Buffer；
- 每个 material 的 descriptor pool/set 是跨帧缓存，不是当帧临时对象；
- 没有 per-draw transient descriptor 或 transient buffer allocation。

此时创建 DescriptorArena 只会把现有 vector/map 包装成新类，没有减少 allocation、没有统一新的 reset 行为，也没有第二个
真实消费者。路线图保留该方向，但等待 RenderGraph、per-draw data 或更复杂材质系统提供实际负载。

## 验证

- FrameScheduler 接口测试确认 current/completed serial 与 completion 查询契约；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

需要用真实 GPU 连续切换材质并观察 DescriptorPool 释放的验证属于人工检查项，不阻塞提交。

## 后续

下一步接入 optional `VK_EXT_memory_budget` 能力：只在扩展实际启用时配置 VMA，并使用 FrameScheduler 的单调 serial 调用
`vmaSetCurrentFrameIndex()`。这会为后续 streaming、缓存淘汰和超大 staging page 回收提供真实预算依据。
