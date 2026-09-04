# 渲染资源所有权

本文记录 Comet 当前渲染基础设施的所有权和生命周期约束。它描述的是资源由谁销毁，不代表 Scene、Asset 和 RenderItem 的最终 API。
阶段 0 改造的背景、帧同步原理和代码前后对比见
[阶段 0 渲染基础改造详解](./rendering-foundation-phase-0.md)。

## 所有权结构

```text
Engine
├── Scene
├── AssetRegistry
│   └── Runtime Mesh/Texture/Material assets
└── Renderer
    ├── RenderContext
    │   ├── Context
    │   ├── Device
    │   │   ├── Allocator
    │   │   └── default CommandPool
    │   └── Swapchain
    ├── ResourceManager
    │   ├── UploadManager
    │   ├── ShaderManager
    │   └── SamplerManager
    ├── SceneResolver
    └── SceneRenderer
        ├── RenderPass/PipelineManager/Pipeline
        ├── FrameScheduler
        │   ├── FrameSlot[frames-in-flight]
        │   │   └── RetainedResources
        │   └── SwapchainImageState[swapchain images]
        ├── ViewProjectBuffer[frames-in-flight]
        ├── RenderTarget
        │   ├── runtime: SwapchainTarget[swapchain image]
        │   └── editor: MultiTarget[frame slot]
        └── MaterialDescriptorState[material][frame slot]

Editor
├── SceneDocument (New/Open/Save + current path)
├── EditorSceneSession (Edit/Play scene switching)
└── ImGuiContext
    ├── ImGui RenderPass/SwapchainTarget
    ├── DescriptorPool
    └── ImGuiContext::TextureBinding[frame slot]
        ├── ImageView → Image
        ├── Sampler
        └── ImGui descriptor
```

`Engine` 独占 Scene 和 Asset Registry，app 和 editor 负责创建或修改场景内容。app/editor 持有项目级
`AssetManager`，并在 Engine 销毁前释放它；`AssetManager` 持有 Asset Database，对 Asset Registry 和
由 ResourceManager 实现的 `RenderResourceFactory` 只保存非拥有引用。Asset Registry 以
`AssetHandle` 保存运行时资源的共享引用，Scene 组件只保存 Handle。`Renderer` 是当前渲染子系统的组合根。
`RenderContext` 独占 Vulkan Context、Device 和 Swapchain；`Device` 独占 `Allocator`。`ResourceManager` 与
`SceneRenderer` 分别保存对 Device 和 RenderContext 的非拥有引用，构造接口不允许空依赖。

渲染和 graphics 层统一使用以下依赖表达：

- `T&` / `const T&` 表示非拥有、构造时必须存在、构造后不可重绑定的依赖。
- `T*` 只用于允许为空、需要重绑定或 moved-from 状态需要置空的对象；当前主要包括 `Entity` 的可空 Scene，
  以及可移动 Fence/Semaphore 内部保存的 Device。
- `std::unique_ptr` 表示独占所有权，`std::shared_ptr` 表示共享生命周期；引用和裸指针都不会延长依赖生命周期。
- GLFW、Vulkan 等 C API handle 保持各自的原生值或指针形式，不属于 C++ 对象借用约定。

runtime 使用 `SwapchainTarget` 直接呈现场景。editor 使用按 frame slot 分配的 `MultiTarget` 生成离屏颜色纹理，
`ImGuiContext` 通过私有纹理绑定持有离屏 `ImageView` 和 `Sampler` 的共享引用，并拥有对应的 ImGui descriptor；
最终 swapchain 只由 ImGui render pass 清屏、合成和呈现。editor 只有一个 Viewport，Edit/Play 复用同一组离屏输出
并切换活动 Scene 与交互状态。

## 生命周期约束

关闭时必须遵循以下顺序：

1. editor 先等待 Device idle，释放 ImGui viewport descriptor、ImGui swapchain target 和 descriptor pool。
2. Engine 在清理 Asset Registry 前等待 Device idle，保证 Registry 可能释放的 GPU 资产不再被提交引用。
3. Asset Registry 释放对 Runtime Mesh/Texture/Material 的 Handle 缓存及其依赖共享引用。
4. 释放 SceneRenderer，确保 per-frame Buffer、pipeline、descriptor、render target、command buffer 等对象先于 Device 销毁。
5. 释放 ResourceManager 持有的 ShaderManager、SamplerManager、UploadManager pending batch 和对应设备级资源。
6. 释放 RenderContext：Swapchain → Device 内部的 CommandPool、Queue timeline、PipelineCache 和 Allocator →
   Vulkan Device → Context。
7. 释放 Scene；Scene 只持有组件和 AssetHandle，不拥有 GPU 资源。

任何 `Buffer`、`OwnedImage`、`Texture`、`Mesh` 或其他 VMA 资源都不得比创建它的 Device 活得更久。`BorrowedImage` 只包装外部 image，不负责释放该 image。
`ImageView` 持有父 `Image` 的共享引用，并在释放该引用前销毁原生 image view；因此任何持有 `ImageView` 的消费者都会
自动延长对应 C++ `Image` 对象的生命周期。ImageView 通过强失败/可恢复静态工厂创建，原生 view handle 成功后才构造
wrapper，失败时不会发布空句柄对象。`BorrowedImage` 对应的原生 image 生命周期仍由 Swapchain 等外部所有者负责。
`FrameBuffer` 持有全部 attachment `ImageView` 的共享引用，并在释放 attachment 前销毁原生 framebuffer，
由此形成 `FrameBuffer → ImageView → Image` 的完整所有权链。`Texture` 和 `RenderTarget` 不再并行保存同一资源的
`Image`/`ImageView` 共享引用；需要 image 时统一通过 `ImageView::get_image()` 访问。

`ResourceState`/`ImageState` 是不拥有 GPU 对象的同步描述，不作为 `Image` 或 `Buffer` 的全局 current state 成员。
状态跟踪属于 command recording、UploadManager 或 RenderGraph 的编译上下文；持久资源只在明确的提交边界交接
handoff state。这样可以分别表达不同 mip/layer 的状态，也不会把已录制、已提交和正在执行的状态混成单一 CPU 值。

底层 Vulkan 包装按职责位于 `graphics/command/`、`graphics/resource/`、`graphics/pipeline/` 和
`graphics/synchronization/`。Context、Device、Queue、Swapchain、RenderPass 和 FrameBuffer 会跨越多个职责组，
因此保留在 `graphics/` 根目录，不用物理目录伪造不存在的单向依赖。

## 职责边界

- `RenderContext`：Vulkan 上下文、逻辑设备、交换链和 idle 等待。
- `RenderResourceFactory`：向资产层暴露从 CPU `TextureData`/`MeshData` 尝试创建 Runtime 资源的窄接口；返回类型化 GPU
  错误，不决定 Registry 发布或旧资源保留策略。
- `ResourceManager`：创建 Device 相关的 Texture/Mesh，独占 UploadManager，并维护 Shader/Sampler 等设备级共享资源；
  不认识或缓存 `AssetHandle`。它通过 RenderResourceFactory 只暴露资产侧 `try_create_*`，并固定遵守 memory budget；
  Mesh/Texture primitive 自身保留强失败工厂，但 ResourceManager 不复制无调用方的强失败转发入口。
- `UploadManager`：在 owner thread 从可复用 staging page 子分配上传范围，合并 buffer/image copy 与 Barrier2；每个
  pending batch 独占所用 page、CommandContext 和目标资源直到 timeline completion，随后有界缓存默认尺寸页并释放
  临时超大页。它不认识 AssetHandle、Importer 或资产发布策略。
- staging 空闲池最多缓存配置数量的默认 page，超大 page 不进入长期缓存；只有 best-fit 失败、池即将增长时才查询
  memory budget，高压力时可销毁空闲页，但绝不回收 active/pending batch 拥有的页。
- UploadManager CreateInfo 可选的 staging growth guard 在任何增长副作用前允许或拒绝新 page；生产默认不设置，自动化测试用
  它确定性注入 allocation error，验证 batch 隔离而不依赖真实显存耗尽。
- recoverable `UploadBatch::try_enqueue_upload()` 在 staging 失败时只 abort 自身：先 discard 自己的未提交
  CommandContext，再释放目标引用并回收自己的 page；其他 open batch 不受影响。pending batch 已有 completion，不能
  abort，只能等待正常回收。
- `Mesh` / `Texture`：持有 Runtime GPU 对象和创建它们的 ready completion；创建返回不等待 CPU。两者都通过静态工厂先
  完成全部目标 GPU owner，再以可回滚 batch enqueue，提交成功后才发布 Runtime wrapper；其公开对象不表达“正在构造”
  的中间状态。当前 upload 与 draw 使用同一 graphics queue；SceneRenderer 根据实际绑定用途把 completion 编译为准确
  stage 的 timeline wait，因此未来切换 transfer queue 不改变资源与资产接口。
- `FrameScheduler`：拥有 FrameSlot 轮转、fence 等待、swapchain image 关联、submission serial 和当前 slot 的
  RetainedResources；实际 draw 的 Mesh/Texture owner 在该 slot fence 完成后统一释放。
- `AssetManager`：按 `AssetHandle` 协调 Asset Database、Importer、依赖解析、运行时 Material 组装和 Asset Registry 发布；不拥有 Device 或 GPU 资源。Runtime Mesh/Texture 创建失败时记录具体结果，首次加载不注册，刷新不替换旧对象。
- `SceneResolver`：选择并校验主 Camera，根据 RenderTarget 尺寸生成 view/projection，将 Handle 解析为运行时 Mesh 和材质绑定，并集中处理可恢复诊断。
- `SceneRenderer`：消费包含可选 view/projection 的整批 RenderSubmission，管理 per-frame uniform buffer、render target、pipeline、descriptor 和 draw command 录制；从实际 Mesh/Texture 绑定汇总 ready wait，并向 FrameScheduler 登记当前帧使用的 owner；没有有效主 Camera 时不提交场景 draw。
- `ViewPanel`：拥有面板逻辑尺寸和 resize debounce；尺寸连续稳定后只产生一次 resize request。Renderer 不保存编辑器面板的稳定帧状态，只在请求到达后等待相关 FrameSlot 并调整离屏 RenderTarget。
- `ImGuiContext`：拥有 editor 最终呈现所需的 render pass、swapchain target 和 viewport descriptor；通过私有绑定共享
  SceneRenderer 的离屏 `ImageView` 生命周期，但不创建或直接销毁这些 engine 图形资源。
- `Scene`：只保存实体、可序列化组件和 `AssetHandle`，不保存 Device、GPU对象或文件路径。
- `AssetRegistry`：唯一按 `AssetHandle` 缓存、注册和解析已发布运行时资源；不保存源路径或执行导入。

Texture/Mesh DTO、Runtime 类型和创建边界集中在 `engine/src/render/resource/`；Material 保留在渲染语义层，不归入设备资源创建子目录。
`RenderScene → SceneExtractor → SceneResolver → RenderSubmission → SceneRenderer` 流水线集中在 `engine/src/render/scene/`，顶层 `Renderer` 只负责编排渲染上下文、资源管理器和这条场景渲染链路。
- `Renderer`：编排 RenderScene 解析、帧开始/结束和 ImGui 回调，不读取 Material 属性或管理 descriptor。

## 帧同步

`render.max_frames_in_flight` 当前为 2，与实际 swapchain image 数量相互独立。

- `FrameSlot` 按 frame slot 创建，持有 in-flight fence、image-available semaphore、command buffer、submission serial
  和本 slot 实际录制所引用的 Runtime owner；等待 fence 后先释放这些 owner，再复用 slot。
- Queue 为每次 submission signal 自有 timeline 并返回单调 `GpuCompletionPoint`；该非拥有 token 位于 synchronization
  模块，由 UploadManager 和 Runtime Resource 保存。FrameScheduler 不重复保存它，frame-slot CPU 复用和
  RetainedResources 回收由 fence 控制；completion point 不得比 Device/Queue 活得更久。
- view/projection uniform buffer 按 frame slot 创建；材质 descriptor set 按 material handle 和 frame slot 缓存，只有对应 fence 完成后 CPU 才能改写。
- FrameScheduler 在 slot/image fence wait 后推进 `completed_frame_serial`；SceneRenderer 以 descriptor state 的最后使用
  serial 判断何时可以销毁整组 DescriptorPool 与缓存资源，不使用固定“延迟 N 帧”猜测。
- Device 只在 `VK_EXT_memory_budget` 实际可用并加入 logical-device extension 列表后为 VMA 启用 memory-budget flag；
  SceneRenderer 每帧传入 FrameScheduler 的单调 serial，循环 slot index 不作为 allocator frame age。
- `Allocator::query_memory_budget()` 将 `vmaGetHeapBudgets()` 转换为不暴露 VMA 类型的只读 snapshot，Device 只负责转发；
  snapshot 区分驱动报告值和扩展缺失时的 VMA 估算值，调用方不得把估算值当成硬分配上限。
- Allocator 的 `create_buffer/image` 是关键资源强失败接口；`try_create_buffer/image` 只返回完整 allocation 或显式
  Vulkan error，`within_budget` 也必须由调用方主动选择。任何路径都不允许发布带空 handle 的成功结果。
- `Buffer::try_create_gpu_buffer()` 与 `Image::try_create()` 先获得完整 allocation，再调用不可公开访问的 owning wrapper
  构造函数；失败时只有 error result，没有可观察的半初始化 GPUBuffer/OwnedImage。
- `GpuResourceResult<T>` 位于 graphics/resource 公共契约，不依赖 Allocator/VMA，统一承载 allocation、wrapper 和 upload
  操作的 Vulkan failure；`void` 特化用于只返回成功/失败的事务操作。
- `SwapchainImageState` 按实际 swapchain image 数量创建，持有 render-finished semaphore，并记录该 image
  最近关联的 frame slot。
- `SwapchainTarget` 按 image 持有 framebuffer 和对外暴露的颜色 image view；framebuffer 内部保留全部 attachment。
- editor 的 `MultiTarget` 按 frame slot 持有 framebuffer 和对外暴露的离屏颜色/resolve image view；深度等内部
  attachment 由 framebuffer 保留。同一 slot 的 fence
  完成后才会重新写入对应离屏资源。
- 离屏 resolve image 在场景 render pass 结束时转为 `ShaderReadOnlyOptimal`，同一 command buffer 随后的 ImGui
  render pass 通过对应 frame slot 的 descriptor 采样它。
- 显式 image transition 接收前后 `ImageState`，由 synchronization 层校验并生成 `ImageMemoryBarrier2`；Texture
  上传声明 Undefined、TransferDestination 和 Fragment SampledRead，不由 CommandBuffer 根据 layout pair 猜依赖。
- Buffer upload 在 copy 后根据最终 `ResourceState` 生成 `BufferMemoryBarrier2`，明确 TransferWrite 到 Vertex/Index
  read 的内存依赖；timeline completion 负责完成身份，不替代资源访问 barrier。
- 单个 CommandBuffer barrier 只处理未声明 owner或 owner 不变的状态。queue-family owner 改变必须由后续提交编排层
  生成 release/acquire barrier，并使用 semaphore/timeline 连接，不能靠一次 transition 冒充完整 ownership transfer。

交换链重建只重建 image state 和 swapchain target，不改变 frame slot 数量，也不重建 editor 离屏目标。
ViewPanel 尺寸稳定后才触发离屏目标重建；当前实现会在该低频操作前等待 Device idle。正常呈现路径不得依赖每帧
`queue.waitIdle()`；阻塞式资源上传也只等待自己的 timeline completion。Device idle 仅用于关闭、swapchain 重建和
离屏目标 resize 等全局资源切换点。

## 错误处理

- 违反引擎内部构造前置条件时使用 `LOG_FATAL` 记录诊断并立即终止，禁止部分初始化对象继续传播。
- Vulkan/VMA 创建失败必须立即终止当前创建流程，不能返回带空 handle 的可用对象。
- 可恢复的运行时状态，例如无效 AssetHandle 或缺失资源，应返回空结果并由提交层跳过，同时输出诊断。
- 缺少主 Camera、Camera 参数非法或渲染尺寸为零时保留清屏和编辑器 UI，但跳过场景 draw；重复状态不得每帧刷屏。
- `eErrorOutOfDateKHR` 和 `eSuboptimalKHR` 触发 swapchain 重建；其他 present/acquire 错误不得被静默忽略。
