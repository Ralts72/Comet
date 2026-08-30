# 041 Swapchain Generation 边界审计

## 目标

在创建 SwapchainGeneration 类型前，先核对当前 swapchain core 与 dependent 的真实生命周期，并修复不依赖新架构也必须成立的
Vulkan 父子销毁顺序。本步保留 Device idle，不尝试同时解决 core 创建事务和 present completion。

## 审计结果

当前资源关系是：

```text
Swapchain core handle/images
├── runtime SwapchainTarget
│   └── framebuffer -> image view -> borrowed swapchain image
└── editor ImGui swapchain target
    └── framebuffer -> image view -> borrowed swapchain image

FrameScheduler SwapchainImageState
└── render-finished semaphore + in-flight frame-slot association
```

旧流程先在 `Swapchain::recreate()` 内替换 images 并销毁 old swapchain，返回后才重建 SceneRenderer target、FrameScheduler image state 和
ImGui target。`Device::wait_idle()` 只能证明 GPU/queue 不再执行这些对象，不能改变 Vulkan 对象依赖：framebuffer/image view 仍应在其
引用的 swapchain image 和父 swapchain 之前销毁。因此旧流程即便没有并发使用，也存在反向析构窗口。

另外确认了几个不能混在本步草率修补的问题：

- `Swapchain::recreate()` 仍直接覆盖 active handle/images/config，get-images 或包装阶段失败时不是完整事务；
- 新 swapchain handle 创建成功后 old handle 已进入 retired 语义，后续 dependent 创建失败不能重新 acquire old；
- graphics frame fence/timeline 不证明 presentation engine 已释放 presentable image；
- RenderPass/Pipeline/ImGui backend 是否兼容必须比较 format、sample count 和 image count，不能只看 extent；
- present completion 不可用的平台最终需要 present-queue idle 回退，而不是无关的全 Device idle。

## 本步改造

`SceneRenderer` 现在提供成对的 swapchain resource callbacks，并统一执行：

```text
Device idle
  -> release runtime SwapchainTarget
  -> release editor ImGui swapchain target
  -> recreate core swapchain
  -> rebuild runtime target
  -> rebuild FrameScheduler per-image state
  -> rebuild editor ImGui target/backend if needed
```

Editor 不再收到一个含义模糊的“swapchain 已重建”通知；`ImGuiContext` 将原 `recreate_swapchain()` 拆成
`release_swapchain_resources()` 和 `rebuild_swapchain_resources()`。viewport 离屏 image/descriptor 与 swapchain 无关，不在 release 阶段
销毁；只释放直接引用 present images 的 ImGui target。

窗口最小化时 `Swapchain::recreate()` 返回 Deferred，旧 core 没有被修改。此时 SceneRenderer 会立即用旧 core 重建刚释放的 dependent，
再返回 false 跳过当前帧，保证对象始终处于“旧 core + 旧 dependent”或“新 core + 新 dependent”的完整组合，而不是 target 为空的
半状态。

Editor shutdown 在销毁 ImGuiContext 前把两个 callback 清空，避免 SceneRenderer 留下捕获已析构 editor 对象的 delegate。

## 为什么还不是最终 Generation

本步的 release/rebuild callback 是组合根连接 engine runtime 与 editor dependent 的窄边界，不是退休机制。它仍有两个明确限制：

- SceneRenderer 和 Swapchain 当前都会等待 Device idle，属于过渡期重复等待；
- core 仍原地更新，所有 dependent 也仍同步重建，不能在候选完整后一次性 commit。

下一步先让 core 创建成为局部候选事务，在 active 状态之外完成 config、handle 和 borrowed images；只有 core 候选成功才开始不可逆的
dependent generation 阶段。再下一步才用 graphics completion + present completion/queue-idle fallback 替换全局 idle。

## 测试与验证

- `ResourceReadinessTest` 固定 SceneRenderer 必须暴露成对的 swapchain resource lifecycle callback；
- Debug editor/app/engine 全量构建；
- 完整 CTest。

真实 resize、最小化/恢复、surface format 变化和 validation layer 生命周期诊断需要窗口化 Vulkan 环境，保留为后续集成验证，不用
无 GPU 的接口测试冒充。
