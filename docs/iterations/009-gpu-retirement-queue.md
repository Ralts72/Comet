# 009 GPU 资源延迟退休

## 目标

修复热重载替换 Runtime Mesh/Texture 时，CPU owner 可能早于已经提交的 draw 完成而销毁 GPU 对象的问题。新资源的 ready
completion 只约束“何时可以开始使用”；本迭代补上相反方向的“最后一次使用完成前不能销毁”。

## 改动前

AssetRegistry 替换 Mesh 后，如果没有其他 `shared_ptr<Mesh>`，旧 Mesh 会立即析构并销毁 vertex/index Buffer；但上一帧的
command buffer 可能仍在 GPU 上执行：

```text
frame N submit draw(old Mesh) ------ GPU still running
frame N+1 hot reload -> Registry replaces old Mesh -> Buffer destroyed too early
```

Texture 因 per-frame descriptor cache 通常会被间接持有更久，但这种偶然所有权不能作为统一生命周期契约，Mesh 路径也
完全没有等价保护。

## 为什么不在 AssetManager 替换时退休

AssetManager 知道“哪个资产被替换”，但不知道旧资源最后进入了哪个 Queue submission，也不知道它是否被多个 frame slot
或渲染路径使用。让资产层猜测 `N frames later` 会把同步策略绑死在帧数上，并污染当前干净的资产/GPU 边界。

正确位置是 command recording：SceneRenderer 知道本帧真正绑定了哪些 Mesh/Texture，提交后也拿到准确的
`GpuCompletionPoint`。

## GpuRetirementQueue

新增的 `graphics/synchronization/GpuRetirementQueue` 是通用 owner-thread 生命周期工具：

```text
retire_batch(completion V, shared owners)
  -> retain owners
  -> collect_completed(): counter >= V
  -> release entire batch
```

它只保存 `shared_ptr<void>`、completion 和 batch，不认识 Mesh、Texture、AssetHandle、FrameSlot 或 Vulkan handle。未来
pipeline 重建、descriptor 替换、render target generation 等都可以复用同一机制。

相同 completion 的相邻 retire 会合并为一个 batch；已经完成的 completion 不进入队列。析构会等待剩余 completion 后
清空，前提与其他 GPU owner 一样：RetirementQueue 必须早于产生 token 的 Device/Queue 销毁。

## SceneRenderer 接入

`SceneRenderer::render()` 在实际录制每个 draw 时收集 Mesh 与绑定 Texture 的 owning `shared_ptr`，按对象地址去重，避免
同一资源被大量实体引用时增加重复 owner。`end_frame()` 获得 frame completion 后，把整批 owner 移入 retirement queue。

下一帧 `begin_frame()` 会收集已完成 batch。即使 AssetRegistry 随后替换旧资源，所有仍可能访问它的在途 frame batch 都
各自持有一份 owner；最后一个 frame completion 满足后，对象才真正析构。

这份 retention 只覆盖实际 draw 使用的 Runtime GPU 资源。SceneRenderer 自己长期拥有的 pipeline、render target、uniform
buffer 继续由其现有 owner 管理，重建路径当前先 `wait_idle()`，后续可逐步改用同一 retirement queue。

## 验证

- 纯逻辑测试确认空 queue collection 与空 batch 不产生 pending owner；
- 接口测试确认 queue 不可 copy/move，并支持类型安全的 `shared_ptr<T>` retirement；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

需要实际 GPU 热重载与 Validation Layer 的生命周期观察属于人工图形验证项，不阻塞提交。

## 当前限制与后续

- queue 当前由 SceneRenderer 持有，只接入 Mesh/Texture draw owner；
- descriptor/pipeline/render-target 重建仍使用 `wait_idle()`；
- retirement batch 以 submission completion 为粒度，不做单对象更细粒度回收；
- 当前没有内存预算压力反馈或 pending resource 数量遥测。

下一步收敛 FrameManager 为明确的 FrameScheduler contract：在 slot wait 后统一执行 retirement collection、per-frame reset 和
command recording 初始化，并把 frame completion/serial 作为后续 descriptor arena 与 deferred release 的稳定基础。
