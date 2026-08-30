# 008 上传与 Ready 链路架构复盘

## 复盘范围

本次不增加新的渲染能力，而是回顾 001—007 建立的类型化状态、Barrier2、timeline completion、UploadManager、staging
page 和 Runtime Resource ready wait 链路，重点检查职责归属与高频路径成本。

## 发现的问题

第 007 步把 `RenderResourceWait` 放进 RenderSubmission，并由 SceneResolver 指定 `VertexInput`、`FragmentShader`：

```text
SceneResolver -> resource + backend stage -> RenderSubmission -> SceneRenderer
```

这在当前固定 cube pipeline 下可工作，但 stage 实际取决于 Renderer 如何绑定资源。未来同一个 Texture 可能用于 fragment、
compute 或 vertex shader；让场景解析层决定 stage 会迫使它了解 pipeline/reflection，形成错误的反向依赖。

同时，旧实现会先对每个 RenderItem 的每个资源调用 `GpuCompletionPoint::is_complete()`，再按 semaphore 去重。如果一千个
实体复用同一个 Mesh 和 Material，一个 frame 可能产生数千次 timeline counter 查询，而真正需要查询的 timeline 只有一个。

## 整理后的结构

```text
SceneResolver
  -> RenderSubmission: camera + resolved Mesh/Material only

SceneRenderer::render
  -> observes actual Mesh/Texture bindings
  -> assigns VertexInput / FragmentShader stage
  -> merges waits by timeline semaphore and max value
  -> queries completion once per unique timeline
  -> returns QueueSemaphoreSubmit list

Renderer
  -> SceneRenderer::end_frame(resource waits)
```

RenderSubmission 因此恢复为 backend 无关的解析结果，不重复保存已经附着在 Runtime Resource 上的 completion。SceneRenderer
同时拥有 pipeline 使用方式和 frame submit 编排，是把 ready completion 编译成 Queue wait 的唯一合理位置。

## 性能变化

等待合并现在发生在 completion 查询之前：

- 同一资源被多个实体复用，只会保留相同 timeline 的最大 value；
- 同一 upload batch 的 Mesh/Texture completion 会合并；
- stage mask 取所有实际用途的并集；
- 每个唯一 timeline 每帧最多调用一次 `getSemaphoreCounterValue()`；
- 已完成 timeline wait 在 frame submit 前删除。

当前通常只有 graphics queue 自己的一个 timeline，因此查询次数从“与可见实体资源引用数成正比”降为最多一次。

## 保留的边界

- Mesh/Texture 仍只暴露 ready completion，不知道消费 stage；
- AssetManager/AssetRegistry 不接触 semaphore；
- UploadManager 只管理上传 batch 和 completion 回收；
- Queue 仍是 semaphore 类型/value/stage 校验与 `VkSubmitInfo2` 构造边界；
- 没有创建新的通用 manager、wrapper 或独立数据文件。

## 验证

- 接口测试确认 `SceneRenderer::render()` 返回类型化 `QueueSemaphoreSubmit` 列表；
- 接口测试确认 `end_frame()` 接收这些 waits；
- 接口测试确认 RenderSubmission 不再保存 backend wait；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 GPU Validation 和 profile 仍是人工检查项，不阻塞本次整理。

## 后续

上传与 ready 子系统现在形成清晰闭环，下一阶段可以独立实现 GpuRetirementQueue。它解决的是相反方向的生命周期问题：
ready token 保证新资源不会过早使用，retirement completion 保证旧资源不会在在途 draw 完成前销毁。
