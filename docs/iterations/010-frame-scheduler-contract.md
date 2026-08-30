# 010 FrameScheduler 生命周期契约

## 目标

把含义宽泛的 FrameManager 收敛为明确的 FrameScheduler：它不负责所有渲染资源，而是定义 frame slot 何时可以复用、
image 与 slot 如何配对、submission 如何登记，以及循环 slot 之外的单调 frame serial。

## 改动前

FrameManager 提供 `begin_frame()`、`prepare_image()` 和 `end_frame()`，但调用顺序只存在于 SceneRenderer 的实现中：

- `begin_frame()` 实际只等待 fence，名字看不出尚未开始录制；
- `prepare_image()` 同时等待 swapchain image、reset 当前 fence 并建立配对；
- SceneRenderer 直接写 `FrameSlot::last_submission`；
- 没有 frame serial，后续 VMA frame index、遥测或 deferred batch 只能误用循环 slot index；
- acquire 失败时缺少显式“slot 已等待但 frame 尚未开始”的状态。

## 新契约

类和文件统一更名为 FrameScheduler，生命周期顺序变为：

```text
wait_for_current_slot()
  -> collect completed retirement
  -> acquire swapchain image
  -> begin_frame(image index)
       -> wait previous image slot
       -> reset current fence
       -> mark frame active
  -> record commands
  -> queue submit -> completion
  -> record_submission(completion)
  -> end_frame()
       -> stamp monotonic frame serial
       -> advance circular slot
```

内部状态机区分 `slot_ready`、`frame_active` 和 `submission_recorded`。错误顺序会立即诊断，而不是等到 fence 永久不 signal
后表现为下一帧卡死。

`wait_for_current_slot()` 是幂等的：如果 acquire/recreate 暂时无法开始 frame，下一次循环仍使用已经安全等待的 slot，
不会 reset fence。只有成功 acquire 后 `begin_frame()` 才 reset fence，因此不存在“fence 已 reset 但没有 submission”的
死锁窗口。

## Frame serial

FrameScheduler 从 1 开始维护 `uint64_t` 单调 serial，成功 `end_frame()` 时写入当前 FrameSlot 的
`last_submission_serial` 并递增。它与 `frame_slot_index` 的区别是：

- slot index 在 `[0, frames_in_flight)` 循环，只用于选择 per-frame storage；
- frame serial 在进程生命周期内单调递增，可用于 VMA current frame、遥测、缓存 age 和 retirement 诊断；
- acquire 失败且没有提交时 serial 不推进。

溢出被视为 fatal，不允许静默回绕破坏 age/ordering 语义。

## 命名与物理结构

旧 `render/frame_manager.h/.cpp` 被 `render/frame_scheduler.h/.cpp` 替换，SceneRenderer 和 Editor getter 同步改为
`get_frame_scheduler()`。这不是为命名增加 facade；旧类被直接收敛和替换，没有同时保留 Manager/Scheduler 两套入口。

FrameScheduler 仍只拥有 FrameSlot 与 SwapchainImageState。GpuRetirementQueue 当前由 SceneRenderer 拥有，但 collection
严格放在 slot wait 之后；未来 descriptor/transient arena 可以接入同一 begin-frame 安全点，而不要求 Scheduler 物理拥有
所有 allocation。

## 验证

- 接口测试确认 wait/begin/record/end 生命周期与单调 serial 查询存在；
- 所有权测试确认 FrameScheduler 不可 copy/move；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 acquire/out-of-date、连续 resize 与 Validation Layer 行为属于人工图形验证项，不阻塞本次提交。

## 后续

下一步审视现有 per-frame uniform buffer 与 MaterialDescriptorState：将它们的 reset/update 时机明确绑定到
FrameScheduler 的 slot reuse contract，再决定是否已经有足够负载证据引入通用 DescriptorArena/TransientBufferArena，
避免仅为路线图名词创建空抽象。
