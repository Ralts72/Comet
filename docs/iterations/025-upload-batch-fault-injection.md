# 025 UploadBatch staging 故障注入

## 目标

用确定性、自动化的真实 GPU 测试验证显式 UploadBatch 的核心承诺：一个 batch 的 staging page 增长失败只回滚该 batch，另一
个已经开放的 batch 仍能提交并完成。测试不依赖机器恰好耗尽显存，也不通过非法 Vulkan 参数制造 Validation 错误。

## 注入点设计

UploadManager::CreateInfo 增加可选 `StagingGrowthGuard`：

```text
guard(requested_capacity, within_budget)
  -> nullopt: 允许正常创建 staging page
  -> vk::Result: 在任何 growth side effect 前拒绝本次增长
```

Guard 只在 available/open pages 都无法满足请求、确实需要新 page 时调用。拒绝发生在 memory budget 采样、空闲池压力回收和
CPUBuffer allocation 之前，因此注入失败不会改变共享 pool。生产配置不设置 guard，行为与之前完全一致。

这个 seam 也可用于未来的自定义上传配额策略，但当前唯一调用方是测试。它不进入 Mesh/Texture、RenderResourceFactory 或
AssetManager API，资源发布层仍只看到正常的 `GpuResourceResult`。

## 隔离测试场景

测试使用真实的 GLFW surface、Vulkan Context、Device、VMA Buffer、CommandContext 和 Queue，并把 staging page size 设为
4 bytes。Guard 在第三次 page growth 返回 `eErrorOutOfDeviceMemory`：

```text
Batch B enqueue 4 bytes
  -> growth #1 succeeds; B owns page/context

Batch A enqueue first 4 bytes
  -> growth #2 succeeds; A owns a different page/context

Batch A enqueue second 4 bytes
  -> growth #3 rejected
  -> A auto-aborts and recycles only A resources

Batch B submit
  -> valid timeline completion
  -> wait succeeds
  -> manager collects B normally
```

如果实现仍使用全局 active state，A 的 abort 会同时清除 B，最后的 B submit 无法成功，因此该测试直接覆盖 024 改造的事务
隔离价值。

## 动态库边界修正

测试首次从 unit_testing dylib consumer 直接构造完整初始化链时暴露出一个既有问题：`Window` 和 `Context` 位于公开 engine
头中，却没有 `COMET_API`，macOS 下可编译但无法链接。两类现在补齐导出标记；没有把对应 `.cpp` 重复编入测试目标，避免
两份实现和平台行为分叉。

## 验证

- Debug 全量构建；
- 定向运行 `UploadBatchGpuTest.StagingFailureOnlyAbortsOwningBatch`；
- 断言注入结果保留 `eErrorOutOfDeviceMemory`；
- 断言恰好发生三次 page growth；
- 断言未失败 Batch 的 completion 有效且 wait 成功；
- `ctest --preset dev-debug --output-on-failure`。

测试环境没有 Vulkan/GLFW 支持时 fixture 会显式 skip；CI 按项目既有要求提供 Vulkan 与 Xvfb，因此正常覆盖真实路径。

## 下一步

Upload 事务隔离和失败传播已经具备确定性测试。下一步把 Texture 文件读取/解码迁到 TaskScheduler，并沿用 Mesh 的 revision
验票模型：Worker 只产生 CPU TextureData，Owner Thread 执行 recoverable GPU 创建和 Registry replace，旧 Texture 在候选完成
前继续可用。
