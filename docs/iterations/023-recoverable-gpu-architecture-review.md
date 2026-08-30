# 023 可恢复 GPU 创建阶段性架构复盘

## 复盘范围

本次不扩展新资源类型，检查 016–022 建立的完整链路：

```text
Allocator
  -> Buffer / Image / ImageView
  -> UploadManager
  -> Mesh / Texture
  -> RenderResourceFactory / ResourceManager
  -> AssetManager / AssetRegistry
```

重点审视强失败与可恢复双轨是否重复、结果类型是否越层、事务边界是否真实隔离、日志是否位于正确层，以及测试是否依赖
真实显存状态。

## 已成立的边界

以下结构保持不变：

- 底层工厂先得到完整原生 allocation/handle，再构造 owning wrapper；
- Runtime Mesh/Texture 只有在全部 enqueue 和 flush 成功后才发布；
- recoverable path 不记录缺少资产上下文的底层错误，由 AssetManager 统一添加 Handle 后输出；
- AssetRegistry 只负责成功对象的类型化缓存和替换，不接触创建错误；
- `within_budget` 由 ResourceManager 的资产侧实现固定为 true，AssetManager 不理解 VMA policy；
- 非法尺寸、空数据和不可能的状态映射仍是契约错误，不伪装成普通资源压力。

## 结果类型决策

`GpuResourceResult<T>` 从 Allocator 一直传到 RenderResourceFactory，意味着资产发布边界能看到 `vk::Result`。当前接受这一点，
原因是 Comet 目前只有 Vulkan backend，而这个接口本身就是 CPU asset data 到 GPU Runtime Resource 的边界；再包一层只有同一
错误码的 Asset/Render result 只会制造重复类型。

如果未来引入多个图形 backend，再在 RenderResourceFactory 边界把 backend error 转成稳定的 render-domain error。当前不为
尚不存在的 backend 提前建立第二套 error hierarchy。

`GpuResourceResult` 的默认构造被删除。结果现在必须通过 `success()` 或 `failure(vk::Result)` 显式产生，“变量尚未赋值”不再
被表示成伪造的 `eErrorUnknown`。`failure(eSuccess)` 仍规范化为 unknown，防止失败对象携带成功码。

## ResourceManager 入口收敛

第 022 步为了迁移暂时同时保留了 `ResourceManager::create_*` 和 `try_create_*`。全仓调用检查确认强失败 wrapper 没有生产调用方，
且它会让 AssetManager 之外的代码绕过 budget policy，因此本步删除这两个转发方法。

Mesh/Texture 底层类仍保留共享实现的强失败 `create()` 与可恢复 `try_create()`：这是 GPU resource primitive 自身的完整契约。
ResourceManager 作为当前资产侧组合服务只暴露 RenderResourceFactory 要求的 budget-constrained try path，以及已有
Shader/Sampler/Upload 回收职责，不额外复制无调用方的方法。

## 发现的主要结构风险

UploadManager 当前只有一个隐式 `m_active_context + m_active_resources`。Mesh/Texture 在各自工厂中按 enqueue/flush 形成逻辑
事务，但 API 没有证明 active batch 在进入工厂时为空。如果未来另一个调用方先 enqueue、随后 Mesh staging 失败，
`abort_batch()` 会连同不属于该 Mesh 的工作一起取消；成功 flush 也会把不同调用方的工作合并进同一个 completion。

当前生产调用点严格串行且每个 Runtime Resource 都立即 flush，所以没有已知行为错误，但这个前提只是调用习惯，不是类型或
状态机保证。它是本次复盘后优先级最高的问题。

推荐下一步引入显式、不可复制的 UploadBatch scope：

```text
UploadManager::begin_batch()
  -> UploadBatch owns command context + active resources
  -> batch.try_enqueue(...)
  -> batch.submit() returns completion
  -> destructor aborts only its own unsubmitted work
```

UploadManager 继续拥有 pending batch、staging 空闲池和 completion 回收；UploadBatch 只拥有一次未提交事务。这样资源工厂不会
依赖全局 active 状态，也为确定性的 staging fault injection 提供隔离点。

## 日志与测试结论

- recoverable 底层不主动打 error，避免同一失败在每层重复；AssetManager 输出资产 Handle 和 Vulkan result；
- 强失败工厂仍在最接近资源名称/字节数的位置记录上下文；
- FakeRenderResourceFactory 用显式 out-of-device-memory result 验证发布政策，不依赖测试机器显存；
- 本步测试额外确认 GpuResourceResult 不可默认构造；完整 Debug 构建和全量单元测试保持通过。

## 下一步

先建立显式 UploadBatch 所有权并迁移 Mesh/Texture，再增加 staging allocation fault injection，验证一个 batch 失败不会影响另一
batch。完成后再把 Texture 文件读取/解码迁到 TaskScheduler；否则后台工作增多只会放大当前隐式 batch 的耦合。
