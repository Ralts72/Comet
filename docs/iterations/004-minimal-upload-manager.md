# 004 最小 UploadManager 与批量提交

## 目标

把 Buffer/Texture 各自实现的 staging、copy、image barrier、提交和等待收敛到可复用的 UploadManager，并让一个
Mesh 的 vertex/index 数据至少合并为同一次 Queue submission。

本迭代保持 ResourceManager 的同步“创建后即可使用”契约，但内部已经具备 `enqueue_upload()`、`flush_batch()`、
completion 持有和回收边界，为后续异步发布与 staging page 复用打底。

## 改造前

上传职责分散在 Runtime Resource 内部：

- `GPUBuffer` 构造函数同时创建目标 buffer、创建临时 VMA upload buffer、复制 CPU 数据、创建 CommandContext、
  submit/wait，再销毁 staging；
- `Texture` 重复同一套流程，并额外自行录制两次 image transition；
- 一个带 index 的 Mesh 构造 vertex/index 两个 `GPUBuffer`，因此创建两个临时 CommandContext、两次 submission 和
  两次 CPU wait；
- staging 和 command buffer 的生命周期只靠局部同步等待保证，无法安全返回异步 ready token；
- `ResourceManager::create_texture/create_mesh()` 被声明为 `const`，但实际创建 GPU 资源本身就是有状态操作。

这能满足同步 demo，但每增加一种资源就可能复制上传模板，也无法集中做批量、回收和统计。

## 改造后

### 职责拆分

```text
ResourceManager
  -> 创建 Runtime Texture/Mesh
      -> Buffer/Image 只分配目标 GPU 对象
      -> UploadManager enqueue CPU bytes + owning destination
          -> staging buffer
          -> shared CommandContext records copy/barrier
  -> upload_and_wait() / flush_batch()
      -> Queue submission
      -> GpuCompletionPoint
      -> completion 后释放 staging、command context 和临时目标引用
```

`GPUBuffer` 现在只负责创建带 `CopyDst` 的 device-local buffer，不再接受初始数据或调用 CommandContext。Buffer
allocation 与内容上传由此成为两个明确阶段。Buffer enqueue 还必须提供最终 `ResourceState`；UploadManager 在 copy 后
生成 `Transfer/TransferWrite -> VertexInput/VertexAttributeRead` 或 `IndexRead` 的 `BufferMemoryBarrier2`，不会把 Queue
完成误当成 transfer write 到后续读取的内存依赖。

Texture 仍负责验证尺寸/像素和声明最终 `SampledRead(FragmentShader)` 状态，但 transfer destination 状态、staging、
copy 和 Barrier2 由 UploadManager 统一生成。Mesh 在 enqueue vertex 和可选 index 后只调用一次
`upload_and_wait()`，两次 copy 进入同一个 command buffer 和 submission。

### UploadManager API

- `enqueue_upload(Buffer, bytes, final state)`：复制进 staging，录制 buffer copy 和 transfer-write 到最终读取状态的
  Buffer Barrier2；
- `enqueue_upload(Image, bytes, before, after)`：生成 TransferDestination 中间状态，录制前后 Barrier2 和 image copy；
- `flush_batch()`：提交当前 batch，返回 `optional<GpuCompletionPoint>`，不做 CPU wait；
- `upload_and_wait()`：为启动期、同步 ResourceManager 和测试提供 flush + completion wait；
- `collect_completed()`：只回收 timeline 已完成的 pending batch。

CPU span 只在 `enqueue_upload()` 调用期间使用，数据在返回前已经复制到 staging，因此 importer 的临时 vector 不需要
延长生命周期。

## 所有权与生命周期

每个未完成 batch 显式持有：

- 已提交的 `CommandContext`，避免 GPU 执行前释放 command buffer；
- staging `Buffer`；
- 目标 `Buffer`/`Image` 的 `shared_ptr`，避免只有 command buffer 中的裸 Vulkan handle 存活；
- 对应 `GpuCompletionPoint`。

`collect_completed()` 只在 completion 满足后销毁整批 owner。UploadManager 是 ResourceManager 的独占成员，运行在当前
GPU owner thread；它不进入 Device，因为 Device 只提供 Vulkan 基础能力，不应反向拥有 Runtime Resource 创建策略。

UploadManager 放在 `graphics/command/`，因为它编排 copy/barrier command 与 submission lifetime，不认识 AssetHandle、
Importer 或 Texture/Mesh 资产语义。它不是为 ImGui 或单一资源类型创建的辅助类。

`GpuCompletionPoint` 仍是非拥有 token；UploadManager 自身以及返回 token 的消费者都必须位于 Device 生命周期以内。
析构时 manager 会等待已提交 batch；未 flush 的录制 batch 被诊断并丢弃，不在析构阶段隐式提交新的 GPU 工作。

## 当前显式限制

首版有意保留以下限制：

- 只使用现有 graphics queue，不引入 transfer queue 和 ownership transfer；
- image upload 只接受完整 color、mip 0、单 layer subresource，并要求目标带 `CopyDst`；
- 每个 enqueue 仍单独分配一个 staging Buffer，尚未实现 page/ring 复用；
- ResourceManager 创建单个资源后仍同步等待，尚未把 pending ready token 发布给 Asset Registry；
- batch 已能合并一个 Mesh 内的多次 copy，但还没有跨多个资产的公开批量创建入口。

这些限制都通过接口边界明确表达，避免首版对 mip、array 或异步可用性提供不完整支持。

## 自动化验证

新增接口测试覆盖：

- UploadManager 只接受持有目标生命周期的 `shared_ptr<Buffer/Image>`，拒绝裸 Buffer 指针；
- Buffer barrier 的 stage/access/offset/size 映射及非法 scope、空范围和 queue owner 变化拒绝；
- `flush_batch()` 返回 `optional<GpuCompletionPoint>`；
- CommandContext 的非阻塞 `submit()` 返回 `GpuCompletionPoint`；
- UploadManager 是 owner-thread 独占、不可 copy/move 的生命周期对象；
- device-local Buffer factory 不再接收 CPU data，验证 allocation 与 upload 已分离；
- AssetManager fake factory 同步更新为非 const 创建契约。

完整构建和 CTest 是自动化验收。带 Validation Layer 启动 app/editor，确认 Mesh/Texture 上传、场景绘制以及销毁期间没有
command buffer、staging 或 image layout 诊断，属于已记录但不阻塞提交的 GPU 手动检查项。

## 下一步

下一迭代应优先减少上传分配和接通真正的异步 ready 语义：

1. 用可复用 staging page/ring 替换每个 enqueue 一个 VMA Buffer；
2. 为 batch 内子分配记录 offset，并让 buffer/image copy 使用对应 source offset；
3. `flush_batch()` 后由 owner thread 发布携带 ready completion 的 Runtime Resource；
4. 首次 graphics consumption 通过 `QueueSemaphoreSubmit(completion, stage)` 建立 GPU-side wait；
5. completion 后回收 staging range 和 upload command context，不通过 CPU wait 才允许资源使用。
