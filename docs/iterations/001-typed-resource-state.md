# 001 类型化资源状态

## 目标

为 Synchronization 2 barrier、异步上传和后续 RenderGraph 建立同一份资源状态语言。本迭代只定义并验证状态契约，
不改变现有 Vulkan 命令录制行为；运行路径的 Barrier2 迁移作为下一次独立提交完成。

## 改造前

`CommandBuffer::transition_image_layout()` 接收 Vulkan `old_layout`/`new_layout`，再通过布局组合推断 stage 和
access。这个接口可以覆盖当前 Texture 的 `undefined -> transfer destination -> shader read`，但有几个限制：

- layout 不能完整表达同一布局下不同的读写依赖；
- shader 访问发生在哪些 pipeline stage 只能由底层猜测；
- barrier 固定使用 color aspect、单 mip、单 layer；
- queue-family ownership 没有进入 Comet 的状态模型；
- 继续增加用途会扩展 layout-pair 分支，并把同步规则留在业务调用路径里。

## 改造后

新增 `graphics/synchronization/resource_state`，包含四个相互独立的概念：

| 类型 | 职责 |
| --- | --- |
| `ResourceUsage` | 调用方声明资源用途，例如传输、采样、storage、attachment 和 present |
| `ResourceState` | 描述 pipeline stage、access mask 和 queue-family owner |
| `ImageSubresourceRange` | 描述 aspect、mip 和 array-layer 范围 |
| `ImageState` | 在通用资源状态上补充 image layout 与 subresource range |

`resolve_resource_state()` 和 `resolve_image_state()` 将常见 usage 解析为确定的 Comet 强类型状态。转换到 Vulkan
`vk::*Flags2` 的工作仍属于 backend，调用方不需要接触 Vulkan barrier 结构。

## 关键设计选择

### `ResourceUsage` 不复用创建用途

Image/Buffer 创建时的 usage flags 表示“对象一生允许做什么”；本次的 `ResourceUsage` 表示“本次操作正在怎样访问”。
二者生命周期和语义不同，合并后无法准确生成同步依赖。

### shader usage 必须提供 stage

`SampledRead`、`UniformRead`、`StorageRead` 和 `StorageReadWrite` 不默认猜成 fragment shader。调用方必须显式给出
vertex、fragment、compute 等实际消费 stage；传入非 shader stage 或遗漏 stage 会返回失败。固定流水线用途反过来
不允许夹带 shader stage，避免状态看似有效但语义矛盾。

### owner 与 subresource 是状态的一部分

queue family 默认使用 `UNSPECIFIED_QUEUE_FAMILY`，当前单 graphics queue 路径可以暂不声明 owner；以后启用独立
transfer queue 时，状态本身已经可以表达 ownership。Image 状态始终携带 aspect、mip 和 layer 范围，不在 `Image`
对象中保存一个含义模糊的全局 current layout。

### 类型归入 synchronization 目录

这些结构不拥有 GPU Resource，也不是 Texture 或 Image 的数据成员；它们描述命令之间的同步契约，因此和 Fence、
Semaphore 一样归入 `graphics/synchronization/`。后续 CommandBuffer、UploadManager 与 RenderGraph 可以共同复用，
不会形成只服务于某一条 Texture 或 ImGui 路径的类型。

## 校验边界

解析函数会拒绝以下不完整或矛盾的输入：

- shader usage 没有明确 shader stage，或混入 transfer 等非 shader stage；
- 固定流水线 usage 额外传入 shader stage；
- image subresource 缺少 aspect，或 mip/layer 数量为零；
- buffer-only usage 被解析为 Image 状态；
- color/present usage 使用 depth aspect，或 depth/stencil attachment usage 使用 color aspect。

`ColorAttachmentWrite` 同时包含 attachment read/write access，以覆盖 load 和 blending 对目标的读取；storage image
使用 `General` layout，深度只读状态使用 `DepthStencilReadOnlyOptimal`。

## 自动化验证

`ResourceStateTest` 覆盖：

- buffer 与 image 常见 usage 的 stage/access/layout 映射；
- sampled/storage shader stage 的显式约束；
- color、depth write、depth read 和 present 状态；
- queue owner 与非默认 mip/layer 范围保持；
- 空范围、错误 aspect 和 buffer/image 用途混用的拒绝路径。

本次不改变运行时 Vulkan 调用，因此没有新增必须手动完成的画面或 GPU 验证项。

## 下一步

下一迭代将让 `CommandBuffer` 接收 known-before 和 desired-after `ImageState`，并完成：

1. `PipelineStage`/`Access` 到 Vulkan Synchronization 2 flags 的转换；
2. 使用 `vk::ImageMemoryBarrier2` 和 `vk::DependencyInfo` 录制 barrier；
3. 将现有 Texture 阻塞上传改为 `Undefined -> TransferDestination -> SampledRead` 的显式状态转换；
4. 删除 layout-pair 推断和 legacy `pipelineBarrier()`；
5. 保持现有上传行为，并通过自动化测试与 validation 手动检查项验证。

这份状态契约随后会继续被 UploadManager 的 batch tracker、持久资源 handoff state 和 RenderGraph barrier compiler 复用。
