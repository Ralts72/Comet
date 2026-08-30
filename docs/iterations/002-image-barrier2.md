# 002 类型化 Image Barrier2

## 目标

让现有显式 image transition 真正消费第 001 步的 `ImageState`，并将命令录制从 legacy
`vk::ImageMemoryBarrier + pipelineBarrier()` 迁移到 Vulkan 1.3 Synchronization 2 的
`vk::ImageMemoryBarrier2 + vk::DependencyInfo + pipelineBarrier2()`。

本迭代迁移当前唯一的显式 transition 消费者——Texture 阻塞上传。传统 RenderPass 的 attachment initial/final
layout 和 subpass dependency 仍由 RenderPass 自身声明，它们不是旧 `CommandBuffer::pipelineBarrier()` 的调用点，
不在本次强行改写成外部 barrier。

## 改造前

Texture 上传把 Vulkan layout 直接传给 `CommandContext`：

```text
vk::Undefined
  -> CommandBuffer 根据 layout pair 猜 stage/access
vk::TransferDstOptimal
  -> copyBufferToImage
  -> CommandBuffer 再次根据 layout pair 猜 stage/access
vk::ShaderReadOnlyOptimal
```

`CommandBuffer` 只支持五组硬编码 layout pair，subresource 固定为 color、单 mip，并使用 legacy 32-bit stage/access
flags。调用点只表达布局，无法证明底层猜出的同步范围是否符合真实消费者。

## 改造后

Texture 上传现在声明三种语义用途，并解析为完整状态：

```text
ResourceUsage::Undefined
  -> ResourceUsage::TransferDestination
  -> copyBufferToImage(ImageLayout::TransferDstOptimal)
  -> ResourceUsage::SampledRead(FragmentShader)
```

主要职责分为三层：

| 层级 | 职责 |
| --- | --- |
| `resource_state` | 把用途解析为 Vulkan 无关的 Comet stage/access/layout/range 状态 |
| `synchronization/barrier` | 校验前后状态并转换为 `vk::ImageMemoryBarrier2` |
| `CommandBuffer` | 用 `vk::DependencyInfo` 录制 `pipelineBarrier2()` |

`barrier` 采用可复用的自由函数，而不是为 Texture、ImGui 或单一调用点创建 manager/class。未来 UploadManager 和
RenderGraph 的 barrier compiler 可以复用相同的 Vulkan 转换边界，也可以在上层先做 hazard 合并再调用它。

## API 收敛

- 删除 `transition_image_layout(old_layout, new_layout, ...)`，替换为
  `transition_image_state(before, after)`；
- `CommandContext::copy_buffer_to_image()` 改用 Comet `ImageLayout`，Texture 不再接触 `vk::ImageLayout`；
- 删除已经没有消费者的 legacy `pipeline_stage_to_vk()` 和 `access_to_vk()`，只保留 Synchronization 2 转换；
- 保留 CommandBuffer 内部需要的原生 Vulkan image handle，因为它本身就是 Vulkan backend 的命令包装层。

这不是单纯缩短调用点，而是把“业务用途”“同步规则”和“命令录制”分开，使错误可以在纯逻辑层被测试。

## Barrier 校验边界

`build_image_memory_barrier()` 拒绝：

- 前后 subresource range 不一致或范围无效；
- 把 `Undefined`/`Preinitialized` 作为目标 layout；
- access 非空但对应 pipeline stage 为空；
- `Undefined` 初始状态携带虚假的 source access；
- 只有一侧声明 queue owner；
- 前后 queue owner 不同。

最后一条是有意限制。跨 queue-family ownership transfer 必须生成 release/acquire 两个 barrier，并由 semaphore 或
timeline dependency 连接；单个 `CommandBuffer` barrier 无法完整表达这套协议。当前 helper 只负责同一 queue 的状态
转换，owner 变化留给后续具备提交编排能力的 BarrierCompiler/UploadManager，避免提供“能填两个 index 但同步并不完整”
的伪支持。

## 同步等价性

Texture 的两次转换现在为：

| 转换 | source | destination |
| --- | --- | --- |
| Undefined → TransferDestination | stage/access 均为 None | Transfer / TransferWrite |
| TransferDestination → SampledRead | Transfer / TransferWrite | FragmentShader / ShaderRead |

Synchronization 2 允许 `Undefined` 来源使用 `NONE`，无需借用没有实际内存访问含义的 `TopOfPipe`。第二次转换明确写出
Texture 当前由 fragment shader 采样；以后如果 compute 或 vertex shader 消费，调用方必须提供真实 stage。

## 自动化验证

新增 `ImageBarrierTest`，覆盖：

- typed state 到 `ImageMemoryBarrier2` 的 stage/access/layout/range 映射；
- transfer write 到 fragment shader read 的依赖；
- 未声明 owner 和相同 owner 使用 `VK_QUEUE_FAMILY_IGNORED`；
- owner 缺失、owner 变化、范围不一致、缺少 stage 和非法目标状态的拒绝路径；
- `CommandContext` 仍只接受非空 `Image&` 的编译期接口约束。

完整构建和 CTest 是本迭代的自动化验收。带 Validation Layer 启动 editor、观察 Texture 上传与场景呈现属于 GPU/画面
手动检查项，按当前执行约定记录但不阻塞提交。

## 后续边界

- 当前没有显式 buffer barrier 消费者，因此不提前创建无人使用的 `BufferBarrier` API；UploadManager 首次需要 host、
  transfer、graphics 间的 buffer dependency 时再以实际 offset/size 契约补充 `vk::BufferMemoryBarrier2`。
- 传统 RenderPass 的 attachment 生命周期仍由 initial/final layout 和 subpass dependency 表达；在切换 Dynamic
  Rendering 或引入 RenderGraph 前，不把隐式转换机械复制成额外 barrier。
- 下一阶段可建立 timeline semaphore completion value，使上传提交返回可组合的 GPU 完成点，再逐步替换每个 Texture
  独立 `queue.waitIdle()` 的阻塞模型。
