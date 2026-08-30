# 019 事务式 Runtime Mesh 创建

## 目标

把底层已经具备的可恢复 Buffer allocation 与 UploadManager batch abort 契约组合成完整的 Runtime Mesh 创建事务。
调用方要么拿到一个拥有 vertex/index buffer 和 ready completion 的完整 Mesh，要么拿到 Vulkan 错误；失败路径不发布
半初始化 Mesh，也不保留未提交的上传命令。

## 原有创建顺序的问题

原 Mesh 构造函数在对象已经开始构造后依次执行以下操作：

```text
构造 Mesh
  -> 创建 vertex buffer
  -> enqueue vertex upload
  -> 创建 index buffer
  -> enqueue index upload
  -> flush
```

这个顺序适合所有失败都直接终止进程的早期实现，但无法承载后台流送或热重载需要的可恢复失败。比如 vertex upload 已经
录制后 index buffer allocation 失败，调用方既拿不到有效对象，也缺少明确的事务边界来取消前半段工作。

## 新创建契约

Mesh 不再公开执行 GPU 工作的构造函数，而是提供两条共享实现的静态工厂：

- `Mesh::create()`：关键资源的强失败入口，允许 allocation 超过预算，失败时记录具体 Vulkan error 并终止；
- `Mesh::try_create()`：非关键流送入口，由调用方选择 `within_budget`，失败时返回 `GpuResourceResult`。

真正的 Mesh 构造函数只接收已经完整创建的 Buffer、数量和 completion，并保持私有。这样“C++ 对象存在”本身就表示其
GPU owner 与 ready token 已经齐全，而不是表示一段仍可能失败的创建过程正在发生。

## 事务顺序

新的 `try_create()` 固定采用以下顺序：

```text
校验 MeshData / 解析目标 ResourceState
  -> 尝试创建全部目标 Buffer
  -> 尝试 enqueue 全部上传
  -> flush 一次 batch
  -> 用完整资源构造并返回 Mesh
```

vertex 和可选 index buffer 会在任何 enqueue 之前全部分配。目标 allocation 失败时，局部 shared owner 直接释放，不存在
active upload。staging enqueue 失败时，UploadManager 会 abort 整个 active batch，撤销此前录制的 copy 并释放 batch
引用。只有所有 enqueue 成功后才 flush；submission 产生 completion 后才构造 Mesh。

空 vertex、超过 `uint32_t` 的 draw count、无法解析的固定资源状态仍属于输入或引擎契约错误，继续强失败。Queue submit
失败也继续由 UploadManager 强失败处理；这类错误不能仅靠保留旧资产安全恢复。

## ResourceManager 边界

`ResourceManager::create_mesh()` 仍维持原有 `RenderResourceFactory` 强失败接口，但改为调用 `Mesh::create()`，不再直接
选择具体构造函数。这样本步只稳定 Runtime Mesh 自身的创建不变量；后续再为资源工厂增加 recoverable 返回值，并由
AssetManager 决定“创建失败时保留旧 Runtime Mesh”的发布策略。

## 验证

- 编译期接口测试确认 Mesh 暴露 `try_create()`，返回 `GpuResourceResult<std::shared_ptr<Mesh>>`；
- 编译期测试确认旧的公开 GPU 工作构造函数不再可调用；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

可恢复 allocation/staging 失败需要可控的 Vulkan/VMA 故障注入或真实显存压力，仍作为人工或后续 fault-injection 测试项，
不通过依赖特定 GPU 容量的脆弱自动化测试模拟。

## 当前限制与下一步

- Texture 还没有同样的事务工厂，其 ImageView 原生句柄创建也尚未提供可恢复结果；
- `RenderResourceFactory` 仍只暴露强失败 `create_mesh()`，AssetManager 暂时无法消费本步的错误结果；
- 下一步先给 ImageView 建立完整句柄或错误的静态工厂，再用同一事务顺序收敛 Texture。
