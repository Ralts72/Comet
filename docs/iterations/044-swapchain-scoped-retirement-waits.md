# 044 Swapchain Scoped Retirement Waits

## 目标

移除正常 swapchain recreation 的 Device-wide idle，同时保证旧 generation 的 graphics 使用和 presentation 使用都完成后才释放。

## 等待模型

当前 Vulkan 路径没有启用 swapchain maintenance present fence，因此采用两段最窄可用等待：

```text
FrameScheduler::wait_for_all_slots()
  -> 等待所有 graphics frame fence
present Queue::wait_idle()
  -> 等待 presentation engine 不再使用旧 presentable images
release old dependent/core
  -> create/commit/rebuild new generation
```

这不是“完全无等待”的异步退休，但阻塞范围已经从整个 Device 收窄到真实相关的 graphics submissions 与 present queue。transfer/upload 等
不相关 queue 不再被 swapchain resize 阻塞。shutdown、device-lost 和全局销毁仍可使用 Device idle。

## FrameScheduler 修正

`wait_for_all_slots()` 允许两种合法调用：frame 尚未开始，或 active frame 已经 submit。active frame 尚未 submit 时调用是内部错误。

frame slot 的 `last_submission_serial` 现在在 `record_submission()` 时写入，而不是推迟到 `end_frame()`。原因是 present 返回
out-of-date 时 recreation 发生在 end_frame 之前；此时 fence 已覆盖本次提交，serial 也必须已经与该 slot 绑定，等待后才能正确推进
completed serial。`end_frame()` 只负责递增 serial 和轮转 slot。

## Queue 回退

`Queue::wait_idle()` 是明确的 queue-local API，使用 `vkQueueWaitIdle` 并对失败强诊断。它只用于当前缺少 present completion token 的
WSI 回退，不回流到资源上传或每帧渲染路径。

## 测试与验证

- FrameScheduler 接口测试固定 all-slot wait 生命周期；
- Queue RAII/接口测试固定 queue-local idle 回退存在；
- Debug 全量构建和完整 CTest 通过。

真实 resize/minimize/present 的 validation layer 验证仍需窗口化 Vulkan 环境。

## 下一步

阶段 4B 完成审计：重点检查 runtime format change 的 Pipeline generation 缺口、Swapchain callback 是否仍有可收敛过渡状态，以及
generation owner 在 shutdown/Deferred/create-failure 三条路径上的一致性。之后进入 Viewport display rect 坐标映射和输入消费边界。
