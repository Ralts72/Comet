# 006 Runtime Resource Ready Completion

## 目标

取消 Mesh/Texture 创建路径中的 CPU 阻塞等待，让 Runtime GPU 资源在上传提交后立即返回，同时保留能够描述“数据何时可
被 GPU 消费”的 `GpuCompletionPoint`。本步先完成资源创建与 ready token 的契约，下一步再把 token 接入帧提交 wait。

## 改动前

Mesh 和 Texture 在构造函数末尾调用 `UploadManager::upload_and_wait()`：

```text
CPU import -> create Runtime resource -> submit upload -> CPU waits timeline -> publish
```

这保证了“构造返回即 ready”，但后台 Mesh 导入完成后仍会在 owner thread 创建 GPU 资源并同步等待，主线程卡顿只是从
CPU 导入阶段转移到了 GPU 上传阶段。AssetManager 也无法把一个尚在上传、但所有权已经安全建立的资源提前发布。

## 改动后

Mesh/Texture 在 enqueue 后调用 `flush_batch()`，保存返回的 `GpuCompletionPoint`，不再等待：

```text
CPU import -> create Runtime resource -> submit upload V -> publish(resource, ready V)
                                             |
                                             +-> UploadManager retains batch until V
```

两类 Runtime Resource 都通过 `get_ready_completion()` 暴露只读 completion。UploadManager 继续持有 staging page、command
context 和目标 Buffer/Image，直到 V 完成，因此候选资源被丢弃或替换也不会提前销毁正在上传的底层对象。

## 为什么 completion 放在 Runtime Resource

ready 状态属于具体 GPU 对象，而不是源资产路径或导入文档：

- AssetManager 只负责按 revision 验票并发布资源，不需要认识 semaphore；
- AssetRegistry 仍是通用的 Handle 到类型化对象映射，不被 Vulkan 同步字段污染；
- Mesh/Texture 即使不经过 AssetManager、由程序直接创建，也具有相同的就绪契约；
- Renderer 从实际要消费的 Runtime Resource 收集依赖，未来可精确区分 VertexInput 与 FragmentShader stage。

没有新增只为某一种资源服务的 manager 或包装对象；复用现有 `GpuCompletionPoint`，只在 Mesh/Texture 各保存一个值。

## 当前正确性

UploadManager 的 CommandContext 与 SceneRenderer 都提交到 `Device::get_graphics_queue(0)`。Vulkan 同一 queue 的 submission
按顺序执行，因此当前实现即使不 CPU wait，draw 也不会越过更早提交的 upload。ready completion 不是为了修补当前同
queue 正确性，而是保留未来 transfer queue、跨 queue 消费和诊断所需的显式依赖。

ResourceManager 增加 `collect_completed_uploads()`，Renderer 每帧开始时调用；UploadManager 在新 staging 分配前也会
主动 collection。这样没有后续上传时，已完成 batch 仍能释放 CommandContext、目标临时引用并把 staging page 放回池。

## 验证

- 编译期接口测试确认 Mesh 与 Texture 都暴露 `const GpuCompletionPoint&`；
- 编译期接口测试确认 ResourceManager 提供完成 batch collection；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 GPU 下观察异步创建期间的 Validation Layer 与帧时间仍属于人工图形验证项，不阻塞本次提交。

## 限制与下一步

- ready completion 还没有进入 RenderSubmission，当前正确性依赖 upload/render 使用同一 graphics queue；
- 每个 Runtime Resource 仍单独 flush，尚未跨资产自动合批；
- 热重载替换旧 GPU 资源仍需要后续 GpuRetirementQueue 保证在途 draw 生命周期；
- completion 是 Queue 所有 timeline 的非拥有引用，Runtime Resource 仍必须早于 Device 销毁。

下一步由 SceneResolver 汇总本帧实际使用的 Mesh/Texture completion，SceneRenderer 在 VertexInput/FragmentShader stage
生成去重后的 timeline waits。完成后即使 UploadManager 切换到 transfer queue，也不需要重新设计 AssetManager 或
AssetRegistry。
