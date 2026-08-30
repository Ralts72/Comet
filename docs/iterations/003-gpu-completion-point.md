# 003 GPU Completion Point 与 Timeline Semaphore 基线

## 目标

建立可跨 frame slot、上传批次和未来多 queue 使用的单调 GPU 完成语义，并让阻塞式上传只等待自己的 submission，
不再对整个 graphics queue 执行 `waitIdle()`。

本迭代只建立 completion 基础设施和现有调用点接入，不创建 UploadManager、staging page 或 retirement queue。

## 改造前

提交接口已经是 Synchronization 2，`QueueSemaphoreSubmit` 也有 `value` 字段，但底层存在契约断层：

- `Semaphore` 始终创建 binary semaphore；
- logical device 只启用了 Vulkan 1.3 `synchronization2`，没有查询和启用 Vulkan 1.2 `timelineSemaphore`；
- binary semaphore 也可以被调用方误传非零 value，Queue 不校验类型；
- `Queue::submit2()` 不返回提交身份；
- Texture/Buffer 的 `CommandContext::submit_and_wait()` 提交后调用 `queue.waitIdle()`，等待范围覆盖整个 queue；
- FrameSlot 只有循环 slot index 和 fence，没有对应的单调 submission token。

也就是说，接口表面上预留了 timeline value，但引擎实际上无法创建或跟踪 timeline 完成点。

## 改造后

### Device feature chain

物理设备候选现在同时要求：

- Vulkan 1.2 `PhysicalDeviceVulkan12Features::timelineSemaphore`；
- Vulkan 1.3 `PhysicalDeviceVulkan13Features::synchronization2`。

查询链和 logical device 启用链都按 `Features2 -> Vulkan12Features -> Vulkan13Features` 连接。Comet 的 Vulkan 1.3
baseline 不代表所有 core feature 自动启用，timeline feature 仍必须显式查询并置为 `VK_TRUE`。

### Semaphore 类型

`Semaphore` 增加 `SemaphoreType::Binary/Timeline`：

- 默认值仍是 Binary，现有 swapchain acquire/render-finished 同步不变；
- Timeline 可以指定初始值，并提供 counter 查询与 host wait；
- binary 初始值必须为 0；
- Queue 提交时校验 binary value 必须为 0、timeline value 必须非 0。

类型仍由同一个 RAII wrapper 管理，避免复制一套 Vulkan semaphore 创建、移动和销毁代码。差异仅限创建参数和 timeline
专属操作，调用边界由运行时类型校验保护。

### Queue completion stream

每个 `Queue` 独占一个 timeline semaphore，并维护从 1 开始的单调 completion value。每次 `submit2()` 都在调用方
signal 列表后追加内部 timeline signal，并返回对应 `GpuCompletionPoint`：

```text
Queue submit N
  -> binary/timeline waits
  -> command buffers
  -> caller signals
  -> queue completion timeline signals value N
  -> return GpuCompletionPoint(timeline, N)
```

completion signal 使用 `AllCommands`，因此该 value 表示本次 submission 的全部命令已经完成。同一 queue 上更大的
value 也覆盖之前更小的 value。

`GpuCompletionPoint` 放在 `queue.h`，因为它就是 Queue submission 的返回值，而不是独立资源系统或新 manager。它保存
内部 timeline 的非拥有指针和 value，提供 `is_valid()`、`is_complete()` 和带 timeout 的 `wait()`；同时可以直接构造
`QueueSemaphoreSubmit`，供未来 graphics queue 以 GPU-side wait 消费 upload completion。

## 生命周期边界

- completion timeline 由 Queue 独占，使用 `unique_ptr` 保证 Queue 在 Device 容器内 move-construct 时 semaphore 地址稳定；
- Queue 禁止 copy 和 move-assign，避免覆盖一个仍被 completion point 引用的 timeline；
- `GpuCompletionPoint` 是轻量非拥有 token，不能比产生它的 Device/Queue 活得更久；UploadManager、FrameSlot 和
  GpuRetirementQueue 都必须位于 Device 生命周期以内；
- `Device` 析构时先等待 idle，再显式销毁 command pool、Queue timeline 和其他 device child，最后销毁 Vulkan device；
- binary frame semaphore 继续由 FrameSlot/SwapchainImageState 持有，timeline 不替代 acquire/present 协议。

FrameSlot 新增 `last_submission`，保存本 slot 最近一次 graphics submission 的单调完成点。fence 仍负责当前 slot 的
CPU 复用；completion point 为后续 last-use retirement 和跨 submission 依赖提供稳定身份，二者职责不同。

## 阻塞上传变化

`CommandContext::submit_and_wait()` 现在：

```text
submit2() -> GpuCompletionPoint -> wait(value)
```

不再调用 `Queue::wait_idle()`。对当前 Texture/Buffer 创建者而言仍是同步返回，行为保持确定性；但等待范围已经缩小到
本次 submission，为后续同一 queue 上的批量和异步上传移除了全队列停顿接口。

## 自动化验证

测试覆盖：

- 缺少 `timelineSemaphore` 或 `synchronization2` 的设备候选会被拒绝；
- 合格候选同时启用 Vulkan 1.2 timeline 和 Vulkan 1.3 synchronization2 feature；
- `Queue::submit2()` 的返回类型是 `GpuCompletionPoint`；
- completion point 可作为 timeline wait 构造 Queue submit 项；
- Semaphore 支持带类型和初始值的构造契约；
- CompletionPoint 的值语义以及 Queue 的禁止复制、仅 move-construct 所有权约束。

完整构建与 CTest 是自动化验收。带 Validation Layer 启动 app/editor，验证真实 timeline semaphore 创建、逐帧提交和
Texture 上传属于 GPU 手动检查项，按当前执行约定记录但不阻塞提交。

## 下一步

下一迭代可以在这份完成语义上建立最小 UploadManager：

1. 明确 `upload_and_wait()`、`enqueue_upload()` 和 `flush_batch()` 的职责；
2. 由 manager 持有 staging allocation 和 upload command buffer，直到 completion point 完成；
3. 多个 Buffer/Texture copy 合并到一次 submission；
4. 异步结果携带 ready completion，首次 graphics consumption 转成 GPU-side wait；
5. 保留启动期和测试使用的阻塞入口，但不再为每个资源创建临时 context 并独立等待。
