# Comet 引擎长期开发路线图

首次生成：2026-07-05

最近更新：2026-08-31

## 目标定位

Comet 的长期目标建议定位为 **Unity/Godot 风格的编辑器型游戏引擎**：引擎不仅能渲染一个 demo，还要能通过编辑器创建场景、管理资产、编辑实体组件，并把同一份项目数据交给运行时执行。

这个定位比“学习型 Vulkan 渲染器”更强调工程闭环，也比“生产级商业引擎”更现实。短中期目标应该是做出一个能稳定完成小型 3D demo 的编辑器工作流，而不是一次性追求 AAA 级渲染、跨平台发布和完整工具生态。

## 当前项目现状

### 已具备的基础

- C++20 + CMake 项目结构已经成型，分为 `engine/`、`editor/`、`app/`、`tests/` 和 `3rdparty/`。
- `engine` 已有基础运行框架：`Application`、`Engine`、`Window`、`Timer` 和统一的 YAML 运行配置加载。
- Vulkan 底层封装已经有一定厚度，包括 `Context`、`Device`、`Swapchain`、`RenderPass`、`FrameBuffer`、`Pipeline`、`CommandBuffer`、`DescriptorSet`、`Buffer`、`Image`、`Sampler` 等。
- VMA 已开始接入，`Device` 独占持有 `VulkanAllocator`，`Buffer` 和 `Image` 通过 allocator 管理显存资源。
- 渲染层已有 `Renderer`、`RenderContext`、`SceneRenderer`、`FrameScheduler`、`RenderTarget`、`Mesh`、`Texture`、`Material`、`ResourceManager` 等雏形。
- Shader 构建链路已经接入 CMake，通过 `glslangValidator` 将 GLSL 编译并生成头文件。
- 编辑器已有 ImGui Docking 基础和几个典型面板：Hierarchy、Inspector、Project、Viewport、Log。
- 测试基础已经存在，覆盖数学、配置、导出、日志、GLFW 初始化、Vulkan RAII 拥有关系和 Scene/ECS 基础行为。
- EnTT 已经作为依赖接入，`Scene`、`Entity` 以及 ID、名称、局部 Transform、MeshRenderer、Camera 等基础组件已经落地。
- Scene/ECS MVP 内核已覆盖实体创建、删除、查询、遍历和通用组件增删查。

### 当前主要形态

目前 Comet 仍接近一个 **带编辑器外壳的 Vulkan demo 引擎原型**，但 Scene/ECS 已经接入最小运行时渲染链路、
编辑器基础数据闭环和场景持久化，New/Open/Save 与最小 Play/Edit 隔离也已经接入编辑器。当前主要矛盾已经转变为：
Texture/Material/Mesh 资产纵向链路已经建立，Mesh 也已有第一种 `.comet/cache/` 导入产物；已加载 Mesh/Texture 通过通用
TaskScheduler、revision 验票和 Owner Thread completion 完成后台 CPU 刷新，Mesh 外部 buffer 也已通过持久化缓存输入、源路径反向索引和 revision 形成精确失效；Editor 已用低频资产源快照自动触发同一扫描事务，但渲染接口仍偏 demo 化，运行时 System 调度还没有形成。

最明显的信号是：

- `Engine` 持有 Scene 和最小 Asset Registry，每帧提取 RenderScene；`SceneResolver` 选择主 Camera、生成 view/projection 并解析 Handle，`Renderer` 编排多个 render item。
- `Renderer` 已不再持有固定相机、demo mesh、texture 或模型矩阵，但当前仍使用固定 cube pipeline。
- 每个 draw 的模型矩阵已通过 push constant 提交；descriptor 资源按材质和 frame slot 缓存。
- Scene 已能管理运行期 `EntityId`、持久化 UUID、父子关系、局部 TRS 与派生世界矩阵，并可将基础组件保存为
  带版本字段的 `.scene` YAML；Inspector 与 `SceneSerializer` 已复用同一份组件/属性描述元数据。
- `MeshRendererComponent` 已使用统一的 `AssetHandle`，app/editor 会从项目资产加载 demo mesh/material，并通过该链路绘制实体。
- Hierarchy、Project 和 Inspector 已共享互斥的 Entity/Asset Selection；Inspector 通过显式组件/属性描述符编辑
  Transform、MeshRenderer 和 Camera，Material 的 Texture 属性在选择变化事件发生时自动保存并替换运行时对象；Viewport 已支持 CPU bounds 拾取、Focus、选中 world AABB 高亮和 Global Translation Gizmo，Rotation/Scale 尚未实现。
- 编辑器中的场景已按 frame slot 渲染到可采样离屏目标，再由 ImGui 显示在单一 Viewport 中；runtime app
  仍直接渲染到 swapchain。Viewport 在 Edit 时使用独立 2D/3D editor camera，Play 时切换到 Runtime Scene primary camera。
- Play 会从 Edit Scene 创建独立 Runtime Scene，Stop 后丢弃运行时修改并恢复原 Scene；暂停、单帧步进和真正的
  runtime System 更新仍未实现。
- Texture/Material/Mesh 已建立 Asset Database、导入/序列化和运行时发布链路，Mesh 产物可按源 glTF 与外部 buffer
  内容自动失效，已加载 Mesh/Texture 会在资产源监视触发扫描后进入后台 CPU 刷新；Material 属性和 Texture Import Settings 都在值变化事件发生时自动提交，通用递归依赖传播仍未建立。
- Scene Update 和 Render Submit 的最小边界已经落地，运行时 System 调度仍未建立。

## 距离成熟编辑器型引擎的核心缺口

### 1. 场景与实体组件模型

Scene/ECS MVP 内核已经完成：

- `Scene`、`Entity`、`IdComponent`、`NameComponent`、`TransformComponent`、`MeshRendererComponent` 和 `CameraComponent`。
- 基于 EnTT 的实体创建、删除、查询、遍历和通用组件访问。
- Scene 独占 registry，Entity 仅通过所属 Scene 访问组件。
- 父子层级、局部/世界变换更新，以及运行时 `EntityId` 与持久化 Entity UUID 的职责分离。
- 对应的纯逻辑单元测试。

接下来仍缺少：

- 运行时 System 调度。
- Rotation/Scale Gizmo、Transform snapping 与编辑器复制/删除/duplicate 命令。

Scene 数据模型已经有了地基，但只有完成渲染和编辑器闭环，Comet 才真正跨过从 demo 渲染器到游戏引擎的第一道门。

### 2. 序列化与项目格式

成熟编辑器引擎必须让用户的工作可持久化。基础 `.scene` YAML 契约、版本字段和纯 Scene 保存/加载已经完成；
当前仍缺少：

- 受版本控制的项目 manifest 或 `ProjectSettings/` 入口约定；`.comet/` 已保留给不提交的项目本地状态。
- `.mat` 已支持 Material Inspector 编辑保存；格式版本迁移和更通用的 MaterialLayout 驱动参数仍未建立。
- 跨项目迁移、递归 revision 和失效重载策略。
- 格式稳定后的 schema 冻结与版本迁移机制；开发期 `.scene` 不承诺向后兼容，过期文件可重新保存或重建。
- 项目文档的格式所有权收敛：人工维护的运行配置继续使用 YAML；由编辑器维护并进入版本控制的 `.scene`、`.mat`、`.meta`
  和 `ProjectSettings/`，在 Schema 稳定后整体迁移为严格、确定性输出的 JSON；`.comet/cache/` 继续保存可重建的二进制产物和索引。
- 编辑器中的显式另存为、自动保存和崩溃恢复。

开发期继续使用现有 YAML 格式，因为当前序列化、校验、原子保存和测试链路已经建立，调试成本低。JSON 迁移必须以
“编辑器成为项目文档的唯一写入入口”和 Scene/Material/Metadata Schema 基本稳定为前提，并一次性统一 `.scene`、`.mat`、
`.meta` 与 `ProjectSettings/` 的格式版本、迁移工具和确定性序列化规则；不能因为 fastgltf 间接带入 simdjson 就零散替换。
若未来正式采用 JSON，Comet 应定义自己的读写抽象和直接依赖，不能把 fastgltf 的私有传递依赖暴露为引擎序列化 API。

### 3. 资产系统与导入管线

当前已经建立 `AssetHandle`、相邻 `.meta`、Asset Database、Texture/Material/Mesh 的同步加载纵向链路，以及第一种
版本化 Mesh 二进制产物；Asset Registry 是运行时 Handle 缓存，ResourceManager 不再承担资产身份或缓存职责。距离成熟管线仍缺少：

- Texture/Shader 等更多 `.comet/cache/` 产物、统一缓存状态和重新导入调度。
- 更通用的递归失效传播，以及大型项目需要时可替换的原生文件事件后端。
- Shader、Scene 等更多资产类型及其导入/加载策略。
- glTF 多 mesh 子资产、node transform、material、animation、skin 和 morph target 导入契约。
- Texture 已支持持久化并在 Asset Inspector 编辑色彩空间和垂直翻转；wrap、filter、mipmap、压缩仍待接入对应运行时链路。
- Shader include、Mesh/Material 等后续资产类型的依赖追踪和热重载。

### 4. 编辑器数据闭环

当前编辑器已经完成第一段真实数据闭环：Hierarchy 和 Project 通过互斥的 Entity/Asset Selection 连接 Inspector，
组件描述符驱动 Transform、MeshRenderer 和 Camera 属性编辑，Material 可选择已索引 Texture 并在变化事件发生时自动保存、替换运行时对象；
创建、删除、重命名和场景文件操作会作用于真实 Scene，场景输出已通过离屏目标进入 View 面板。
接下来仍缺少：

- Project 缩略图、搜索、拖拽与创建/重命名资产工作流。
- Viewport 在 Edit 时使用 editor camera、选择、高亮、gizmo 和拾取，在 Play 时使用场景 primary camera 与游戏输入。
- Viewport 在 resize 后保持交互坐标、渲染分辨率与显示区域一致。
- Add Component、Remove Component 和组件搜索菜单。
- Undo/Redo、复制粘贴、duplicate 和拖拽资源到实体。

编辑器成熟度的关键不是面板数量，而是每个操作都能修改真实项目数据并可保存。

### 5. 渲染架构可扩展性

当前渲染管线已有 RenderScene、RenderItem、主 Camera 和多对象提交的最小链路，但仍使用固定 cube pipeline。
继续扩展仍缺少：

- Render Queue 排序、批处理和多 pipeline 选择。
- LightComponent 与基础光照。
- 材质参数到 descriptor/push constant/uniform 的稳定映射。
- 多 pass 结构，例如 shadow、depth prepass、forward/deferred、post-process。
- Pipeline cache key、shader reflection、descriptor 自动布局。
- Resize、swapchain recreation、offscreen render target 和 editor viewport 的完整闭环。

短期建议先完成 Forward Renderer，不要过早追求完整 Deferred/PBR。

### 6. 运行时系统与游戏循环

当前 `Application` 和 `Engine` 有基础生命周期，编辑器也已能通过 Scene 克隆完成最小 Play/Edit 隔离，但成熟引擎仍缺少：

- System 调度，例如 TransformSystem、RenderSystem、CameraSystem。
- Fixed Update 与普通 Update 分离。
- 输入系统。
- 时间缩放、暂停、单帧步进。
- 应用层访问场景和资源的稳定 API。

模式与运行状态应保持两条正交的状态轴：

```cpp
enum class EditorMode {
    Edit,
    Play
};

enum class RuntimeState {
    Running,
    Paused
};
```

`EditorMode` 只属于 editor：它决定当前使用 Edit Scene 还是克隆的 Runtime Scene，以及是否开放场景编辑工具。
runtime app 不需要理解 `EditorMode`，它启动后直接进入运行时生命周期。`RuntimeState` 属于可复用的运行时调度层：
editor 只在 Play 时使用它控制游戏 System 是否继续推进，runtime app 也可在需要游戏内暂停时复用同一语义。
Paused 时应停止 gameplay update/fixed update，但保持编辑器 UI、场景检查和画面渲染；单帧步进是对 Paused 状态的一次调度命令，
不需要再增加一个持久枚举值。不应增加 `EditorMode::Paused`，否则 Scene 所有权状态与运行时时钟状态会被耦合。

### 7. 脚本能力

编辑器型引擎最终需要让用户定义行为。当前缺少：

- C++ Native Script 组件或脚本接口。
- 脚本生命周期：on_create、on_update、on_destroy。
- 脚本字段暴露到 Inspector。
- 热重载或至少可控的重新编译流程。
- 长期可考虑 Lua、C# 或其他脚本层，但短期建议先做 C++ Native Script。

### 8. 物理、音频、动画和 UI

这些是成熟游戏引擎的常见功能模块，目前基本未出现：

- 物理：碰撞体、刚体、射线检测、触发器、物理调试绘制。
- 音频：音源、监听器、混音、空间音频、资源加载。
- 动画：骨骼动画、动画状态机、clip 导入、skinning。
- UI：运行时 UI 组件、字体、布局、事件。

这些不应该成为第一阶段重点，但路线图要预留模块边界。

### 9. 工具链、测试和发布质量

成熟引擎不仅是功能多，还要稳定可维护。当前缺少：

- 更严格的单元测试和集成测试。
- Headless/CI 友好的渲染测试或截图测试。
- Shader 编译失败的清晰诊断。
- 资源导入和序列化的回归测试。
- 性能统计、GPU profiler、内存统计。
- crash dump、错误报告、资源泄漏检查。
- 打包发布流程和示例项目。
- 文档化的架构决策记录。

## 路线图原则

1. 先做数据模型，再扩展功能。
2. 先打通编辑器闭环，再追求复杂渲染。
3. 每个阶段都交付一个可运行 demo。
4. 运行时和编辑器共享同一套项目数据，但保持职责边界。
5. 区分运行时 `EntityId`、持久化 Entity UUID 和资产 GUID，避免后期资产引用、序列化和 Undo/Redo 重做。
6. 渲染器只消费场景提交结果，不继续持有应用层 demo 状态。
7. 优先建设可测试的纯逻辑模块，把 Vulkan 相关测试控制在少量集成测试中。
8. Scene 和序列化数据只保存 `AssetHandle`，不保存文件路径、裸指针、`shared_ptr` 或 Vulkan handle。

### 资源引用分层

| 概念                 | 职责                                                                           | 引入阶段                               |
| -------------------- | ------------------------------------------------------------------------------ | -------------------------------------- |
| `AssetHandle`        | 代码层使用的轻量、不透明、可比较和可序列化的资源引用                           | 阶段 1B                                |
| Asset GUID/元数据    | 保证资产跨重启、改名和移动后的持久身份，并记录类型、路径和导入设置             | 阶段 2 定义持久化契约，阶段 3 完整实现 |
| Runtime/GPU Resource | `Mesh`、`Texture`、`Material`、`Buffer` 等已加载对象，只存在于资源缓存和渲染层 | 阶段 1B 起按需解析，永不写入 Scene     |

`AssetHandle` 是引擎 API 使用的包装类型，其底层值可以由持久化 GUID/UUID 提供。阶段 1B 只实现最小 Handle 和内存注册表，允许 demo 资源手动注册；资产扫描、`.meta`、导入器、依赖追踪和热重载仍留在阶段 3。

### 线程模型与任务系统演进

当前 Comet 是单线程主循环：主线程依次执行 GLFW 事件、Application/Scene 更新、`SceneExtractor`、
`SceneResolver`、Vulkan command recording、queue submit/present 和 ImGui 合成。这是当前规模下正确且便于
验证的基线，但路线图不能只零散地写“后台编译”或“渲染线程边界”，需要明确最终线程所有权。

线程演进遵循以下顺序：

```text
当前：单线程、清晰所有权
  -> 阶段 3：CPU TaskScheduler + 后台资产导入
  -> 阶段 3 后半段：UploadManager 异步完成与资源发布
  -> 阶段 5：独立 RenderThread + 有界 RenderFramePacket 队列
  -> profile 证明需要后：并行 culling/render preparation/secondary command recording
```

目标线程职责：

| 线程         | 主要职责                                                                          | 禁止直接执行                                |
| ------------ | --------------------------------------------------------------------------------- | ------------------------------------------- |
| Main/Update  | GLFW 事件、ImGui 构建、Scene/EnTT 修改、脚本和 editor command                     | 并发修改 GPU runtime cache                  |
| Worker pool  | 文件 I/O、解码、导入、Shader 编译和纯 CPU 数据处理                                | 修改 Scene、访问 ImGui、提交 Vulkan Queue   |
| RenderThread | 消费只读 frame packet、管理 GPU runtime cache、录制命令、submit/present、延迟销毁 | 读取可变 EnTT registry 或 editor panel 状态 |

基本规则：

- Scene/EnTT 默认由 Main/Update thread 独占，不为了并行任意 callback 给 registry 加一把全局锁。
- worker task 只接收不可变输入或独占数据，产出 CPU artifact/result，再通过有界队列回到 owner thread。
- `RenderScene` 继续作为 Scene 与 Renderer 之间的值语义快照；独立渲染线程阶段将其扩展为带 frame serial、
  viewport requests、资源 revision 和 owned UI draw data 的 `RenderFramePacket`。
- frame queue 必须有界，容量不超过允许的 frames-in-flight；主线程通过等待、覆盖 editor stale packet 或其他
  显式 backpressure 策略处理拥塞，不能让延迟和资源引用无限累积。
- Vulkan Queue submit/present 由 RenderThread 集中执行。并行录制只允许每个
  `frame slot + recording thread + queue family` 使用独立 CommandPool，不能并发访问同一个 pool。
- GPU 对象创建、缓存替换和销毁在 owner thread 的安全帧边界执行；跨线程只传描述、handle、revision 和
  completion token，不传临时裸指针。
- ImGui 的 `ImDrawData` 默认只在下一次 `NewFrame()` 前有效。迁移 RenderThread 时必须复制为 owned UI packet，
  或暂时让 editor UI 渲染保持在主线程，不能把原始指针直接排队。
- TaskScheduler 提供可在测试中使用的 single-thread executor，保证序列化、导入和资源状态测试可重复。
- 关机顺序明确为停止接收任务、取消或 drain worker jobs、停止生产 frame packet、join RenderThread、等待必要的
  GPU completion、销毁 GPU 资源，最后销毁 Window/Logger。

独立 RenderThread 不是当前阶段的前置任务。只有 RenderScene/RenderSubmission 不再依赖可变 Scene，ResourceManager
具有稳定 handle/revision 和延迟销毁语义，ImGui packet 生命周期也明确后，拆线程才不会把现有直接调用变成竞态。

### 目标渲染资源组织

以下结构描述最终职责和依赖方向，不要求一次重写，也不要求每个框都立刻对应一个新类。当前 `RenderContext`、
`Device`、`FrameScheduler`、`SceneRenderer` 和渲染侧资源服务按后续需求逐步收敛到这些边界；不为匹配命名增加只做
转发的 facade，也不把任何对象改成全局单例。

```text
RenderSystem (Main/Update owner)
  -> RenderScene / RenderFramePacket
      -> Renderer / RenderThread (GPU runtime owner)
          ├── InstanceContext
          ├── GraphicsDevice
          │   ├── LogicalDevice + queues
          │   ├── immutable CapabilitySet
          │   ├── Allocator (VMA)
          │   ├── PipelineCache
          │   └── low-level resource factories
          ├── FrameScheduler
          │   └── FrameSlot[N]
          ├── active SwapchainGeneration
          │   └── SwapchainImageState[M]
          ├── UploadManager
          └── render-side runtime/GPU caches
```

这是一张生命周期和调用方向图，不表示 `GraphicsDevice` 直接拥有下面所有模块。`InstanceContext` 先于 device 创建并
晚于 device 销毁；`FrameScheduler`、`SwapchainGeneration`、`UploadManager` 和渲染侧资源服务依赖 device，但各自
拥有自己的业务资源和回收规则。

#### GraphicsDevice

`GraphicsDevice` 表示稳定的 logical-device lifetime 边界。现有 `Device` 可以直接演进到该职责，不因名称不同额外
增加一层包装。它负责：

- logical device、graphics/present/transfer/compute queue handles 与 queue-family metadata。
- GFX-002 产生的不可变 `CapabilitySet`，记录实际启用的 API version、extensions、features、limits 和 formats。
- `Allocator`/VMA allocator 与 pipeline cache。
- 创建 Buffer、Image、Sampler、CommandPool 等底层 wrapper 所需的工厂入口。

它不拥有所有 FrameSlot、资产、材质、RenderTarget 或 swapchain-dependent object。Vulkan parent-child lifetime 只说明
销毁顺序，不等价于架构中的业务所有权。`InstanceContext` 继续负责 instance、debug messenger 和 physical-device
选择所需的实例级状态；surface/WSI 生命周期通过 swapchain 层协调，不能被普通 GPU resource factory 隐式持有。

#### FrameScheduler

现有 `FrameScheduler` 已经包含 slot 轮转、frame fence、image-available semaphore、command buffer、单调 submission
serial 和显式生命周期状态机；后续按实际需求迁入 arena：

```text
FrameSlot
├── graphics/queue-thread CommandArena
├── completion fence + frame submission serial
├── image-available semaphore
├── Uniform/TransientBufferArena
├── FrameDescriptorArena
├── RetainedResources
└── DeferredReleaseBatch
```

`begin_frame()` 的顺序固定为等待该 slot 的 completion、回收 retained/deferred resources、reset command、descriptor
和 transient arenas，再开始下一次录制。CommandArena 按 `frame slot + queue family + recording thread` 隔离；没有启用并行
录制时只创建主录制线程所需 arena。长期 material/global descriptor、资产和 swapchain image state 不进入 FrameSlot。

FrameScheduler 负责“何时可复用”，不要求物理拥有所有 allocation。迁移初期 UBO、descriptor 和 transient buffer
仍可保留在 `SceneRenderer` 等现有 owner 中，只要统一使用 scheduler 的 slot index 和 completion/reset contract。

#### SwapchainGeneration

SwapchainGeneration 负责随 surface format、extent、image count 和 present mode 共同失效的资源；具体 generation
结构、重建事务和 presentation completion 规则见后文“Swapchain Generation 与重建事务”。render-finished semaphore
和 last frame-slot use 按 swapchain image 保存，FrameScheduler 只管理当前 slot；acquire/submit 时临时配对
`FrameSlot[N]` 与 `SwapchainImageState[M]`，不能假设两个索引相同。

#### UploadManager

UploadManager 将 GPU resource 创建拆成目标对象创建、批量传输和异步就绪三个阶段：

```text
CPU artifact + destination resource
  -> enqueue_upload()
  -> flush_batch() records copy/barrier and signals timeline value V
  -> first consuming graphics submission waits ready token V
  -> completed value >= V: reclaim staging and upload command resources
```

公开接口按用途区分：

- `upload_and_wait()`：启动期、工具和测试使用，返回时资源已可用。
- `enqueue_upload()`：运行期 streaming 使用，资源进入 pending 状态并返回 upload request/ticket。
- `flush_batch()`：控制提交粒度并产生 batch completion point；每个 request 获得引用该 completion point、同时记录
  自己 final resource state 的 ready token。

ResourceManager 可以发布带 ready token 的 pending resource，Renderer/RenderGraph 在首次消费时把 token 转成精确 stage
的 semaphore wait；若 upload 与消费在同一个有序 queue 上，backend 可以依据 queue-order 省略冗余 semaphore wait，
但 staging 回收仍以 completion value 为准。当前实现也可以先选择“完成后才发布”的简单策略，但不能在 API 中丢失
ready token，避免以后只能通过 CPU wait 才能接入异步资源。

当前实现已由 ResourceManager 独占持有 UploadManager：Buffer/Image allocation 与上传分离，`enqueue_upload()`
立即把 CPU 数据复制到可复用 staging page 的子分配范围并录制 batch，`flush_batch()` 返回 `GpuCompletionPoint`；
pending batch 独占所用 page、CommandContext 和目标资源直到 completion，随后整页回池。同步 ResourceManager 通过
每帧 collection 回收已完成 batch；Mesh/Texture 创建不再 CPU wait，而是保存 ready completion 后立即返回，一个 Mesh
的 vertex/index copy 已合并为一次 submission。SceneRenderer 从实际 draw 资源汇总 ready completion，按 timeline
semaphore 合并等待值和 stage，在 frame submission 建立 VertexInput/FragmentShader wait；完成查询在去重后每个
timeline 只执行一次。跨资产批量和更细粒度 ring 回收仍待实现。

#### Descriptor System

Descriptor 按更新频率和 owner 拆成三类：

```text
FrameDescriptorArena[slot]
  -> 当帧临时 set，slot completion 后整体 reset

PersistentDescriptorArena
  -> material/global/versioned set，显式释放或随 generation retirement

ImGuiDescriptorPool
  -> editor-only，由 ImGuiContext/EditorPresentGeneration 独立管理
```

当前“一材质一个 pool”和每 slot descriptor set 可以继续服务 MVP。只有 pool 数量、碎片或更新时间经过 profile 证明
成为问题后，再把 PersistentDescriptorArena 实现为分页 pool。descriptor allocation lifetime 与 descriptor 引用的
Buffer/Image lifetime 是两套契约；reset/free descriptor set 不能代替 `RetainedResources` 保持底层资源有效。

#### Asset Runtime Cache、ResourceManager 与 RenderSystem

Asset Registry/Asset Manager 是 `AssetHandle` 到 Mesh、Texture、Material 等已加载运行时资产的唯一缓存和发布边界，
并逐步管理 pending、ready、failed、evicted 和 revision 状态。`ResourceManager` 只保留 Device 相关对象创建与
Shader/Sampler 等设备级共享资源，不建立第二份 AssetHandle 缓存。未来由 `MaterialRuntimeCache`、Pipeline cache 等
渲染侧缓存按资产 Handle 与 revision 保存派生 GPU 状态；UploadManager 负责传输，GpuRetirementQueue 负责旧 GPU owner 回收。

`RenderSystem` 是 Main/Update system schedule 中的高层协调者：读取 Scene 的稳定时点，调用 extraction/culling，生成
`RenderScene` 或 owned `RenderFramePacket` 并提交 Renderer。它不持有 Vulkan object，不从 RenderThread 读取可变
Scene，也不替代负责 GPU command/submit 的 Renderer。当前 Engine 主循环中的提取与提交代码可在运行时 System
调度建立后逐步收敛到该边界。

#### Resource State Tracker

Resource state tracker 不属于单个 RenderTarget。UploadManager 可使用 batch-local tracker，RenderGraph 使用
frame-compile tracker，持久资源通过明确 handoff state 连接两者；完整状态、subresource 和 Barrier2 规则见后文
“资源状态与 Barrier 编译”。

### GPU Completion 与延迟释放

当前 `FrameSlot` 持有 in-flight fence 和最近一次 frame submission serial；`FrameScheduler::wait_for_current_slot()`
会在复用 slot 前等待 fence并推进 completed serial，随后 SceneRenderer 收集通用
GpuRetirementQueue。viewport resize、swapchain
recreation、渲染模式切换和 renderer cleanup 仍通过 `Device::wait_idle()`
保证旧资源不再被 GPU 使用。这对当前阶段是安全且简单的基线，但不能作为 texture/shader/pipeline 热重载、资产卸载、
streaming 和持续 resize 的常规资源替换机制。

目标模型分为两层：

```text
single graphics-frame use
  -> FrameSlot fence + DeferredReleaseBatch

async upload / multiple submissions / multiple queues
  -> GpuCompletionPoint(queue, timeline value)
  -> GpuRetirementQueue[completion point -> owning resources]
```

FrameSlot 路径的顺序固定为：

```text
replace resource R
  -> 新 frame packet/submission 不再引用 R
  -> 根据 R 的 last-use submission 把 owning reference 放入对应 retirement batch
  -> wait slot fence
  -> release batch
  -> reset fence/command pool and reuse slot
```

实现边界：

- 不能只记录循环复用的 FrameSlot index 作为长期 completion token。slot 应同时关联单调递增的 frame submission
  serial，退休资源必须依据实际 last-use submission，而不是依据“当前 CPU 正在写哪个 slot”猜测。
- 单 graphics queue 上，较晚 submission 的完成可以覆盖同 queue 上更早的提交；跨 graphics/transfer/compute queue
  使用的资源需要记录各 queue 的 completion point，全部满足后才能释放。
- UploadManager 使用 timeline value 管 staging page、upload command buffer 和上传资源的临时引用；FrameSlot batch
  只负责由该 frame fence 完整覆盖的 graphics 使用，不能替代 timeline retirement。
- `std::vector<std::function<void()>>` 可以作为原型，但不作为长期资源所有权接口。正式实现使用受约束的 move-only
  retirement entry 或 owning-resource type erasure，回收只执行 noexcept 资源析构，不允许混入任意业务 callback。
- versioned texture、material descriptor、shader 和 pipeline 在切换新 revision 后，把旧 owning reference 交给回收器；
  command buffer 中的裸 Vulkan handle 本身不能延长 C++ owner 生命周期。
- `GpuRetirementQueue::collect()` 只能由当前 GPU resource owner thread 执行。阶段 5 引入 RenderThread 后，Main/Update
  和 worker 只提交 retire request，不直接销毁 Vulkan 对象。
- `waitIdle()` 仍允许用于正常 shutdown、设备丢失/恢复边界、测试和缺少更精确完成机制的平台回退；目标是移出正常
  运行中的资源替换路径，而不是机械删除所有调用。

### Swapchain Generation 与重建事务

当前 `SceneRenderer::recreate_swapchain()` 会等待全部 graphics frame-slot fence，并在没有 present completion 的平台只等待 present queue；
之后释放 runtime `SwapchainTarget` 和 editor ImGui target，再由 `Swapchain::recreate()` 构建局部 `SwapchainGeneration` 候选。generation 收拢
handle、borrowed images、config 与 current index，全部就绪才替换 active shared owner；窗口最小化延期或 `vkCreateSwapchainKHR` 失败时
从仍有效的旧 core 恢复 dependent。父子对象顺序、core active 字段事务、dependent generation 和 compatibility diff 已建立；runtime
format-dependent Pipeline generation 仍未完成。

目标结构按代际管理：

```text
SwapchainGeneration
├── vk::SwapchainKHR
├── BorrowedImage[M]
├── format / extent / image-count / present-mode metadata
└── SwapchainImageState[M]

SwapchainTargetGeneration (engine)
├── ImageView[M]
├── Framebuffer[M]
├── depth/MSAA resources
└── shared owner -> SwapchainGeneration

EditorPresentGeneration (editor only)
├── ImGui render target/backend resources
└── shared owner -> SwapchainGeneration
```

`engine_runtime` 的 swapchain core 不反向持有 editor 类型；app 和 editor 分别构建自己的 dependent generation，
通过共同持有 core generation 保证先销毁 framebuffer/image view，最后销毁 swapchain。RenderPass、Pipeline 和 ImGui
backend 是否重建由新旧 format、sample count 和 image count 的 compatibility diff 决定，不能只检查 extent。

重建按 prepare/create/commit/retire 管理，但 Vulkan WSI 的事务边界需要单独处理：

```text
prepare CPU plan + validate format-dependent compatibility
  -> vkCreateSwapchainKHR(oldSwapchain = current handle)
      -> 失败：不覆盖或销毁 current generation
      -> 成功：old swapchain 立即进入 retired 状态，不能再 acquire
  -> 为新 images 创建 views/depth/MSAA/framebuffers/per-image state
  -> 完成新 RenderPass/Pipeline/ImGui dependent generation
  -> 切换 active generation
  -> old dependent generation 进入 retirement queue
  -> rendering 与 presentation 都完成后销毁 old generation
```

因此“创建失败继续使用旧 generation”只适用于新 swapchain handle 创建成功之前；成功之后若 dependent resource
创建失败，必须进入明确的 retry/recovery/no-present 状态，不能重新从已 retired 的 old swapchain acquire。实现时先在
局部 generation 对象中构建，不在中途覆盖 `m_swapchain`、`m_images` 等 active 字段。

graphics FrameSlot fence 只证明相关 graphics submission 完成，不自动证明 presentation engine 已释放旧 presentable
image。旧 swapchain 的回收必须使用可用的 present completion/fence；平台不支持时，对 present queue 执行局部
`waitIdle()` 作为兼容回退。不能仅把 old swapchain 放入任意 FrameSlot 的 deferred batch。
当前正常重建已经收窄为全部 graphics frame-slot fence 与 present queue idle 回退；`Device::wait_idle()` 只保留在 shutdown、
device-lost 等全局边界。未来平台提供 present completion/fence 时，再用精确完成信号替换 present queue idle。

### Vulkan 同步演进顺序

当前 Vulkan 1.3 `synchronization2` feature 查询与启用、`Queue::submit2()` 和显式 image/buffer barrier 迁移均已完成。
每个 submit wait/signal 使用独立 `vk::SemaphoreSubmitInfo`；Texture 阻塞上传通过类型化 `ImageState` 提供 stage、
access、layout、subresource 和 queue owner，并由 `vk::ImageMemoryBarrier2`、`vk::DependencyInfo` 与
`pipelineBarrier2()` 录制；UploadManager 的 buffer copy 也会从 TransferWrite 转到 Vertex/Index read state，并生成
`vk::BufferMemoryBarrier2`。Vulkan 1.2 `timelineSemaphore` 已加入能力查询和 logical device feature chain；每个
Queue submission 会 signal 单调 timeline value 并返回 `GpuCompletionPoint`。旧 layout-pair 推断、legacy image
barrier、对应 32-bit stage/access 转换以及 Queue 级上传 `waitIdle()` 已经删除。

Synchronization 2 应作为阶段 3 异步上传的前置基础设施，顺序固定为：

```text
GFX-002 PhysicalDevice capability/feature chain
  -> 查询并启用 PhysicalDeviceVulkan13Features::synchronization2
  -> Queue::submit 迁移为 SubmitInfo2
  -> image/buffer barrier 迁移为 DependencyInfo + *Barrier2
  -> 用现有 frame/upload 路径完成 validation 回归
  -> 引入 timeline semaphore completion value
  -> 实现异步 UploadManager
  -> profile 证明需要后再引入 transfer queue
```

实现边界：

- Vulkan 1.3 将 Synchronization 2 纳入 core，但 `synchronization2` feature 仍需显式查询和启用；不能只调用
  `submit2()`。Comet 以 Vulkan 1.3 为 baseline 后无需继续维护 legacy submit/barrier 双实现。
- `Queue::submit2()` 的每个 wait/signal 使用独立 `vk::SemaphoreSubmitInfo`，明确 semaphore、timeline value 和
  stage mask；command buffer 使用 `vk::CommandBufferSubmitInfo`。
- barrier 使用 `vk::ImageMemoryBarrier2`、`vk::BufferMemoryBarrier2` 和 `vk::DependencyInfo`。资源状态至少包含
  layout、stage mask、access mask、queue-family owner 和 subresource range，不继续只由 old/new layout 猜测完整依赖。
- 迁移 Synchronization 2 不要求同时切换 dynamic rendering，也不改变 `presentKHR` 接口。
- transfer queue 不是 Synchronization 2 迁移的验收条件；以后启用独立 queue family 时，再补齐 release/acquire
  ownership transfer 和跨 queue semaphore 依赖。

### 资源状态与 Barrier 编译

此前 `CommandBuffer::transition_image_layout()` 根据 old/new layout 组合硬编码 stage/access，只能覆盖 demo 的
`undefined -> transfer dst -> shader read` 等少量链路。现在显式 image transition 已改为消费 known-before 和
desired-after `ImageState`；layout 不再被当成完整同步关系，业务调用点也不再扩展 layout-pair `if/else`。

目标模型：

```text
Upload operation / RenderGraph Pass
  -> declared ResourceUsage or explicit desired ResourceState
      -> state mapping + previous known state
          -> BarrierCompiler / StateTracker
              -> vk::DependencyInfo + ImageMemoryBarrier2/BufferMemoryBarrier2
```

`ResourceState` 至少描述 pipeline stage、access mask 和 queue-family owner；`ImageState` 额外描述 layout 与
subresource range。`SampledRead`、`ColorAttachmentWrite`、`DepthAttachmentWrite`、`TransferSrc/TransferDst`、
`Present` 等 `ResourceUsage` 是面向常见用途的语义层，由 Vulkan backend 映射为具体 state；特殊路径可以显式提供
state，但不能退回只传 old/new layout。

状态所有权与正确性边界：

- 不在 `Image` 内只保存一个全局 `current_layout`。同一 image 的 mip/layer 可能处于不同状态，已录制或 in-flight
  command buffer 也会让“CPU 当前值”产生歧义。
- 状态跟踪属于 command recording/RenderGraph 编译上下文，并按 subresource range 记录。持久资源由 RenderThread
  保存最后一次已提交的 handoff state，作为下一次编译的已知初始状态；临时 graph resource 的状态只属于该次编译。
- 新建 image 从 `eUndefined` 开始；UploadManager 明确声明 transfer destination 和最终消费状态；swapchain image
  的 acquire/present 作为外部状态契约；导入 RenderGraph 的资源必须声明已知 initial state，不能从业务逻辑猜测。
- compiler 比较 previous/desired state，只有存在 layout transition、写后读、读后写、写后写或 queue ownership
  变化时生成必要 barrier；兼容的 read-after-read 不应机械插入全 barrier。
- queue-family ownership transfer 生成配对的 release/acquire barrier，并由 semaphore/timeline dependency 连接；
  不能只修改 barrier 中的 queue-family index。
- 绕过 RenderGraph 的显式 copy、清屏或第三方渲染代码必须通过受控接口更新 handoff state。未知状态应在边界处
  显式拒绝或要求调用方声明，不能默认为某个 layout。

分阶段实施：

1. Synchronization 2 迁移时先定义可测试的 `ResourceState`/`ImageState` 和 usage-to-state 映射，让上传、swapchain
   与现有 RenderPass 路径显式传入 known-before/desired-after state，停止扩展 layout-pair `if/else`。
2. UploadManager 在 batch 内使用局部 tracker 合并连续转换，并把最终 handoff state 与 completion token 一起发布。
3. 阶段 5 的 RenderGraph 根据 pass 的资源读写声明遍历状态，自动生成 barrier；pass execute callback 不再自行猜测
   graph resource 的 old layout。
4. 增加纯单元测试覆盖常见 state mapping、read/write hazard、subresource、queue transfer 和 unknown initial state；
   再通过 validation layer 验证真实 frame、upload、resize 和 present 链路。

### Dynamic Rendering 的评估时机

Vulkan 1.3 的 Dynamic Rendering 可以不再预先创建显式 `RenderPass`/`Framebuffer` 对象，而是在
`beginRendering()` 时直接描述 color、depth 和 stencil attachments。它适合 attachment 组合频繁变化、单 pass
离屏视口和 RenderGraph 动态生成 pass 的场景，但不是对传统 RenderPass 的无条件替代。

Comet 当前的 `RenderPass`、`FrameBuffer`、attachment 和 subpass 封装已经可以支撑现有 app、editor viewport 与
ImGui 合成链路。阶段 1 至阶段 4 不为替换 API 单独重写；在阶段 5 设计多 pass/RenderGraph 时再做技术验证和选择。

演进边界：

- 先建立与 Vulkan 后端对象解耦的 pass 描述，至少包含 attachment format/sample count、load/store op、pass 内
  desired usage，以及 imported/exported resource 的边界状态。RenderGraph 编译阶段再决定生成
  `vk::RenderingInfo`，还是兼容的 `RenderPass`/`Framebuffer`。
- Pipeline 的 render-target compatibility signature 必须包含 color/depth/stencil formats 和 sample count；采用
  Dynamic Rendering 时，通过 `vk::PipelineRenderingCreateInfo` 把这些信息纳入 pipeline 创建与 `PipelineKey`。
- 通过 GFX-002 的 Vulkan 1.3 feature chain 查询并显式启用 `dynamicRendering`，不能只因为 API version 是 1.3
  就直接调用 `beginRendering()`。
- 简单 forward、后处理和 editor viewport pass 优先验证 Dynamic Rendering；需要 input attachment、复杂 subpass
  合并或依赖 tile-local 数据的路径，应结合目标 GPU 能力与实测结果决定是否继续使用传统 RenderPass。
- 不要求所有 pass 使用同一种后端。若平台收益明确，RenderGraph backend 可以同时支持 Dynamic Rendering 与传统
  RenderPass；上层 pass、material 和 scene submission 不应感知该选择。
- 迁移时验证 validation layer、RenderDoc/调试工具、ImGui Vulkan backend、MSAA resolve、resize/swapchain rebuild
  以及桌面与 tile-based GPU 行为，不以减少对象数量本身作为完成标准。

触发评估的条件是 RenderGraph 已开始承载多个实际 pass，或 attachment 组合变化导致现有 RenderPass/Framebuffer
缓存和重建逻辑出现可测复杂度。若届时传统路径仍简单、稳定且性能满足要求，可以继续保留，不安排无架构收益的
全量替换。

### 组件元数据与反射策略

反射在 Comet 中的目标不是单纯“自动遍历 C++ 成员”，而是建立一份可被 Inspector、场景序列化、
Undo/Redo 和脚本字段暴露共用的稳定组件元数据。目标调用链是：

```text
ComponentRegistry
  -> ComponentDescriptor
      -> PropertyDescriptor[]
          -> Inspector PropertyEditor
          -> Scene Serializer
          -> Undo/Redo Property Command
          -> Script Field Binding
```

`ComponentDescriptor` 应记录稳定类型 ID、显示名、创建/删除/查询组件的操作；`PropertyDescriptor` 应记录稳定字段
ID、类型、访问器以及 `editable`、`serializable`、`transient`、`read_only`、`min/max/step` 等属性。
持久化 ID 不能使用 `typeid(T).name()` 或编译器生成名称；字段访问优先使用 pointer-to-member 或类型化访问器，
不依赖裸 offset。

短期保持 C++20，使用显式、类型化注册建立第一版元数据，概念接口可以类似：

```cpp
registry.component<TransformComponent>("Transform")
    .property("translation", &TransformComponent::translation)
    .property("rotation", &TransformComponent::rotation)
    .property("scale", &TransformComponent::scale);
```

该注册层是引擎自己的稳定边界，Inspector 不应直接绑定 EnTT meta 或某个编译器的反射 API。当 C++26
反射在主要工具链上成熟后，可用 `<meta>` 在编译期枚举成员，结合 annotation 生成同样的
`ComponentDescriptor`/`PropertyDescriptor`；Inspector、序列化和 Undo/Redo 不需因此重写。自动反射也不应默认
暴露所有 C++ 成员，仍需要显式的编辑、序列化和版本迁移策略。

#### C++ 标准升级决策

当前不因反射把整个工程从 C++20 升级到 C++26，升级到 C++23 也不会获得这套反射能力。截至
2026-08-05，[P2996R13](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html) 已给出
C++26 反射方案；[GCC 16](https://gcc.gnu.org/projects/cxx-status.html) 需要 `-std=c++26 -freflection`，
而 [Clang 官方状态](https://clang.llvm.org/cxx_status) 仍将 P2996R13 标为未支持，
[MSVC STL 跟踪项](https://github.com/microsoft/STL/issues/5606) 也仍处于 blocked。Comet 当前的 macOS
工具链是 Apple Clang，CI 也没有固定 GCC 16，现在升级会破坏本地与 CI 可移植性。

只有在以下条件同时满足时，才重新评估把项目 baseline 升级到 C++26：

- macOS 的 Apple Clang/Clang 和 Linux CI 编译器都支持 P2996 及所需 `<meta>` 库。
- 反射可通过标准模式和 feature-test macro 检测，不再依赖单一实验分支。
- Vulkan SDK、VMA、GLFW、GLM、EnTT、ImGui 和测试依赖在目标编译器上验证通过。
- C++20 显式元数据实现可作为回归对照，反射后端生成的 descriptor 行为与之一致。

在此之前，可以建立不进入默认构建和发布链的 GCC 16 实验 target，用于验证 C++26 反射如何生成
Comet descriptor；不应在业务代码中同时维护两套 Inspector 或序列化逻辑。

### Material 与 Shader 绑定演进策略

当前材质链路是为固定 cube pipeline 服务的 MVP：`SceneResolver` 每帧按字符串读取
`u_Texture0`/`u_Texture1`，`MaterialBinding` 和 descriptor 资源固定为两张纹理，pipeline layout 固定使用
binding 0/2/3，所有 draw 也共享一个 `cube_pipeline`。后续不应只把 `array<2>` 换成 `vector`，而要
建立 shader 接口、材质数据和 GPU 绑定之间的契约。

目标链路：

```text
MeshRendererComponent
  -> mesh handle + material handle
  -> SceneResolver
      -> Mesh + Material Asset
  -> Render Queue（Pipeline/Material 排序）
  -> MaterialRuntimeCache
      -> MaterialLayout / ShaderInterface
      -> PreparedMaterial
      -> versioned immutable Material DescriptorSet
  -> SceneRenderer
      -> bind frame set
      -> bind material set
      -> push object constants
      -> draw
```

职责边界：

- `SceneResolver` 只把 mesh/material handle 解析为运行时对象，不得知道具体材质属性名、纹理数量、
  descriptor set 或 binding。
- `ShaderInterface` 记录 descriptor set/binding/type/count/stage 和 push constant range；`MaterialLayout` 在此基础上
  补充稳定 `PropertyId`、默认值、编辑器显示和参数语义。
- `Material` 是可序列化的材质资产，保存 layout/template 引用和具体参数；纹理和其他资产参数最终保存
  `AssetHandle`，不保存 `shared_ptr<Texture>` 或 descriptor。
- `MaterialRuntimeCache` 属于渲染运行时，负责将材质中的资产 handle 解析为 Texture/Sampler/Buffer、
  创建 descriptor set 并缓存 `PreparedMaterial`；GPU 资源不回写到 Material Asset。
- `SceneRenderer` 只绑定已准备的 pipeline 和 material descriptor，不再通过字符串查询材质属性。

Descriptor set 按更新频率拆分：

| Set/通道               | 资源                                      | 更新频率                             |
| ---------------------- | ----------------------------------------- | ------------------------------------ |
| set 0: Frame           | view/projection、light、shadow            | 每个 frame slot                      |
| set 1: Material        | texture、sampler、稳定 material constants | material revision 变化时创建替代版本 |
| set 2: Object/Instance | skinning、instance buffer（未来）         | 每对象或批次                         |
| push constant          | model matrix、object ID                   | 每次 draw                            |

`Material` 和 `MaterialLayout` 需要 revision/generation。`MaterialRuntimeCache` 以 material handle、material revision、
layout/pipeline ID 作为稳定缓存维度，只在材质或 layout 变化时重新解析参数。稳定的纹理/sampler descriptor set
不按 frame slot 复制；材质变化时创建新的不可变 descriptor set，旧版本等引用它的 in-flight frame 完成后再释放。
如果某类 material constants 使用 per-frame backing buffer，只为这部分维护 per-slot binding/offset state，不复制稳定
纹理 binding。同一材质被多个 render item 使用时，不应重复进行字符串查询和资源解析。

Shader 源码、编译结果和 Runtime Vulkan 对象应保持分层。路线图现阶段只确定以下能力边界：

- Shader 源文件与共享 include 作为可再生成输入，由版本控制管理。
- build-time compiler 与 editor compiler 共用 stage、entry、define/variant、target 和依赖信息的逻辑契约。
- 编译结果可提供 SPIR-V、revision 和后续 reflection 生成的 `ShaderInterface`，Runtime 再从中创建
  `ShaderModule`、`PipelineLayout` 和 `Pipeline`。
- Shipping app 只消费预编译并打包的 Shader 数据，不携带 Shader 源码编译器。

编译描述和编译结果是否对应独立类型、是否落盘、使用资源包还是构建期嵌入数据，等 Asset Database 方案确定后再选型。
热加载不以 Runtime 直接读取松散 `.spv` 为前提。

Shader reflection 后续从 SPIR-V 提取 set、binding、descriptor type、array count、stage 和 push constant，
用于生成低层 `ShaderInterface`；它无法单独决定参数显示名、默认资源、颜色/法线等语义和
Inspector 范围，这些仍由 Material metadata 提供。Shader reflection 与前述 C++ 组件反射是两套不同机制，
不应混用。在引入反射库之前，先用手工 `MaterialLayout` 跑通多 pipeline 和动态参数链路；当前阶段
不引入 bindless/descriptor indexing。

编辑器 Shader 热加载是独立能力，但应与 Shader reflection 共用 `ShaderInterface`、pipeline key 和缓存失效逻辑。
目标链路：

```text
Editor FileWatcher
  -> Shader source changed（debounce）
  -> 根据 Shader Asset/导入设置解析 stage、entry、define 和依赖
  -> ShaderCompiler 后台编译 SPIR-V
  -> Shader Reflection 生成新 ShaderInterface
  -> 形成内存中的候选编译结果/revision
  -> 与旧 ShaderInterface 比较
      -> 接口兼容：重建 ShaderModule/Pipeline
      -> 接口变化：重建 Layout，失效相关 MaterialRuntimeCache
  -> 在安全帧边界切换新 Pipeline
  -> 等待相关 frame fence 后销毁旧 GPU 对象
```

热加载只属于 editor/tooling：editor 链接文件监听、ShaderCompiler 和错误诊断，runtime app 只消费已编译的
Shader 导入产物，不携带源码编译器。编译失败时必须保留旧 Shader/Pipeline 继续渲染，并把文件、行号和
错误消息送到 Console/Shader Inspector。成功编译也不应立即销毁可能仍被 in-flight command buffer 引用的旧
ShaderModule、Pipeline、PipelineLayout 或 DescriptorSetLayout。

建议演进顺序：

1. `SceneResolver` 只返回已解析 `Material`，移除 `u_Texture0/1` 和固定纹理数组。
2. 建立 material/layout revision 和 `MaterialRuntimeCache`，停止每帧重复解析同一材质。
3. 用手工 `MaterialLayout` 验证参数类型与 binding，拆分 frame/material descriptor set。
4. 让 Render Queue 按 pipeline/material 排序，支持多 shader、多 pipeline 和非纹理材质参数。
5. 在 Asset Database 结构稳定后，确定 Shader 导入设置、编译结果与缓存/打包形式；此前不锁定具体类型。
6. 接入 SPIR-V shader reflection 生成 `ShaderInterface`，Material metadata 继续补充高层语义。
7. Material Inspector 基于 `MaterialLayout` 生成控件，修改参数后通过 revision 精确失效运行时缓存。
8. 增加 editor-only Shader 热加载，生成候选编译结果，复用 reflection 比较接口并重建 pipeline/材质绑定。

### Pipeline 对象缓存与驱动 Cache 持久化

Comet 当前已经创建 `vk::PipelineCache`，并在 `createGraphicsPipeline()` 时传入，因此同一进程内的驱动级 cache
可以生效；但 `Device::create_pipeline_cache()` 总是从空 cache 启动，析构前也没有调用
`getPipelineCacheData()` 保存。另一方面，`PipelineManager` 当前只用字符串 name 作为对象缓存 key，在多 Shader、
热加载和多 render state 阶段不能保证复用正确性。

这两层 cache 必须分开设计：

| 层次                 | 职责                                                                       | Key/兼容依据                                       |
| -------------------- | -------------------------------------------------------------------------- | -------------------------------------------------- |
| Engine `PipelineKey` | 判断当前进程中两个 pipeline description 是否等价，复用 `vk::Pipeline` 对象 | Shader/layout/render state/render-target signature |
| Vulkan cache blob    | 帮助驱动跨 pipeline 创建或跨进程复用内部编译结果                           | `VkPipelineCacheHeaderVersionOne` 与设备/驱动标识  |

#### Engine PipelineKey

Pipeline name 只作为调试标签，不再作为唯一缓存身份。`PipelineKey` 至少包含：

- Shader artifact ID/content hash、revision、entry point 和 specialization constants。
- `ShaderInterface`/`PipelineLayout` signature，包括 descriptor set layouts 和 push constants。
- vertex input、input assembly/topology。
- rasterization、multisample、depth/stencil、blend 和 dynamic-state 配置。
- color/depth attachment formats、sample count、subpass 或等价的 render-target compatibility signature。

`PipelineManager` 以结构化 `PipelineKey` 查询对象缓存；hash 只用于索引，命中后仍执行完整相等比较，不能让 hash
碰撞返回错误 pipeline。Shader 或 render state revision 变化时创建新 key/new pipeline，旧对象等引用它的
in-flight frame 完成后再释放，不原地替换仍可能被 GPU 使用的 handle。

#### Vulkan Cache Blob

目标生命周期：

```text
startup
  -> PhysicalDevice 确定
  -> 从 .comet/cache/vulkan 读取候选 blob
  -> 校验 envelope 和 VkPipelineCacheHeaderVersionOne
  -> 通过 PipelineCacheCreateInfo.initialDataSize/pInitialData 创建 cache

pipeline creation/hot reload
  -> 始终传入同一个 owner-thread pipeline cache

compile burst 后或正常 shutdown
  -> getPipelineCacheData
  -> 写临时文件
  -> atomic rename 替换正式 cache
```

兼容与失败策略：

- 至少校验 `headerSize`、header version、`vendorID`、`deviceID` 和 `pipelineCacheUUID`；UUID 是识别驱动实现兼容性
  的关键字段，不能只比较 vendor/device。
- Comet 自己的文件 envelope 可额外保存 magic、schema version、driver version、Vulkan API version、payload size
  和 checksum，便于拒绝截断或损坏文件。
- 不兼容、损坏或读取失败时记录一次诊断并退回空 cache，不能阻止引擎启动。
- cache 文件放在项目 `.comet/cache` 或平台用户 cache 目录，不放进引擎源码目录，默认不提交版本控制。
- Shader 或 render state 变化通过 `PipelineKey` 产生新的 pipeline create info，不需要因此删除整个驱动 blob；
  驱动 cache 会自行判断是否存在可复用条目。
- `vk::PipelineCache` 的 host access 需要遵守外部同步。引入 RenderThread 后，pipeline 创建、合并和 blob 读取/保存
  都由 RenderThread 串行执行，不从后台 ShaderCompiler 直接访问 cache。
- 不每帧保存。editor 可在一批 pipeline 编译完成后节流保存，runtime 至少在正常 shutdown 保存；异常退出允许
  丢失本次新增 cache 数据。

实现顺序：

1. 先建立结构化 `PipelineKey`，消除 name-only cache 的正确性风险。
2. Shader 热加载和多 pipeline 创建统一通过 `PipelineManager` 与 key/revision 路径。
3. 项目 `.comet/cache` 可用后增加 `PipelineCacheStore`，完成 blob 校验、加载和原子保存。
4. 通过日志和 profiler 比较 cold/warm pipeline creation 时间，但不把特定机器上的加速比例作为功能测试条件。

## 分阶段开发路线

### 阶段 0：基线收束

目标：把当前 demo 状态整理成可继续演进的稳定基线。

当前状态：已完成。`Renderer` 中 demo 资源的所有权已经被识别为应用层职责，实际迁移将在阶段 1B
随 Scene 和 Render Submission 一次完成，避免在相邻阶段重复改造。

已完成：

- 明确 demo mesh、texture、模型矩阵和固定 camera 属于示例场景，`Renderer` 只应保留渲染系统职责。
- 在 `docs/architecture/rendering-ownership.md` 记录 `RenderContext`、`SceneRenderer`、`ResourceManager`、
  Material runtime cache、VMA 资源和 Device 的所有权与析构顺序。
- 为 Buffer、Image、Device 和 ResourceManager 增加无效参数保护与单元测试。
- 将 engine、editor 和 app 的源码清单改为显式维护；测试源码 glob 使用 `CONFIGURE_DEPENDS`。
- 移除正常呈现路径中的逐帧 `queue.waitIdle()`，补齐 frame/image fence 关联，并按 frame-in-flight
  独立持有 descriptor set 和 uniform buffer。
- 将 `max_frames_in_flight` 显式配置为 2，与实际 swapchain image 数量解耦；command buffer 按 frame slot
  复用，render-finished semaphore 和 framebuffer 按 swapchain image 管理。
- 统一 swapchain acquire/present 的可恢复错误处理与重建路径。

验收标准：

- [x] 编辑器和 app 都能正常构建。
- [x] 当前 cube demo 可连续渲染，行为不回退。
- [x] README 与实际依赖、构建命令保持一致。
- [x] 关键资源释放顺序清晰，正常渲染路径无 Vulkan validation error。

### 阶段 1：Scene/ECS 与渲染提交 MVP

目标：建立真实场景数据模型，让引擎能从 Scene 渲染对象，而不是从 `Renderer` 内部硬编码对象。

当前状态：阶段 1A 至 1E 已完成；Scene、Camera、AssetHandle、多对象渲染、编辑器离屏视口和 Transform
层级已经建立。下一步进入阶段 2。

#### 阶段 1A：Scene/ECS Core（已完成）

- 新增 `engine/src/scene/` 模块。
- 基于 EnTT 实现 `Scene`、`Entity` 包装类型。
- 实现 `IdComponent`、`NameComponent`、`TransformComponent`、`MeshRendererComponent` 和 `CameraComponent`。
- 实现实体验证、创建、删除、按 ID 查询、遍历和通用组件操作。
- 使用单元测试约束 Entity 不公开 registry、原始 EnTT handle 和所属 Scene。

#### 阶段 1B：Scene Render Submission（已完成）

- [x] 定义最小 `AssetHandle`，包含无效值、比较和哈希能力。
- [x] 将 `MeshRendererComponent` 的 mesh/material 字符串替换为 `AssetHandle`。
- [x] 提供最小内存 Asset Registry，支持 app 将 demo `AssetHandle` 注册到运行时资源；暂不实现目录扫描和导入。
- [x] 定义最小 `RenderScene` / `RenderItem`，至少包含实体 ID、模型矩阵、mesh handle 和 material handle。
- [x] 实现局部 TRS 到模型矩阵的转换；Euler 角使用度，矩阵顺序为 `T * Rz * Ry * Rx * S`。
- [x] 新增 SceneExtractor，提取具有 Transform 与 MeshRenderer 的实体；Scene 组件不持有文件路径或 GPU 资源。
- [x] 让运行时层持有 Scene，由 app 创建 demo Scene 和 cube entity。
- [x] 让 `Renderer` 消费场景提交结果，逐项交给 `SceneRenderer` 绘制；消费端诊断并跳过无效或未解析的 Handle。
- [x] 使用 `SceneResolver` 和 `RenderSubmission` 封装资源解析；SceneRenderer 批量消费已解析对象并内部管理 per-frame UBO 与 descriptor。
- [x] 接入主 `CameraComponent` 驱动 view/projection；Camera FOV 统一使用角度，并校验 FOV、裁剪面和渲染尺寸。
- [x] 每个 draw 的模型矩阵使用 push constant，避免单一 model uniform buffer 阻碍多对象渲染。
- [x] 删除 `Renderer` 内部硬编码的 cube mesh、texture 和模型旋转逻辑。
- [x] 删除 `Renderer` 的固定相机业务状态，由场景主 Camera 提供 view/projection。

#### 阶段 1C：编辑器基础数据闭环（已完成）

- [x] Hierarchy 从真实 Scene 读取实体，先支持平铺列表，不提前实现伪层级。
- [x] 建立 Selection 服务，只保存并按需解析 `EntityId`，连接 Hierarchy 和 Inspector，并为后续 Viewport 拾取复用。
- [x] Inspector 直接读写 Name 与 Transform，并让修改进入下一帧的场景提取和渲染结果。
- [x] 支持创建、删除和重命名实体的最小编辑流程。
- [x] 实体删除或无效 ID 会自动清空 Selection，并有纯逻辑单元测试保护。

#### 阶段 1D：编辑器离屏视口基础（已完成）

- [x] 将编辑器的场景渲染目标从 swapchain 分离：场景进入离屏颜色/深度附件，swapchain 只承载 ImGui 和最终呈现。
- [x] 为 frame slot 提供独立的离屏资源，避免多个 frames-in-flight 同时读写同一张 viewport image。
- [x] 离屏颜色附件结束时进入 `ShaderReadOnlyOptimal`，并通过 ImGui Vulkan descriptor 注册为 `ImTextureID`。
- [x] `ViewPanel` 提供实际内容尺寸，Renderer 使用稳定尺寸按需重建离屏目标，并处理零尺寸和折叠状态。
- [x] 单一 Viewport 复用离屏提交基础；Edit/Play 当前都使用活动 Scene 的主 Camera，独立 editor camera 留到阶段 4。
- [x] 调整 ImGui swapchain render pass，使其负责清理编辑器背景和合成 UI，不再依赖先在 swapchain 上绘制场景。
- [x] 保持 runtime app 直接渲染到 swapchain 的路径不变。

验收标准：

- [x] 编辑器中的场景画面只出现在可见 View 面板内，不再铺满整个窗口背景。
- [x] View 面板 resize、折叠、切换和 swapchain recreation 后纹理仍正确，且没有 Vulkan validation error。
- [x] 两个 frames-in-flight 下不会复用仍被 GPU 或 ImGui 采样的离屏附件。
- [x] app 的直接呈现路径和现有多实体渲染行为不回退。

#### 阶段 1E：Transform 层级（已完成）

- [x] 新增父子关系组件和循环依赖保护。
- [x] 实现局部矩阵和世界矩阵的父级优先全量更新；reparent 保持局部 TRS。
- [x] SceneExtractor 提交世界矩阵，Camera 忽略自身局部 scale。
- [x] Hierarchy 显示真实父子树，支持拖拽 reparent 和拖回根节点。
- [x] 补充父子变换、销毁父节点、reparent 和非法层级的单元测试。

`LightComponent` 延后到阶段 5，在基础 Forward Lighting 真正消费它时再加入，避免只存在类型却没有运行路径。

验收标准：

- demo cube 不再由 `Renderer` 内部硬编码创建。
- 一个 Scene 可以提交并绘制多个 render items。
- Scene、MeshRenderer 和 RenderItem 之间只传递 `AssetHandle`，不传递字符串路径或 GPU 对象。
- 场景主 Camera 驱动 view/projection，FOV 单位明确且有测试保护。
- Hierarchy 可以从真实 Scene 读取实体名称。
- Inspector 至少可以编辑选中实体的 Transform。
- 单元测试覆盖实体生命周期、组件操作、RenderItem 提取和 Transform 计算。

### 阶段 2：场景序列化与编辑器闭环

目标：用户能在编辑器里创建、编辑、保存、加载一个简单场景。

当前状态：持久化 Entity UUID、开发期 `.scene` YAML、编辑器 New/Open/Save、descriptor 驱动的基础组件 Inspector、
descriptor 驱动的场景组件序列化和最小 Play/Edit 隔离均已完成；下一步进入阶段 3 的 Asset Database MVP。

建议任务：

- [x] 引入 128-bit `EntityUuid` 和 `UuidComponent`，并与运行时 `EntityId` 区分；支持生成、解析、格式化、
  比较、哈希、指定 UUID 创建、重复拒绝和按 UUID 查找。
- [x] 定义 `AssetHandle` 的 YAML 无符号整数表示；路径不进入 Scene，后续由 Asset Database 建立 Handle 到资产的稳定映射。
- [x] 定义带 `version` 字段的开发期 `.scene` YAML 格式，并记录实体、父 UUID 和基础组件契约。
- [x] 实现纯 Scene 的内存序列化往返和文件保存/加载；加载失败不会修改已有 Scene。
- [x] 菜单 New/Open/Save Scene 接入真实逻辑；Open 和首次 Save 使用路径输入，已有路径的 Scene 可直接保存。
- [x] New/Open Scene 后重绑定 Hierarchy 与 Selection，并在运行时 `EntityId` 可能复用时仍清理旧选择。
- [x] 建立 `ComponentRegistry`、`ComponentDescriptor`、`PropertyDescriptor` 和按属性类型分发的
  `PropertyEditorRegistry`。
- [x] 先为 Transform、MeshRenderer 和 Camera 显式注册元数据，Inspector 通过 descriptor 生成控件，不再为每个字段
  硬编码面板逻辑。
- [x] 场景序列化复用同一份属性元数据，由 stable ID、类型访问器和 `serializable` 标记定义当前活动格式。
- [x] 实现 Play/Edit 模式的最小切换；进入 Play 时通过内存序列化复制 Edit Scene，退出时丢弃 Runtime Scene 并恢复
  原 Edit Scene，切换时重绑定 Hierarchy/Selection，Play 期间禁用场景文件操作。
- [x] 为稳定排序、组件与层级往返、文件读写、重复 UUID、悬空父引用、层级环和格式错误补充单元测试。

验收标准：

- 在编辑器中新建场景，创建 cube/camera，保存后重启可以恢复。
- Inspector 修改 Transform 后，Viewport 立即反映。
- Inspector 可以通过通用 descriptor 编辑 Transform、MeshRenderer 和 Camera，新增已支持类型的字段时不需新增专用面板分支。
- Scene 文件可读、可 diff、可手动排查。
- Scene 保存和加载后，mesh/material 的 `AssetHandle` 保持不变。
- 场景序列化有单元测试。
- Play 中修改 Runtime Scene 后退出，Edit Scene 保持原值。

### 阶段 3：资产数据库与项目系统

目标：从“按路径加载文件”升级为“项目资产管理”。

项目目录契约：

```text
<ProjectRoot>/
├── assets/                # 受版本控制的源资产和与其相邻的 .meta
├── .comet/                # 不提交的项目本地数据
│   ├── cache/             # 可重建的导入产物、索引和缓存
│   └── editor/imgui.ini   # 当前机器的编辑器窗口与 Docking 布局
└── ProjectSettings/       # 受版本控制的项目级设置
```

`ProjectPaths` 已提供项目根目录、源资产、项目设置、`.comet` 本地数据、cache 和 editor 状态的统一路径解析；除实际消费者
按需创建 cache/editor 子目录外，它本身不执行 I/O。项目创建、打开与校验属于后续 Project System，Asset Database 只通过该契约定位输入和缓存。

资产身份契约：

- 每个源资产使用相邻的 `<filename>.meta` 保存非零 64 位 `guid` 和资产类型。
- `guid` 在代码层直接包装为 `AssetHandle`，二者数值一一对应，不增加运行期重编号或额外哈希映射。
- 新资产只在缺少 `.meta` 时生成随机身份；资产改名或移动必须携带原 `.meta`，Asset Database 会拒绝重复身份。
- 当前 `.meta` v2 包含 `version`、`guid`、`type` 和按资产类型校验的可选 Importer 设置；Texture 已定义
  `color_space` 与 `flip_y`。
- `.meta` 由编辑器在首次发现资产时自动创建，之后作为源资产的一部分长期保存并进入版本控制；它不是
  `.comet/cache/` 中可随时删除重建的缓存。
- Asset Inspector 是 `.meta` 的主要编辑入口：GUID 和识别出的资产类型只读，Importer 参数经过校验后写回并触发
  重新导入。当前开发格式允许手工编辑 YAML；资产源监视只触发 Asset Database 扫描，仍由数据库按同一契约校验。解析失败时保留上一次有效导入产物并报告错误。
  编辑器写入链路和 Metadata Schema 稳定后，`.meta` 应与 `.scene`、`.mat`、`ProjectSettings/` 一起迁移为统一 JSON 文档，
  而不是单独更换格式。
- `.meta` 只服务于编辑器、Asset Database 和导入管线，不原样进入 Shipping 资源包。发布流程消费 `.comet/cache/`
  中确认有效的导入产物，并生成描述打包资源、依赖和版本的最终 manifest。
- Asset Database 扫描 `assets/` 并建立 Handle 与项目相对路径的双向索引；扫描报告会收集重复 GUID、孤立或损坏
  `.meta`、类型不匹配和不支持的文件，有效资产仍可进入索引，不因单个坏文件丢失整个项目视图。

建议任务：

- 定义项目目录结构，例如 `assets/`、`.comet/cache/`、`.comet/editor/`、`ProjectSettings/`。
- 实现 Asset Database：
  - 持久化 GUID 分配
  - 元数据文件
  - `AssetHandle` 到元数据、源文件和导入产物的索引
  - 类型识别
  - 依赖查询
- [x] Project 面板读取真实资产目录并展示扫描问题。
- [x] 建立 Texture 同步纵向链路：Importer 输出 CPU `TextureData`，ResourceManager 创建 GPU Texture，Asset Registry 按 Handle 缓存。
- [x] 建立 Material 同步加载链路：`.mat` 通过 Texture Handle 表达依赖，AssetManager 解析依赖并将运行时 Material 发布到 Asset Registry。
- [x] Texture Importer 支持可持久化、可校验并实际参与导入的基础参数：色彩空间和垂直翻转。
- [x] Material 资产可通过编辑器修改 Texture Handle、保存，并在候选对象构建成功后显式替换运行时 Material。
- [x] Asset Database 从 MaterialData 建立正向/反向依赖索引，扫描时报告缺失或类型错误的引用，并供 Texture 重导入查询直接 Material 依赖。
- [x] Asset Database 先构建候选快照再提交并报告新增、删除、修改 Handle；Project Refresh 同步 Registry 与 Inspector，扫描发现阶段失败时保留上一份有效快照。
- [x] Scene、Material 和 `.meta` 复用原子文本替换；Texture/Mesh 的 CPU 创建数据从 Runtime GPU 类头文件中拆分。
- 在 Asset Database 的 metadata 和导入产物结构稳定后，设计 Shader Importer 的输入、编译结果、缓存键和打包方式；
  当前 CMake 编译链不预设对应的 C++ 类型或落盘格式。
- [x] Mesh Importer 接入 fastgltf，支持 `.gltf`/`.glb` 静态 triangle mesh，并由 app/editor 通过项目 AssetHandle 加载示例 cube。
- [x] 建立 `.comet/cache/imported/mesh/<AssetHandle>.bin` 纵向切片；`MeshImportCache` 保存显式格式和 Importer 版本、项目内输入内容指纹与完整性校验，缺失、过期或损坏时自动重新导入并原子替换。
- [x] 缓存命中时恢复 Mesh Importer 源依赖，Asset Database 建立源路径正向/反向索引；`.bin` 作为辅助输入不生成资产身份，其变化会推进所属 Mesh revision 并复用后台刷新链路。
- [x] 将磁盘变化签名与 `AssetRevision` 分离；每次数据库提交的资产变化分配单调 revision，Mesh 候选在发布到 Registry 前验票并丢弃过期结果。
- [x] Asset Registry/Asset Manager 负责把 `AssetHandle` 解析为资产元数据和同步导入结果。
- [x] Asset Registry/Asset Manager 以 `AssetHandle` 为唯一缓存键创建或复用 Runtime Asset；ResourceManager 不承担资产扫描或 Handle 缓存。
- 完成 GFX-002 后让现有 `Device` 暴露不可变 `CapabilitySet`，统一保存实际启用的 feature/extension、queue family、
  limits 和 format 能力；明确它承担 GraphicsDevice 职责，但不迁入 FrameSlot、资产或 RenderTarget 所有权。
- [x] 建立最小 `TaskScheduler`/worker pool，支持 FIFO 提交、Future、等待空闲和安全 drain；Engine 统一持有，测试可显式使用单 Worker。
- [x] 将已加载 Mesh 的缓存读取和 CPU 导入移出主线程；worker 只产出候选结果，Owner Thread 验证 revision 后才写缓存、创建 Runtime Mesh 和替换 Registry，失败时保留旧 Mesh。同步首载暂时保留。
- [x] 将已加载 Texture 的文件读取和图片解码移出主线程；Mesh/Texture 共享任务占位与 pending revision 去重，Owner Thread 双重验票后才创建 GPU Texture、替换 Registry 并刷新已加载 Material 依赖。
- [x] 建立低频 `AssetSourceMonitor`：只观察 `assets/` 文件树并触发既有 scan，目录不可访问时保留上一快照，Editor 自身已知写入按精确路径确认；Project 与 Inspector 在 Owner Thread 消费同一扫描报告。
- [x] 在现有显式同步路径完成 Synchronization 2 迁移：通过 GFX-002 的 Vulkan 1.3 feature chain 查询并启用
  `synchronization2`，将 queue submit 改为 `vk::SubmitInfo2`/`submit2()`，将显式 image/buffer barrier 改为
  `vk::DependencyInfo` 和 `vk::ImageMemoryBarrier2`/`vk::BufferMemoryBarrier2`。
- [x] 定义类型化 `ResourceUsage`、`ResourceState`/`ImageState` 与 usage-to-state 映射，包含 queue-family owner、
  image subresource range 和不完整输入的拒绝规则，并使用纯单元测试覆盖。
- [x] 让现有 Texture upload 提供 known-before/desired-after state，删除 layout-pair `if/else` 和 legacy
  `pipelineBarrier()`；传统 RenderPass 的隐式 attachment 转换继续由 RenderPass 契约表达。
- [x] 用现有 swapchain frame submit、texture upload 和 buffer copy 路径保持迁移等价性，并引入 Queue 独占 timeline
  semaphore 和 `GpuCompletionPoint`；没有把 API 迁移、异步上传和 transfer queue 合并成一次改动。
- [x] 建立最小 `UploadManager`，集中持有 staging、upload command context、目标资源和 completion；一个 Mesh 的
  vertex/index copy 合并为一次 submission，不再由 Buffer/Texture 各自创建临时 CommandContext。
- [x] 明确阻塞式上传和显式 `UploadBatch` 的同步/提交契约；batch submit 返回 `GpuCompletionPoint`，未提交 batch 析构只回滚自身。
- [x] 将每次 enqueue 的独立 staging allocation 演进为可复用 page 子分配；page 由 pending batch 独占，并在其
  timeline completion 满足后回池。更细粒度 ring 回收等 profile 证明有必要后再增加。
- [x] 异步资源携带 ready token，首次 graphics 消费在准确 stage 等待 upload timeline value；同一 submission 内按
  timeline 合并最大 value 与 stage，同一有序 queue 上的冗余 wait 后续可由 backend 消除。
- [x] 建立 `GpuCompletionPoint`，让 Queue submission 返回单调 timeline value；异步资源和 retirement queue 保存该
  token，FrameSlot 只保存 fence 覆盖的单调 frame serial，不重复保存未参与帧调度的 timeline token。
- [x] 建立 owner-thread `GpuRetirementQueue`。SceneRenderer 将每帧实际录制的 Runtime GPU owner 绑定到 frame timeline
  completion，UploadManager 继续按 upload timeline value 回收 staging 与 command resources。
- [x] 将 `FrameManager` 收敛并更名为 FrameScheduler contract：统一 slot index、单调 frame submission serial，以及
  wait slot、collect retirement、acquire、reset fence、record submission、advance slot 的顺序；per-frame arena 后续迁入。
- [x] 让 FrameScheduler 从实际 fence wait 推进 completed frame serial；Material descriptor cache 记录最后使用 serial，
  仅在对应 submission 完成后回收，现阶段不创建没有实际 allocation 需求的通用 DescriptorArena。
- [x] `UploadManager` 根据 timeline value 延迟回收 staging page、upload CommandContext 和上传期间临时持有的目标资源；显式
  batch 可由调用方等待自己的 completion，用于启动期、工具和测试。
- [x] ResourceManager 创建 Mesh/Texture 等 GPU Resource 时通过 UploadBatch 提交数据，不直接管理 staging lifetime；
  pending resource 携带 ready token，首次 graphics 消费建立 GPU-side wait。
- [x] 将 `VK_EXT_memory_budget` 作为 optional device capability；只在扩展实际启用后为 VMA allocator 设置
  `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`。
- [x] memory budget 启用后，每个渲染帧用单调递增的 frame serial 调用 `vmaSetCurrentFrameIndex()`，不传循环变化的
  FrameSlot 下标。
- [x] 通过 Allocator/Device 暴露按需 `MemoryBudgetSnapshot`，将 `vmaGetHeapBudgets()` 的 heap 统计转换为 Comet 类型，
  并标记数据是驱动报告值还是 VMA 估算值。
- [x] UploadManager 在 staging pool 确实需要增长时采样 budget snapshot；空闲池只保留有限数量的默认 page，超大 page
  完成后直接释放，高压力增长前释放全部空闲页，并对连续压力做边沿触发诊断，不在每帧无条件采样。
- 区分关键 GPU 资源和可降级 streaming 资源。只有 ResourceManager 已支持返回错误、占位资源、重试或淘汰后，
  才为非关键 allocation 使用 `WITHIN_BUDGET`；render target 等关键资源保留独立失败策略。
- [x] Allocator 先建立双轨创建契约：现有 `create_buffer/image` 保持强失败，`try_create_buffer/image` 返回显式结果；
  `WITHIN_BUDGET` 仅通过默认关闭的 allocation 选项启用，不改变任何现有关键资源调用点。
- [x] Buffer/Image owning wrapper 增加静态尝试创建：allocation 成功后才构造 GPUBuffer/OwnedImage，旧公开构造入口收回，
  因而失败结果不会携带可发布的半初始化包装对象。
- [x] UploadManager 增加可恢复 staging page 创建和 `try_enqueue_upload()`；失败会 discard 未提交 CommandContext 并 abort
  整个 active batch，强失败 enqueue 与 recoverable enqueue 共享录制逻辑。
- [x] 支持资产改名、移动后的引用稳定性：Project 面板只提交 Handle 与项目相对目标，AssetManager 成对移动 source/sidecar，
  以数据库快照作为提交点并在失败时回滚；Selection、Inspector、Log 与源监视器消费同一结果。

完整资源路径：

```text
Scene Component / RenderItem
          AssetHandle
              ↓
    Asset Registry / Asset Manager
              ↓
 metadata + source/imported artifact
              ↓
        Asset Registry cache
              ↓
     Mesh / Texture / Material
```

验收标准：

- Project 面板显示真实 `assets/` 目录。
- 拖拽 mesh/material 到实体后可以保存并重新加载。
- 删除或移动资产时有基本错误提示。
- 资产数据库重建后，场景引用仍然稳定。
- Scene 和编辑器业务代码不直接使用资产文件路径获取 Runtime/GPU Resource。
- GraphicsDevice/`Device` 的 capability snapshot 与实际创建 logical device 时启用的 feature/extension 一致，且
  Device 不反向拥有 FrameScheduler、ResourceManager 或 RenderTarget。
- engine-owned submit/barrier 主链路不再使用 legacy `vk::SubmitInfo`、`vk::ImageMemoryBarrier` 或
  `pipelineBarrier()`；每个 wait semaphore 都显式携带自己的 stage mask 和 timeline value。
- Synchronization 2 迁移后，现有 frame fence、swapchain semaphore、layout transition 和阻塞式 upload 行为保持
  正确，validation 无 stage/access、layout 或资源生命周期错误。
- upload 与 swapchain 的 barrier 由类型化 state 映射生成；unknown initial state 会被拒绝，常见 read/write hazard、
  subresource range 和 usage mapping 有不依赖真实 GPU 的单元测试。
- 批量加载资源时不会为每个 buffer/texture 单独执行 `queue.waitIdle()`；一次 batch 中的 copy 和 layout transition
  可以合并提交。
- staging allocation、upload command buffer 和目标资源的可用状态均由 completion token 约束；GPU 完成前不会
  提前释放，也不会在缺少对应 GPU-side wait 的 submission 中被消费。
- 异步上传不会通过 CPU wait 才允许首次使用；graphics submission 会消费 ready token 并建立 GPU-side wait，
  `upload_and_wait()` 仍提供确定性的阻塞路径。
- retirement queue 使用可注入的 completed serial/value 测试 last-use、跨 queue completion 和回收顺序；旧资源在
  完成点之前保持 owning reference，完成后只析构一次。
- FrameScheduler 的 reset-order 测试证明 completion 前不会 reset command/descriptor/transient arena，slot 复用后
  submission serial 单调递增。
- 大批量导入时窗口事件和编辑器交互不会被文件读取、解码或 CPU importer 长时间阻塞；测试可切换到确定性的
  single-thread executor。
- memory budget capability、VMA allocator flag 和每帧 frame serial 保持一致；内存不足时非关键资源可以失败并
  进入占位/重试路径，而不是直接终止进程。

### 阶段 4：编辑器视口与交互能力

目标：让编辑器真正可用，而不是只显示数据。

本阶段建立在阶段 1D 的离屏视口基础上，不再重复解决 RenderTarget 到 ImGui 纹理的接线。

当前基线：

- editor 只有一个 ViewportPanel 和一组按 frame slot 分配的离屏纹理，面板尺寸直接驱动同一个 RenderTarget。
- Edit 使用 Edit Scene 和不属于 Scene 的 editor camera；Play 使用 Runtime Scene 的 primary Camera。Viewport 可见性、目标尺寸和
  Camera 来源由 ViewportRenderRequest 表达，SceneRenderer 不依赖 EditorMode；输入焦点留在 Editor 交互层。
- 2D/3D 是单个 Edit Viewport 的观察和交互方式，不是两个独立 Viewport；当前按钮只保存 UI 状态，尚未真正切换 editor camera 投影和操作逻辑。
- resize 会等待尺寸连续稳定，再事务式创建新的离屏 target generation；正常切换不等待 `Device::wait_idle()`。
- Camera 垂直 FOV 与实体 Transform 不变，RenderTarget 尺寸只改变 projection aspect；ImGui 再把纹理等比放入面板。
- Viewport layout 已区分 ImGui 逻辑尺寸、HiDPI 物理渲染分辨率和实际 image display rect。

#### 阶段 4A：单 Viewport 的模式化 Camera 与输入

- [x] 建立明确的 viewport render request，保存可见性、Camera 来源和 RenderTarget 目标尺寸，不把 UI 模式塞入 `SceneRenderer`。
- [x] Edit 使用 editor-only camera；该 Camera 不属于 Scene entity，不参与场景保存，也不影响 runtime 主 Camera。
- [x] Play 使用 Runtime Scene 中的 primary Camera，并在没有有效主 Camera 时只清屏和显示诊断。
- [x] Viewport 隐藏或折叠时跳过 resize。
- [x] Edit 模式的鼠标输入只从可见画面区域激活 editor camera；RMB orbit、MMB pan 和滚轮 zoom 由 backend-neutral controller 消费，
  拖拽激活后持续到按键释放。输入策略不进入 Renderer 请求。
- [ ] Play 模式的游戏输入转发随运行时 Input System 接入，不复用 editor camera 输入快照。
- 只有出现 Play 中脱离游戏相机调试 Runtime Scene 的真实需求后，再增加 Eject/Debug Camera，不提前维护第二套 Viewport。

验收标准：

- 进入 Play 时同一 Viewport 切换到 Runtime Scene primary Camera，Stop 后恢复 Edit Scene 和 editor camera。
- 移动 editor camera 不会修改 Scene，Play 中的 Camera 和运行时修改也不会污染 Edit Scene。
- 两个 frames-in-flight 下不会读写仍在使用的离屏附件。

#### 阶段 4B：渲染分辨率与显示策略

- [x] 明确区分 panel content size、render resolution 和 image display rect，不再把三者视为同一尺寸。
- [x] Edit 模式默认按面板物理像素尺寸渲染，并结合当前 ImGui platform viewport 的 framebuffer scale 处理 Retina/HiDPI。
- [x] Play 模式支持 Free、16:9、1280x720、1920x1080 分辨率策略；固定像素模式下 panel resize 只改变显示缩放，不改变 render resolution。
- [x] 提供 Fit、1x 显示倍率，保持物理像素语义并记录 letterbox/pillarbox 或 1x 裁切后的真实 image display rect。
- [x] 保留 resize debounce，并用 frame submission completion 和延迟销毁替代 `Device::wait_idle()`，避免拖拽面板时阻塞整个 GPU。
- [x] resize 创建完整的新 `MultiTarget` generation，成功后切换 viewport 引用，并按最后使用它的 frame submission 延迟释放
  旧 image、image view、framebuffer 和 ImGui descriptor；创建失败时继续使用旧 generation。
- [x] 建立 swapchain dependent release/rebuild 边界：等待全部 graphics frame-slot fence 与 present queue 后先释放 runtime/ImGui framebuffer target，再替换 core；
  重建延期时恢复旧 core 的 dependent，避免 framebuffer/image view 晚于父 swapchain 销毁。
- [x] 将 core handle、borrowed images、config 和 current image index 收敛为 `SwapchainGeneration`；候选完整后才提交 active shared
  owner，`vkCreateSwapchainKHR` 失败不覆盖旧 generation。
- [x] 让 runtime/ImGui SwapchainTarget 显式共享其 core generation，并以纯 compatibility diff 区分 extent、format、image count
  失效；editor 按 diff 精确重建 backend。
- [x] 将 swapchain core 与 runtime/editor dependent 分层持有，并按 extent、format、image count compatibility 精确失效；
  core 创建失败不提交候选，dependent 始终共享其构造时对应的 core generation。
- [ ] 将 runtime format/sample-count-dependent RenderPass 与 Pipeline 收敛为可事务替换的 generation；该工作与阶段 5 的
  PipelineKey/RenderGraph 生命周期一起完成，当前 format 变化明确终止，绝不继续使用不兼容 pipeline。
- [x] old swapchain 只有在 graphics use 和 presentation use 都完成后释放；没有 present completion 能力的平台保留
  present-queue idle 回退，不用不相关的全局 Device idle 代替依赖判断。
- [x] 为超大 View 增加设备硬上限与 editor 软上限约束，等比限制最终物理分辨率，避免无上限重建离屏资源。

验收标准：

- Viewport 在不同 DPI 和窗口缩放下保持清晰，RenderTarget 像素尺寸与实际显示需求一致。
- Play 模式切换固定分辨率时 Camera aspect、输出纹理和留白区域正确，场景对象不会被非等比拉伸。
- 连续拖拽面板不会每帧重建资源，也不会依赖全局 Device idle。
- 新 swapchain handle 创建失败不会覆盖或提前销毁 current generation；创建成功后不会尝试从已 retired 的 old
  swapchain acquire。重建后 image count、format-dependent RenderPass/Pipeline、FrameScheduler image state 和 ImGui
  backend 保持一致，旧 generation 不会在 presentation 完成前释放。

#### 阶段 4C：视口交互闭环

- [x] 基于 image display/visible rect 将屏幕坐标映射到当前实际 RenderTarget 像素，排除工具栏、letterbox/pillarbox、
  最大边界和 OneToOne 裁切区域；resize debounce 期间不使用尚未发布的请求分辨率。
- [x] Viewport 在 Edit 模式实现 editor camera 的 RMB 环绕、MMB 平移和滚轮缩放；UI prepare 在 SceneExtractor/SceneResolver 前执行，当前帧直接消费最新 camera 和 Scene snapshot。
- [x] 让 2D/3D 按钮切换真实投影与操作策略：RenderCamera 显式表达 Perspective/Orthographic，2D 固定 +Z 观察轴、
  使用屏幕 XY 平移与正交高度缩放并禁用 orbit，不用极端透视参数模拟。
- 默认保持一个 Viewport 并在内部切换 2D/3D，共享同一组 RenderTarget、拾取和 gizmo 上下文。只有当多视图同时对照成为明确工作流时，
  才增加可停靠的多 Viewport；每个同时可见视口应拥有独立 Camera、尺寸、frame-slot RenderTarget 和渲染提交，隐藏时必须跳过渲染。
- 实现对象拾取、Selection 同步、移动/旋转/缩放 gizmo 和选中对象高亮。
- [x] 完成拾取技术审计并建立 CPU bounds 基础：MeshData 计算有限 local AABB，Runtime Mesh 与 GPU buffer 同候选保存；该数据同时服务
  CPU ray picking、Focus Selection、culling 和 debug draw，不保留完整 CPU 顶点副本。
- [x] 实现 CPU ray-local-AABB 最近命中与 Selection 同步：点击事件映射为当前纹理 pixel，Renderer 复用当前 RenderSubmission/Camera，
  空白点击清除选择且查询不依赖 ImGui 或 GPU 等待；GPU ID attachment/readback 留到真实几何精度需求出现并与阶段 5 RenderGraph
  resource/pass contract 一起设计。
- [x] 增加 Focus Selection：Viewport 聚焦时的 `F` 产生一次事件，Editor 用 Selection 实体 world transform 与 Runtime Mesh local bounds
  计算 world AABB；Perspective 保留观察方向并按水平/垂直 FOV framing，Orthographic 按 world XY 与 aspect 调整高度，不修改 Scene。
- [x] 完成选中高亮/gizmo/Undo 技术审计：删除未编译且协议过时的 axis/cube/triangle 与旧 PBR Shader 草稿；确定先建立
  Editor Command History，再以通用 DebugDraw line path 实现 bounds 高亮并供 gizmo 复用，GPU outline/ID pass 留给阶段 5 RenderGraph。
- [x] 建立 Editor Command History 并接入 Inspector 实体属性：ImGui 激活到释放形成一个 before/after 事务，命令通过 EntityUuid 与
  component/property stable id 重新解析，MenuBar 与快捷键统一 Undo/Redo；Scene owner 切换清空历史，不持有组件裸指针。
- [x] 建立通用 DebugDraw line path：无后端类型的 CPU list 生成 line/AABB，独立 line pipeline 使用当前 Camera/depth/MSAA；每个 FrameSlot
  独占可恢复增长的 mapped vertex buffer，Renderer 一次性提交，模块不依赖 Selection、Scene、Material 或 ImGui。
- [x] 接入 selected bounds highlight：Editor 在 Edit/Viewport 可见且选中实体能解析有效 Runtime Mesh 时复用 Focus 的 world bounds 语义，
  向现有 DebugDraw list 提交 12 条 depth-tested 高亮线；Play、资产选择和缺失 Mesh 不产生提交。
- [x] 建立 Global Translation Gizmo 事务：连续 texture-pixel pointer 输入区分工具命中与场景拾取，纯 Editor controller 完成轴投影命中、
  ray-axis 拖拽和 parent-local 换算；实时 Transform 在 release 时只登记一个现有属性命令，取消会恢复开始值。
- [x] 将 RenderScene 快照延迟到 overlay prepare 之后：Renderer 以 provider 保持完整帧事务，Engine 在 UI/Gizmo 修改 Scene 后再提取当前 owner，
  Inspector/Gizmo/bounds 与 Mesh model matrix 同帧，不暴露 begin/render 半开状态。
- 如果对象拾取采用 GPU ID buffer，按请求或 FrameSlot 持有 host-visible readback buffer；copy 完成并确认
  fence/timeline 后再 invalidate/read。若采用 CPU ray cast，则不为了预留能力提前建立通用 readback 系统。
- 为 Rotation/Scale Gizmo 复用现有 Undo/Redo 事务，并支持复制、粘贴、删除和 duplicate。
- 支持 prefab 的最小版本，至少能保存一组实体为可复用资产。

验收标准：

- 用户可以通过鼠标在 Edit 模式 Viewport 精确选择对象，点击留白区域不会产生错误拾取。
- 用户可以用 gizmo 修改对象并保存场景，Transform 修改可撤销和重做。
- Viewport 的拾取坐标、gizmo 和高亮在面板 resize、DPI 变化和显示倍率切换后仍与画面一致。

### 阶段 5：渲染系统升级

目标：从基础 forward demo 进入可扩展渲染器。

建议任务：

- 抽象 RenderGraph 或轻量 RenderPass Pipeline。
- RenderGraph pass 显式声明资源读写 usage 与 subresource，只有 imported/exported resource 声明边界状态；由 graph
  compiler/state tracker 生成 Synchronization 2 barrier，pass execute callback 不直接调用 layout-pair transition。
- 在 RenderGraph pass 描述稳定后验证 Dynamic Rendering：比较对象/缓存复杂度、validation 结果和目标平台性能，
  再决定默认采用 `beginRendering()`、保留传统 RenderPass，或由 backend 按 pass 选择；不先做全量 API 替换。
- 移除 `SceneResolver` 中的 `u_Texture0/1` 和两张纹理假设，只解析 Mesh 与 Material Asset。
- 建立 `ShaderInterface`、`MaterialLayout`、material/layout revision、`PreparedMaterial` 和
  `MaterialRuntimeCache`。
- 将 frame/material/object descriptor 按更新频率分层，保证同一材质在多个 render item 之间复用已准备绑定。
- 建立 `FrameDescriptorArena[slot]`、分页 `PersistentDescriptorArena` 和 editor-owned `ImGuiDescriptorPool` 的所有权
  边界；frame arena 在 slot completion 后整体 reset，persistent set 随 material/global generation 退休。
- FrameSet 按 FrameSlot 分配；MaterialSet 按 material revision 创建不可变版本并跨 slot 复用，替换后通过 frame
  completion 延迟释放旧版本。只有引用 per-frame backing storage 的 material 参数保留局部 per-slot state。
- 扩展阶段 1 的基础 Render Queue，支持多 mesh、多 material、多 camera，并按 pipeline/material 排序和批处理。
- 将大量 object data 从“每对象独立 allocation/update”演进为每 FrameSlot 独立的 ring buffer；按设备的
  `minUniformBufferOffsetAlignment`/`minStorageBufferOffsetAlignment` 分配 offset，并根据数据规模选择 dynamic UBO
  或 SSBO。
- per-frame 小型固定数据继续使用独立 persistent-mapped buffer；只有对象数量和 profile 证明有收益时，才并入
  通用 frame-data arena。
- 建立独立 `RenderThread`。Main/Update thread 生成不可变 `RenderFramePacket`，通过容量不超过 frames-in-flight
  的有界队列提交；RenderThread 独占 Renderer 可变状态、Vulkan command recording、queue submit/present 和
  GPU deferred destruction。
- 将 Engine 主循环中的 scene extraction/render submission 编排收敛到 Main/Update 侧 `RenderSystem`；它只生成
  `RenderScene`/`RenderFramePacket`，不持有 Vulkan object，也不替代 Renderer/RenderThread。
- RenderThread 独占 `GpuRetirementQueue::collect()`；热重载、资产卸载、pipeline/material revision 替换和 streaming
  eviction 都以实际 last-use completion point 退休旧 owner，正常替换路径不调用 `Device::wait_idle()`。
- 为 editor frame packet 建立 owned ImGui draw data 或等价的 UI render packet，并把 viewport resize、Shader
  reload 和资源替换转换为在 RenderThread 帧边界消费的命令。
- 在 CPU/GPU profiler 证明 render preparation 成为瓶颈后，再使用 TaskScheduler 并行执行 culling、sorting、batch
  preparation；只有 pass/draw 数量足够时才引入 secondary command buffers，并为每个 recording worker 使用独立
  per-FrameSlot CommandArena/CommandPool。
- 新增 `LightComponent`，并接入真实 Forward Lighting 提交流程。
- 基础光照模型：方向光、点光、聚光灯。
- Shadow Map。
- PBR 材质基础：base color、normal、metallic、roughness。
- build-time compiler 和 editor compiler 向 Runtime 提供一致的 Shader 编译结果接口；Shipping 只消费打包数据，
  editor 热加载可以直接使用内存中的候选结果，不以松散 `.spv` 文件为必要条件。
- 先使用手工 `MaterialLayout` 跑通动态参数链路，再通过 SPIR-V shader reflection 生成低层
  descriptor/push-constant `ShaderInterface`。
- Material Inspector 通过 `MaterialLayout` 和 material metadata 生成参数控件。
- 将 `PipelineManager` 从 name-only map 改为结构化 `PipelineKey`；Shader、layout、render state 和 render-target
  compatibility 任一变化都会生成不同 key，name 只保留为调试标签。
- 增加 `PipelineCacheStore`：从 `.comet/cache/vulkan` 加载经过设备/驱动校验的 cache blob，通过
  `PipelineCacheCreateInfo::initialDataSize`/`pInitialData` 创建 cache，并在 pipeline compile burst 后或正常关机时
  原子保存。
- 建立 editor-only `FileWatcher -> ShaderCompiler -> Shader Reflection -> Pipeline Reload` 链路；编译与 reflection
  在后台执行，GPU 对象只在渲染线程的安全帧边界替换。
- Shader 热加载后按新旧 `ShaderInterface` 是否兼容决定只重建 pipeline，还是同步重建 layout 并失效
  `MaterialRuntimeCache`。
- 后处理：tone mapping、gamma、简单 bloom。
- GPU/CPU 性能统计面板；内存面板低频显示 `vmaGetHeapBudgets()`，`vmaCalculateStatistics()` 和详细 allocation
  dump 只在手动诊断时执行。

验收标准：

- 一个场景中可稳定渲染多个对象、多材质、多光源。
- RenderGraph/pass 描述不直接暴露 `vk::RenderPass` 或 `vk::Framebuffer` 所有权；Dynamic Rendering 验证结果记录了
  支持范围、传统 RenderPass 保留场景与平台测试结论。
- RenderGraph 能根据 pass contract 为 image/buffer 生成必要且范围正确的 barrier；未知 imported state、跨 queue
  handoff 和越过 tracker 的资源操作会被明确拒绝或要求显式声明。
- `SceneResolver` 不知道材质属性名、纹理数量和 descriptor binding，源码中不再依赖固定
  `u_Texture0/1` 链路。
- 至少两种不同 shader/pipeline layout 的材质可在同一场景正确渲染，纹理和标量/向量参数均由
  `MaterialLayout` 验证和绑定。
- 材质未变化时不会每帧重复解析属性或重写 descriptor，在多个 draw 之间正确复用运行时缓存。
- FrameDescriptorArena 不会覆盖 in-flight slot 的 set；PersistentDescriptorArena 的 material/global set 可跨 slot
  复用并随 generation 延迟释放；ImGui descriptor 不从 runtime pool 分配。
- 材质参数在 Inspector 修改后实时生效。
- 编辑器中修改合法 Shader 后无需重启即可更新画面；编译失败时保留旧 pipeline 并显示定位到文件/行号的
  诊断。
- texture/shader/pipeline/material 热替换和资产卸载不会销毁仍被 in-flight submission 引用的 GPU 对象，也不会通过
  全局 Device idle 阻塞正常帧循环。
- 相同 `PipelineKey` 在同一进程内复用 pipeline，不同 Shader/layout/state/attachment signature 即使 name 相同也
  不会错误复用；key/hash 逻辑有纯单元测试。
- 兼容 cache blob 可以在下一次启动时作为 `initialData` 加载；截断、损坏、UUID 或设备不匹配的文件会被忽略并
  回退为空 cache。cache header/envelope 解析有不依赖真实 GPU 的测试。
- pipeline cache 文件通过临时文件和 atomic rename 保存，不进入源码资产或版本控制；RenderThread 模型下不会
  从多个线程并发访问同一个 `vk::PipelineCache`。
- Shader 接口变化后，相关 layout、material binding 和 `PipelineKey`/Pipeline 对象缓存被正确失效或重建，
  或给出明确的不兼容错误；
  旧 GPU 对象在对应 in-flight frame 完成后才销毁。
- 大量对象数据每帧通过有界的 FrameSlot ring/dynamic UBO/SSBO 更新，不为每个对象重复创建 buffer allocation；
  offset 满足设备 alignment，且不会覆盖仍被 in-flight frame 使用的数据。
- Main/Update 与 RenderThread 之间只通过有界、owned frame packet 通信；慢渲染不会导致队列、延迟或 GPU resource
  引用无界增长。
- RenderSystem 只读取 Main/Update 所有的 Scene 并生成 packet；Renderer/RenderThread 不读取可变 registry，
  GraphicsDevice 不拥有 RenderSystem 或业务资产。
- Scene/EnTT 修改、ImGui draw data、Asset Registry/渲染派生缓存和 Vulkan Queue 均有唯一 owner，ThreadSanitizer/validation
  测试不报告跨线程生命周期或外部同步错误。
- 并行 command recording 启用时，每个 `FrameSlot + worker + queue family` 使用独立 CommandArena/CommandPool，
  关闭该优化后仍可走确定性的单线程路径。
- 渲染路径不依赖硬编码 cube pipeline。
- 有基础性能统计，能定位 CPU/GPU frame time。

### 阶段 6：运行时游戏能力

目标：支持制作一个小型可交互 3D demo。

建议任务：

- 输入系统：键盘、鼠标、手柄抽象。
- Native Script 组件。
- Script 生命周期；Inspector 字段暴露复用阶段 2 的 descriptor 和 `PropertyEditorRegistry`，不另建一套反射协议。
- Fixed Update。
- 建立 TransformSystem：收口 Inspector、gizmo、脚本和运行时的 Transform 修改入口，以系统持有的 dirty 集合
  增量更新受影响子树；保留全量更新路径作为正确性基线和测试对照。
- 复用阶段 3 的 TaskScheduler 执行具有明确读写集合的 Transform propagation、animation preparation、AI 或物理
  辅助任务；系统依赖由显式 phase/barrier 组织，不并行调用可能写入同一组件集合的任意脚本 callback。
- 物理引擎接入，建议先接入 Jolt 或 Bullet。
- Audio 基础，建议先接入 miniaudio。
- Runtime UI 的最小方案。
- Play 模式 Scene clone，退出 Play 不污染编辑数据。

验收标准：

- 能做一个可移动角色、可碰撞场景、带声音反馈的小 demo。
- Play/Edit 切换稳定。
- 脚本能读取输入、修改 Transform、触发音效。
- 物理调试绘制可在编辑器中打开。
- 多线程系统在相同输入和固定时间步下保持可重复结果，单元测试可以强制 single-thread execution。

### 阶段 7：内容生产与发布

目标：让 Comet 支持小型项目从编辑到打包的完整流程。

建议任务：

- Build Settings。
- 资源打包格式。
- 根据 Asset Database 和 `.comet/cache/` 导入产物生成发布 manifest；Shipping 包排除源资产 `.meta` 和编辑器专用的
  Importer 配置，只包含运行时所需资源身份、依赖和打包位置。
- Runtime player 可加载打包资源。
- 示例项目模板。
- 项目设置面板。
- Crash/log 输出目录规范。
- CI 构建、测试、打包。
- 文档站或系统化开发文档。

验收标准：

- 一个示例项目可以从编辑器打包为独立可执行程序。
- 打包产物不依赖源码目录。
- Shipping 资源包不包含松散 `.meta`，Runtime 可通过发布 manifest 解析所需资产和依赖。
- CI 能至少构建 engine、editor、app、tests。
- 新贡献者能按文档完成环境初始化、构建、运行和测试。

## 推荐的优先级顺序

近期最应该做：

1. [x] 引入持久化 Entity UUID，并明确它与运行时 `EntityId` 的边界。
2. [x] 定义 `.scene` YAML 格式并实现保存、加载。
3. [x] 接通 New/Open/Save Scene 和 Selection 生命周期。
4. [x] 预留场景版本字段并严格校验当前格式；schema 冻结和迁移器推迟到项目格式稳定后。
5. [x] 为序列化往返、无效引用和加载失败补充测试。
6. [x] 实现最小 Play/Edit 切换，运行时修改不污染编辑态 Scene。

暂时不要急着做：

1. 完整 PBR/Deferred/RenderGraph。
2. 复杂脚本语言。
3. 完整物理和动画系统。
4. 多平台打包。
5. 大规模材质图/节点编辑器。
6. 在 Scene/资源生命周期尚未稳定前提前拆独立 RenderThread 或并行 command recording。

Scene、编辑器、持久化和最小 Play/Edit 生命周期已经形成第一段纵向闭环。下一步进入 Asset Database，先稳定项目资产身份、
路径和导入产物边界，再扩展材质编辑、资源拖拽、热重载和 streaming。

## 12 个月建议里程碑

以下时间按一名全职开发者或小团队持续投入估算。兼职开发应保留里程碑顺序，不强行套用月份。

### 第 1-2 个月

- 完成最小 `AssetHandle` 和内存 Asset Registry。
- 完成 Scene Render Submission。
- Renderer 改为消费 RenderScene/RenderItem。
- 主 Camera 驱动渲染。
- 编辑器显示真实实体和 Transform。
- 编辑器使用离屏目标在 View 面板中显示场景，不再把场景铺满 swapchain。
- 完成基础父子层级与世界矩阵。

### 第 3-4 个月

- 引入持久化 Entity UUID。
- 完成 `.scene` 保存/加载。
- 完成 New/Open/Save Scene。
- Inspector 支持 MeshRenderer 和 Camera。
- 单一 Viewport 按 Edit/Play 切换 editor camera 和 game camera。

### 第 5-6 个月

- 完成项目系统和 Asset Database MVP。
- Project 面板绑定真实资产目录。
- Material 和 Texture 资产可编辑、可保存。
- 支持 glTF 静态 mesh 导入。
- 建立 CPU worker pool，后台执行文件读取、纹理解码和 mesh importer，并支持测试用 single-thread executor。
- 完成 Synchronization 2 feature/submit/barrier 迁移，并用现有渲染与阻塞上传链路完成 validation 回归。
- 固化 GraphicsDevice capability snapshot 和 FrameScheduler 的 completion/reset contract。
- 用类型化 `ResourceState`/`ImageState` 覆盖 upload、swapchain 和现有 RenderPass 转换，不再扩展 layout-pair
  `if/else`。
- 批量创建 GPU Resource 时通过 `UploadManager` 合并上传，并用 completion token 管理 staging 生命周期。
- 异步 upload ready token 可以直接转成首次 graphics consumption 的 GPU-side wait，不要求主线程等待上传完成。
- 建立 frame-fence/timeline completion point 与统一 retirement queue，资源在 last-use 完成前保持有效。
- 接入 optional VMA memory budget，低频采集 heap budget，并为非关键 streaming 资源建立可恢复失败路径。

### 第 7-8 个月

- 补齐 Rotation/Scale Gizmo、snapping 和对象级 Undo/Redo 命令。
- 初步支持 prefab。
- 编辑器可完成一个小型静态场景搭建。
- viewport RenderTarget 和 swapchain 使用 generation prepare/create/commit/retire；正常 resize 不依赖全局 Device idle。

### 第 9-10 个月

- 渲染升级到多材质、多光源、排序和批处理。
- 大量对象数据改用 per-FrameSlot ring/dynamic UBO/SSBO，并满足设备 buffer offset alignment。
- Main/Update 与独立 RenderThread 通过有界、owned `RenderFramePacket` 队列解耦。
- Main/Update 侧 RenderSystem 负责 extraction/packet submission；RenderThread 使用 per-slot/per-thread CommandArena。
- RenderThread 统一消费 retirement queue，热重载和资产淘汰按 submission/timeline completion 延迟释放。
- Descriptor system 拆分为 frame、persistent 和 editor ImGui 三类 arena/pool，并按各自 completion/generation 回收。
- RenderGraph pass 描述与具体 `RenderPass`/`Framebuffer` 解耦，完成 Dynamic Rendering 技术验证并记录后端选择；
  验证结果不支持迁移时继续保留传统路径。
- RenderGraph 根据 pass resource contract 统一跟踪 subresource 状态并生成 Synchronization 2 barrier。
- PipelineManager 使用结构化 `PipelineKey`，并完成兼容 cache blob 的跨启动加载与原子保存。
- 初步 PBR、Shadow、Post-process。
- Inspector 支持 Light 和实时光照参数。
- 材质参数在 Inspector 中实时编辑。

### 第 11-12 个月

- 输入、Native Script、Fixed Update。
- 基础物理和音频。
- Play/Edit 模式稳定。
- 完成一个可交互 demo 项目。

## 成熟度评估表

| 能力域          | 当前成熟度 | 目标成熟度 | 优先级 |
| --------------- | ---------- | ---------- | ------ |
| CMake/工程结构  | 中         | 高         | 中     |
| Vulkan 基础封装 | 中         | 高         | 中     |
| 内存管理/VMA    | 中低       | 高         | 中     |
| 渲染管线        | 低中       | 高         | 高     |
| 任务系统/多线程 | 无         | 中高       | 中     |
| Scene/ECS       | 低中       | 高         | 最高   |
| 序列化          | 低         | 高         | 最高   |
| Asset Database  | 低         | 高         | 高     |
| Editor UI       | 低中       | 高         | 高     |
| Editor 数据闭环 | 低中       | 高         | 最高   |
| 脚本            | 无         | 中         | 中     |
| 输入            | 无         | 中         | 中     |
| 物理            | 无         | 中         | 中低   |
| 音频            | 无         | 中         | 中低   |
| 动画            | 无         | 中         | 低     |
| 打包发布        | 无         | 中         | 低     |
| 测试/CI         | 中低       | 高         | 中     |

## 下一步建议

下一步补齐 **Rotation / Scale Gizmo modes**：复用现有连续 pointer、固定屏幕尺度和 Controller transaction 契约，增加模式快捷键与可选 snapping；
三种 Transform 都必须在 release 时只产生一个命令，Selection/Scene owner 切换时可安全取消。
runtime format/sample-count-dependent Pipeline generation 归入阶段 5，与 PipelineKey/RenderGraph 生命周期一并设计，避免在旧
PipelineManager 上再增加一套只为 swapchain 服务的临时包装。

建议的职责边界：

- Engine/运行时层持有 Scene，app 和 editor 负责创建或修改场景内容。
- Scene 只拥有实体、可序列化组件和 `AssetHandle`，不依赖路径、Mesh、Texture、Buffer 等运行时/GPU对象。
- SceneExtractor 把 Transform、MeshRenderer、Camera 转换为只读 RenderScene；Camera Transform 的 scale 不影响 view matrix。
- Asset Registry 唯一保存 `AssetHandle` 到运行时资源的映射，ResourceManager 只创建 Device 相关资源并维护设备级共享对象。
- SceneResolver 选择 EntityId 最小的主 Camera、验证参数并将 RenderScene 解析为完整 RenderSubmission；Renderer 编排帧流程，SceneRenderer 管理帧资源并执行绘制。

阶段 2 完成情况：

1. [x] 为实体增加可持久化 UUID，运行时 `EntityId` 继续用于快速查询和编辑器 Selection。
2. [x] 定义 UUID 的生成、解析、格式化、比较、哈希和无效值语义，并拒绝无效或重复 UUID。
3. [x] 定义 `.scene` YAML 中实体、父子 UUID 引用和基础组件的表示。
4. [x] 实现纯 Scene 的内存序列化往返和文件保存/加载。
5. [x] 对重复 UUID、缺失父节点、循环引用和格式错误给出可测试的失败策略。
6. [x] 接入编辑器 New/Open/Save，并在替换 Scene 后清理失效 Selection。
7. [x] 建立组件/属性描述元数据，并让 Inspector 通过描述生成基础组件编辑控件。
8. [x] 让场景序列化复用属性描述元数据，由 descriptor 定义当前开发格式。
9. [x] 实现最小 Play/Edit 切换：进入 Play 时复制 Edit Scene，退出时丢弃 Runtime Scene，并清理失效 Selection。

阶段 3 的第一批最小范围：

1. [x] 定义 `assets/`、`.comet/` 和 `ProjectSettings/` 的目录职责，以及源资产、`.meta`、导入产物和编辑器本地状态的边界。
2. [x] 定义持久化 Asset GUID 与代码层 `AssetHandle` 的映射规则，禁止把源文件路径直接写入 Scene。
3. [x] 实现只负责扫描、索引和查询的 Asset Database Core，并用临时项目目录完成纯逻辑测试。
4. [x] 完成 Texture 同步导入与运行时发布，资源缓存键从路径收敛为 `AssetHandle`。
5. [x] 让 Project 面板读取真实索引、刷新扫描并展示问题。
6. [x] 接入 Material 资产，使其通过 Texture Handle 表达依赖；缩略图后续增加，文件监视入口已在第 43 步完成。
7. [x] 为 Texture 建立类型化 Importer 设置，支持色彩空间与垂直翻转的持久化、校验和同步导入。
8. [x] 统一 Entity/Asset Selection，并在现有 Inspector 中完成 Material 编辑、保存和显式运行时重载。
9. [x] 扫描以候选快照提交变化集，Project Refresh 同步 Registry/Inspector，发现失败保留上一有效快照。
10. [x] Scene、Material 和 `.meta` 使用共享原子文本替换，并为 AssetManager 更新/刷新链路补测试。
11. [x] 删除未接入生产渲染路径的 MaterialConfig/MaterialInstance，拆分 TextureData/MeshData CPU DTO。
12. [x] 以 submodule 接入 fastgltf/simdjson，完成 `.gltf`/`.glb` 静态 Mesh 导入、失败安全刷新，并让 app/editor 从项目资产加载 cube。
13. [x] 将项目本地数据收敛到 `.comet/`：cache 保存版本化 Mesh 二进制产物并支持源文件/外部 buffer 失效检测、完整性校验和原子重建，editor 保存 ImGui 布局状态。
14. [x] 分离资产源文件签名与单调 `AssetRevision`，并在 Mesh 候选发布前验证 revision，阻止旧导入结果覆盖新状态。
15. [x] 建立 Engine 统一持有的最小 TaskScheduler，并让已加载 Mesh 通过后台 CPU 导入、Owner Thread completion 和 revision 验票完成失败安全热刷新。
16. [x] 从 Mesh 缓存恢复 Importer 源依赖，在 Asset Database 建立源路径正向/反向索引，并让项目内、由 glTF 外部引用的 `.bin` 变化推进所属 Mesh revision、触发后台热刷新。
17. [x] 建立类型化 `ResourceUsage`、`ResourceState`/`ImageState` 与 usage-to-state 映射，显式携带 queue owner 和 image subresource range，并拒绝缺少 shader stage、非法 aspect 和空范围。
18. [x] 将现有显式 image transition 迁移到 `ImageMemoryBarrier2`/`DependencyInfo`，让 Texture 上传提供前后 `ImageState`，删除 layout-pair 推断和 legacy `pipelineBarrier()`。
19. [x] 启用 Vulkan timeline semaphore，Queue 为每次 submission 返回单调 `GpuCompletionPoint`，Runtime Resource/retirement 保存需要的 token，FrameSlot 记录 fence 覆盖的 submission serial；阻塞式上传只等待对应完成点而不再等待整个 Queue idle。
20. [x] 建立 ResourceManager 独占的最小 UploadManager，分离目标 allocation 与内容上传，统一 staging/copy/Barrier2/completion 生命周期，并把 Mesh vertex/index 合并为一次提交。
21. [x] 将 staging 演进为默认 4 MiB 的可复用 page：同一 batch 线性子分配，超大上传按需扩页，timeline completion 后整页回池。
22. [x] 让 Runtime Mesh/Texture 保存上传 completion 并在提交上传后立即返回；ResourceManager 每帧回收已完成 batch，取消资源创建路径的 CPU wait。
23. [x] 将 Mesh/Texture ready completion 汇总为 frame submission 前置条件，并按 timeline 去重、合并最大 value，在 VertexInput/FragmentShader stage 等待。
24. [x] 完成上传/ready 子阶段架构复盘：把 stage 与 Queue wait 编译从 SceneResolver 收回 SceneRenderer，并将 timeline 完成查询移到去重之后。
25. [x] 建立通用 GpuRetirementQueue；SceneRenderer 将实际录制的 Mesh/Texture owner 绑定到 frame completion，热重载旧资源不再早于在途 draw 销毁。
26. [x] 将 FrameManager 更名并收敛为 FrameScheduler：显式状态机约束 slot wait、image acquire 后 begin、submission completion 记录和单调 frame serial。
27. [x] 由实际 fence wait 推进 completed frame serial，并用它安全回收不再使用的 Material DescriptorPool/资源缓存；确认当前无需空的通用 per-frame arena。
28. [x] 将 VK_EXT_memory_budget 作为可选设备能力接入 VMA，并用 FrameScheduler 单调 serial 更新 allocator frame index；扩展缺失时保持兼容回退。
29. [x] 增加不暴露 VMA 类型的 MemoryBudgetSnapshot，由 Allocator 查询每个 heap 的 block/allocation/usage/budget，Device 提供只读转发接口。
30. [x] 为 UploadManager 增加有界 staging 空闲池和预算感知增长：超大 page 不缓存，pool miss 时才采样预算，高压力下仅释放无在途引用的空闲页并节流记录。
31. [x] 完成 GPU 资源生命周期子阶段复盘：GpuCompletionPoint 迁入 synchronization，删除 FrameSlot 重复 timeline token、未使用配置/透传接口和冗余析构代码，保持 fence serial 与 timeline completion 职责分离。
32. [x] 在 Allocator 建立强失败 `create_*` 与可恢复 `try_create_*` 双轨接口，并增加默认关闭的 `within_budget` allocation 选项；失败结果不包含半初始化 handle。
33. [x] 将 recoverable contract 提升到 Buffer/Image 静态工厂：先完成 allocation 再构造 owning wrapper，强失败工厂继续委托同一逻辑且默认允许超预算。
34. [x] 建立事务式 UploadManager active batch：upload staging 支持 within-budget 尝试创建，recoverable enqueue 失败自动 abort 未提交命令/引用/page；GpuResourceResult 从 Allocator 细节中独立出来供整条 GPU 创建链复用。
35. [x] 将 Runtime Mesh 收敛为强失败/可恢复静态工厂：全部 vertex/index target 分配成功后才开始 enqueue，flush 产生 completion 后才构造和发布 wrapper。
36. [x] 将 ImageView 收敛为强失败/可恢复静态工厂：原生 view handle 成功后才构造持有父 Image 的 wrapper，并迁移现有生产调用点。
37. [x] 将 Runtime Texture 收敛为强失败/可恢复静态工厂：Image、ImageView、staging enqueue 和 flush 全部成功后才发布 wrapper，并删除重复的纯色 GPU 构造入口。
38. [x] 将 recoverable Mesh/Texture 结果接入 RenderResourceFactory、ResourceManager 和 AssetManager：资产创建遵守预算，失败记录具体 Vulkan error，刷新候选不替换旧 Registry 对象。
39. [x] 完成 recoverable GPU 创建纵向链复盘：删除 ResourceManager 无调用方的强失败转发，禁止默认构造 GpuResourceResult，并确认下一优先级是把 UploadManager 全局 active state 收敛为显式 UploadBatch。
40. [x] 建立显式 UploadBatch：每个 scope 独占未提交 CommandContext、目标引用和 staging pages，submit 后移交 pending ownership，析构/失败只 abort 自身；删除 UploadManager 全局 active API。
41. [x] 增加 staging growth guard 和真实 GPU fault-injection 测试：Batch A 第二次增长返回 out-of-device-memory 并只回滚 A，已开放 Batch B 仍能提交、等待和回收；补齐 Window/Context 动态库导出。
42. [x] 将已加载 Texture 的扫描刷新迁到 TaskScheduler：泛化 pending/scheduled task 状态，Worker 只解码 TextureData，Owner Thread 双重 revision 验票、可恢复 GPU 创建并替换 Registry，失败保留旧 Texture。
43. [x] 建立 Editor 资产源自动监视入口：500ms 时间门控只在文件快照变化时触发既有 scan，失败保留上一基线，已知 Editor 写入按路径确认，Project/Inspector 在 Owner Thread 更新。
44. [x] 完成资产管线阶段性架构复盘：确认 monitor/database/cache/revision/task 状态各自职责，修复索引类型变化后 Registry 仍保留旧 C++ 类型的问题，并确定 metadata 失败安全为下一优先级。
45. [x] 让 metadata 扫描失败安全：已有路径的无效 sidecar 合并上一份 AssetRecord/signature/revision/依赖，新资产只报告问题；重复 GUID 拒绝整次有歧义快照，不再按路径选择身份所有者。
46. [x] 保留无效 Material 文档的上一份有效 Texture Handle 依赖：Material 文件变化仍推进 revision 并触发重载诊断，旧 Runtime Material 与正反向依赖图保持一致。
47. [x] 建立可复用 ImportInputSnapshot，并接入 MeshImporter、MeshImportCache 与 Owner Thread 发布门；输入在导入/缓存/GPU 创建期间变化时丢弃候选、推进 revision 并自动重调度。
48. [x] 让 TextureImporter 与后台/同步发布复用 ImportInputSnapshot：解码和 GPU 创建期间源图片变化时不发布旧像素，主动推进 revision 并自动重调度；首次加载补齐 revision 验票。
49. [x] 建立 AssetManager 资产移动事务：校验项目内目标和 sidecar 身份，成对 rename source/.meta，成功复用 scan，目标冲突、路径越界或快照无法提交时回滚并保留 Handle/Registry。
50. [x] 在 Project 面板接入事件式 Move/Rename：UI 只提交选中 Handle 与相对目标，Editor 同步 Inspector、Selection、Log 和 AssetSourceMonitor；成功后保持选中身份，失败留在对话框并展示诊断。
51. [x] 建立 ViewportRenderRequest 与 editor-only camera：Edit 提交显式相机快照，Play 选择 Runtime Scene 主相机，隐藏视口不请求 resize；SceneRenderer 不感知 EditorMode。
52. [x] 完成阶段 3 → 4 架构复盘：删除 Editor/ProjectPanel 重复 scan report 状态、Renderer 未消费的输入策略和 SceneResolver 旧重载；确认 AssetManager 暂不按行数拆分，补齐资产架构文档。
53. [x] 建立纯 ViewportLayout：分离 panel logical content、HiDPI physical render resolution 与等比 image display rect；ViewPanel 使用 platform viewport framebuffer scale，Renderer 只接收物理分辨率。
54. [x] 增加 Play Viewport 分辨率/显示策略：Free、16:9、1280x720、1920x1080 与 Fit/1x 均由纯布局策略计算；固定模式不随 panel resize 改变目标像素。
55. [x] 将离屏 resize 改为 generation prepare/create/commit/retire：FrameBuffer 与 MultiTarget 提供可恢复工厂，SceneRenderer 按真实 submission completion 保留旧 target，ImGui descriptor 只在 ready frame slot 替换。
56. [x] 为 Viewport 物理分辨率增加设备/编辑器双重上限：DeviceCapability 暴露 maxImageDimension2D，ViewportLayout 对所有 resolution policy 的最终结果统一等比约束到 4096 以内。
57. [x] 完成 swapchain generation 边界审计并先修 parent/dependent 顺序：SceneRenderer 在 core 重建前释放 runtime/ImGui target，成功或延期后重建；Editor shutdown 主动解绑回调。
58. [x] 建立 SwapchainGeneration core 候选事务：handle/images/config/current index 不再分散覆盖 active 字段，vkCreate 失败保留旧 generation；新 handle 成功后的 image 查询失败明确为不可回滚 fatal 状态。
59. [x] 建立 swapchain dependent generation 共享与 compatibility diff：SwapchainTarget 持有 core shared owner，editor 对 extent/format/image-count 精确失效，runtime format 变化在 pipeline generation 完成前明确拒绝。
60. [x] 将 swapchain 正常重建从 Device idle 收窄为全部 graphics frame-slot fence + present-queue idle 回退；submission serial 在 record 时绑定 slot，确保 active-frame 重建等待能推进真实完成状态。
61. [x] 完成阶段 4B 架构审计：RenderTarget generation 删除原地 resize/dirty/recreate API 和重复 OffscreenTarget，extent/frame-count 构造后只读；初始 RenderPass 使用 active swapchain 实际格式，runtime Pipeline generation 明确归入阶段 5。
62. [x] 建立 Viewport 屏幕点到当前纹理像素的纯映射：布局显式输出 image resolution 与裁切后的 visible rect，排除工具栏、留白、最大边和 OneToOne 不可见区域，并覆盖 HiDPI、debounce 旧纹理和裁切测试。
63. [x] 建立 Viewport editor camera 输入闭环：拆分 overlay prepare/render 时序，当前帧 UI 状态先在 SceneResolver 前提交；可见画面内激活 RMB orbit、MMB pan、wheel zoom，纯 controller 覆盖距离、平移和异常输入测试。SceneExtractor 的同帧前移由第 73 项补齐。
64. [x] 让 Viewport 2D/3D 成为真实观察模式：RenderCamera/SceneResolver 支持正交高度，ViewPanel 只发投影切换事件；2D 固定观察轴、使用屏幕 XY pan 与独立正交 zoom，保留 3D camera 状态。
65. [x] 完成 Viewport picking 技术审计并建立可复用 Mesh bounds：新增通用 AxisAlignedBox，MeshData 拒绝空/非有限位置，Runtime Mesh 与 GPU buffer 同候选保存 local bounds；CPU ray-AABB 优先于当前阶段的 GPU ID/readback。
66. [x] 建立事件式 CPU Viewport 拾取闭环：ViewPanel 只提交可见画面内的当前纹理 pixel，Renderer 用当帧 RenderSubmission/Camera 完成 ray-local-AABB 最近命中并回调 Selection；空白点击清除选择，无 GPU readback 或 ImGui 下沉。
67. [x] 增加 Focus Selection：通用 geometry 负责 local AABB 到 world AABB，Editor 只在 Viewport `F` 事件上解析 Selection/Mesh/world transform，现有 camera controller 分别为 Perspective/Orthographic framing；无缓存 world bounds 或 Scene 修改。
68. [x] 完成 editor visualization/command 审计并清理旧 Shader 草稿：拒绝材质 tint、ImGui 假轮廓和阶段 4 临时 ID attachment；确定 CommandHistory → DebugDraw → bounds 高亮 → gizmo transaction 的实现顺序。
69. [x] 建立有界 Editor Command History 与通用属性事务：Inspector 一次控件手势只登记一个已应用命令，UUID + descriptor stable id 避免悬空引用；MenuBar/快捷键接入 Undo/Redo，Scene owner 切换清空历史。
70. [x] 建立通用 DebugDraw line submission/executor：纯 CPU list 支持 line/AABB，独立 depth-tested pipeline 在当前场景 subpass 绘制；mapped vertex buffer 按 FrameSlot 隔离并预算内增长，失败不影响主场景。
71. [x] 将 Editor Selection 接到 DebugDraw：Focus 与持续高亮复用同一 selected Mesh world bounds 解析，Edit Viewport 可见时提交 12 条深度测试线；不修改 Material/Scene 或增加专用 renderer。
72. [x] 建立 Global Translation Gizmo MVP：固定 logical-pixel 尺寸的 RGB 世界轴通过 DebugDraw 绘制，连续 texture-pixel 输入完成命中/拖拽；parent-local 换算和 release 单命令事务可测试，未命中 press 继续进入场景拾取。
73. [x] 对齐 Editor 修改与 RenderScene 快照时序：wait/acquire 后先 overlay prepare，再由 Engine provider 提取当前 Scene；同帧 Transform 进入 Mesh draw，Renderer 继续独占完整 frame 生命周期。

格式所有权后续需求：

1. [ ] 保留 YAML 作为人工维护的运行配置格式；不把外部 glTF JSON 解析能力扩散为通用项目格式依赖。
2. [ ] 在编辑器成为唯一写入入口且 Schema 稳定后，将 `.scene`、`.mat`、`.meta` 和 `ProjectSettings/` 作为一个版本化迁移整体改为 JSON。
3. [ ] 为 JSON 项目文档建立确定性输出、严格字段校验、版本升级和旧 YAML 项目迁移测试；禁止只迁移单一资产类型。
4. [ ] `.comet/cache/` 和 Shipping 资源继续使用面向 Runtime 的二进制产物/索引；JSON 只用于需要工具互操作的 manifest 或协议边界。
