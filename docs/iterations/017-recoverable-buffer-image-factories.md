# 017 可恢复的 Buffer/Image 工厂

## 目标

把 Allocator 的显式失败结果提升到 Vulkan owning wrapper 边界，并保证失败时不会先构造一个 handle 为空的
`GPUBuffer`/`OwnedImage` 再要求上层识别其状态。

## 事务式构造顺序

改动前，公开构造函数同时承担参数、VMA allocation 和 wrapper 初始化：

```text
make_shared<GPUBuffer/OwnedImage>
  -> object construction starts
  -> VMA allocation
  -> fill handles
```

这种顺序适合强失败，但无法自然表达可恢复失败。改动后静态工厂统一使用：

```text
validate create parameters
  -> build Vulkan create info
  -> Allocator::try_create_*
     -> failure: return error, no wrapper exists
     -> success: construct wrapper from complete allocation
```

`GPUBuffer` 和 `OwnedImage` 的 allocation-taking 构造函数不再是公共 API，只允许各自基类静态工厂调用。wrapper 一旦可见，
其 Vulkan handle 与 Allocation 必然同时有效；析构逻辑无需增加空对象分支或额外状态枚举。

## 公共接口

- `Buffer::try_create_gpu_buffer(..., within_budget, ...)` 返回
  `ResourceAllocationResult<std::shared_ptr<Buffer>>`；
- `Image::try_create(..., within_budget, ...)` 返回
  `ResourceAllocationResult<std::shared_ptr<Image>>`；
- 原 `create_gpu_buffer()`/`Image::create()` 签名保持不变，内部委托 try 工厂并在失败时输出上下文后强失败；
- CPUBuffer、upload staging、render target 等现有关键调用点没有切换到 recoverable path。

`within_budget` 在 try 工厂上要求显式传入，避免调用方因为默认参数无意中改变预算语义；强失败工厂固定传 `false`，保持原有
“预算是诊断、真实分配错误才失败”的行为。

## 参数错误与运行时错误

零大小 Buffer、零 extent、未定义 Image format 和空 usage 仍是程序员契约错误，会立即强失败。只有 VMA/Vulkan 资源
创建失败进入结果对象。Image 的参数校验发生在 allocation 之前，避免为无效描述做无意义的显存操作。

## 验证

- 编译期接口测试确认 try 工厂返回带 `shared_ptr<Buffer/Image>` 的明确结果；
- 测试确认 null Device 不能调用新工厂；
- 测试确认旧 GPUBuffer/OwnedImage allocation 构造方式不再公开；
- 现有强失败工厂、UploadManager 和 Vulkan RAII 所有权测试保持通过；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实设备 OOM 与 within-budget 拒绝仍属于人工 GPU 验证项，不阻塞提交。

## 当前限制与后续

- Mesh/Texture 构造函数仍调用强失败工厂；
- UploadManager 目前 enqueue 后不能回滚 active batch，因此 Runtime Resource 尝试创建必须先完成全部目标 allocation，再
  开始 enqueue；
- ResourceManager/RenderResourceFactory 尚未返回失败结果；
- 下一步建立事务式 Mesh/Texture 尝试创建，并把错误传到资产发布边界，刷新失败时继续保留旧 Runtime Resource。
