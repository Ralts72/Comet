# 015 GPU 资源生命周期架构复盘

## 范围

本轮不增加渲染功能，而是复盘最近形成的完整链路：

```text
UploadManager submission
  -> Mesh/Texture ready completion
  -> SceneRenderer queue wait
  -> frame submission completion
  -> GpuRetirementQueue / descriptor serial collection
  -> staging cache + memory budget
```

重点检查同一事实是否被多个对象重复保存、通用类型是否放在错误模块、Runtime 头是否携带不必要的底层依赖，以及过渡期
接口是否已经没有调用方。

## 发现与处理

### GpuCompletionPoint 不属于 Queue 头

改动前 `GpuCompletionPoint` 定义在 `graphics/queue.h`。Mesh、Texture、UploadManager 和 GpuRetirementQueue 只需要“某次
GPU 工作何时完成”的 token，却被迫依赖完整 Queue submission 接口。

改动后类型及实现迁入 `graphics/synchronization/gpu_completion_point.h/.cpp`，Queue 只负责创建 token：

```text
Queue --creates--> GpuCompletionPoint <--consumed by-- Upload/Resource/Retirement
```

token 仍然是指向 Queue timeline semaphore 的非拥有引用，生命周期约束没有改变；移动物理位置只收窄依赖，没有创造
新的包装层。

### FrameScheduler 不重复保存 timeline token

FrameSlot 同时保存 in-flight fence、`last_submission_serial` 和 `last_submission` completion，但后者写入后从未读取。
实际职责已经分成：

- FrameScheduler 通过 fence 判断 slot 是否可复用，并用 serial 推进 descriptor 回收；
- SceneRenderer/GpuRetirementQueue 使用 timeline completion 延长实际 draw resource 的所有权；
- UploadManager 使用 upload timeline completion 回收 staging batch。

因此删除 `FrameSlot::last_submission`，把 `record_submission(completion)` 收敛为只表达状态机跃迁的
`record_submission()`。这避免让 FrameScheduler 看起来同时负责 timeline retirement，保留 `last_submission_serial`
作为 fence 完成后推进全局 completed serial 的唯一记录。

### 删除无调用方的过渡接口和状态

- 删除 SceneRenderer 从未读取的 `m_vulkan_config`；
- 删除 `Texture::get_image()`；Texture 只对渲染消费方公开实际需要的 ImageView，底层 Image 所有权继续由 ImageView 保留；
- Mesh 析构改为默认析构，移除与成员自动析构等价的手工 `shared_ptr::reset()`；
- Mesh 公共头改用前置声明，把 Buffer、Device 和 CommandBuffer 的完整依赖移到实现文件；
- 删除只为测试暴露、已有 snapshot 标记可以表达同一事实的 `Allocator::is_memory_budget_enabled()`；
- 修正 FrameScheduler 更名后遗留的日志文本。

## 保留的双重所有权不是重复状态

复盘确认以下引用虽然同时存在，但用途不同，不能合并：

- Material descriptor state 保留 Texture，保证 descriptor 在安全改写/销毁前引用的 ImageView 存活；
- GpuRetirementQueue 保留本帧实际录制的 Mesh/Texture，保证 command buffer 中的裸 Vulkan handle 在 submission 完成前
  存活；
- UploadManager pending batch 保留目标 Buffer/Image 和 staging page，保证 copy submission 自身完成前资源存活。

它们覆盖不同 GPU 使用区间，删除任意一层都会重新引入热重载或异步上传的悬空资源风险。

## 验证

- FrameScheduler 接口测试确认 submission 状态跃迁不再接收 timeline token，FrameSlot 不再公开 `last_submission`；
- 现有 Queue、UploadManager、GpuRetirementQueue 和 Runtime Resource completion 接口测试继续编译通过；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 GPU 上的 timeline wait、热重载 retirement 和 Validation Layer 行为属于人工验证项，不阻塞提交。

## 结论与下一步

当前 GPU 生命周期边界已经形成三条互补而非重叠的完成语义：upload timeline、frame fence serial、runtime retirement
timeline。下一步可以在不破坏这些边界的前提下，为非关键 streaming 资源增加可恢复 allocation 失败路径；关键 render
target、frame resource 和初始化资源继续保留强失败语义。
