# 005 可复用 Staging Page

## 目标

让 UploadManager 不再为每次 `enqueue_upload()` 单独创建和销毁一个 VMA staging buffer，改为在持久映射的 page 中按
偏移子分配。复用仍以现有 `GpuCompletionPoint` 为安全边界，不在这一迭代同时引入异步资产发布或 transfer queue。

## 改动前

每次 Buffer 或 Image enqueue 都会创建一个大小刚好等于上传数据的 upload buffer：

```text
enqueue A -> VMA allocate staging A
enqueue B -> VMA allocate staging B
flush     -> submit A/B copy
complete  -> destroy staging A/B
```

这种实现的优点是所有权直观，适合作为 UploadManager 的第一版；但大量小 Mesh、Texture 或未来跨资产 batch 会把一次
内存复制放大成一次 VMA allocation，增加 allocator 调用、Vulkan buffer 对象和映射对象的数量。

## 改动后

UploadManager 默认创建 4 MiB staging page，同一 active batch 在已有 page 中执行 4 字节对齐的线性子分配：

```text
available pages
      |
      v
active batch: page [ upload A | padding | upload B | free ... ]
      |
      | flush_batch() -> completion V
      v
pending batch owns page + CommandContext + destinations
      |
      | completed value >= V
      v
page.used = 0 -> available pages
```

如果当前 page 容不下新数据，manager 会从可用池选择容量最小但足够的 page；池中没有合适对象时才创建新 page。单次
上传大于默认值时创建按 4 字节向上对齐的超大 page，完成后同样进入可用池。

## 生命周期与安全边界

- CPU span 仍只在 `enqueue_upload()` 期间借用，返回前数据已写入 page 的持久映射范围；
- active batch 可以在自己的 page 中继续追加，但不会使用 pending batch 的 page；
- `flush_batch()` 把所用 page 与 CommandContext、目标 Buffer/Image 一起移入 pending batch；
- `collect_completed()` 只在 timeline completion 满足后把整页清零并放回池；
- manager 析构时先等待 pending batch，再释放 page，Device 生命周期约束不变。

这不是一个允许多个在途 batch 共享 range 的环形分配器。整页独占会产生少量内部空洞，但避免维护 head/tail、跨页
range 和乱序完成回收，是当前负载下更合理的复杂度边界。

## 接口调整

`CommandContext` 和底层 `CommandBuffer` 的 copy API 现在接受 source buffer offset，Buffer-to-Image copy 也接受
`bufferOffset`。因此多个上传可以引用同一个 Vulkan Buffer 的不同范围。

`Buffer::create_upload_buffer()` 返回 `shared_ptr<CPUBuffer>`，并允许初始数据为空。它仍只负责创建 upload 类型的持久
映射 buffer；范围写入和 page 生命周期由 UploadManager 管理，没有把 pool 策略下沉到通用 Buffer 类。

Buffer copy 的 offset 和 size 必须满足 Vulkan 的 4 字节约束；page 子分配统一保证 offset 对齐，UploadManager 会拒绝
非 4 字节倍数的 Buffer 数据。Image copy 的 buffer offset 使用相同对齐规则。

## 架构价值

page 是 UploadManager 的私有实现细节，没有为 staging 创建 engine 级通用资源类。职责保持为：

- Buffer/CPUBuffer：Vulkan/VMA buffer 与映射写入；
- CommandBuffer/CommandContext：录制带范围的 copy 命令；
- UploadManager：page 策略、batch 编排、completion 回收；
- ResourceManager/AssetManager：分别负责 GPU 资源创建与资产发布，不接触 staging range。

这样减少分配成本的同时，没有把 ImGui、Texture、Mesh 或资产语义带入 graphics 基础层，也为后续跨资产 batch 留出
稳定的 staging 所有权边界。

## 验证

- UploadManager 接口测试确认默认 page 为 4 MiB；
- 编译期接口测试确认 Buffer 和 Buffer-to-Image copy 支持 staging offset；
- 编译期接口测试确认 upload buffer 工厂返回可执行范围写入的 `CPUBuffer`；
- 完整 Debug 构建和 `ctest --preset dev-debug` 覆盖现有 Buffer、Barrier、资源和集成回归。

真实 GPU 下 page offset copy 与 Validation Layer 输出仍属于人工图形验证项，本迭代不以它阻塞提交。

## 当前限制与后续

- page 只在整批完成后回收，尚不支持 batch 内 range 的独立退休；
- 超大 page 会留在池中，尚未增加内存预算、池上限或空闲淘汰；
- Texture 当前仍使用阻塞式 `upload_and_wait()`，资产发布没有携带 ready token；
- 所有上传继续走 graphics queue，暂不引入 queue family ownership transfer。

下一步让 Runtime Resource 的发布结果携带 ready completion，并由首次 graphics 消费建立 GPU-side wait；完成该闭环后，
再依据 profile 决定是否需要跨资产自动 flush、staging ring、内存预算和专用 transfer queue。
