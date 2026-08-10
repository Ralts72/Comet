# Comet 引擎长期开发路线图

首次生成：2026-07-05

最近更新：2026-08-10

## 目标定位

Comet 的长期目标建议定位为 **Unity/Godot 风格的编辑器型游戏引擎**：引擎不仅能渲染一个 demo，还要能通过编辑器创建场景、管理资产、编辑实体组件，并把同一份项目数据交给运行时执行。

这个定位比“学习型 Vulkan 渲染器”更强调工程闭环，也比“生产级商业引擎”更现实。短中期目标应该是做出一个能稳定完成小型 3D demo 的编辑器工作流，而不是一次性追求 AAA 级渲染、跨平台发布和完整工具生态。

## 当前项目现状

### 已具备的基础

- C++20 + CMake 项目结构已经成型，分为 `engine/`、`editor/`、`app/`、`tests/` 和 `3rdparty/`。
- `engine` 已有基础运行框架：`Application`、`Engine`、`Window`、`Timer` 和统一的 YAML 运行配置加载。
- Vulkan 底层封装已经有一定厚度，包括 `Context`、`Device`、`Swapchain`、`RenderPass`、`FrameBuffer`、`Pipeline`、`CommandBuffer`、`DescriptorSet`、`Buffer`、`Image`、`Sampler` 等。
- VMA 已开始接入，`Device` 独占持有 `VulkanAllocator`，`Buffer` 和 `Image` 通过 allocator 管理显存资源。
- 渲染层已有 `Renderer`、`RenderContext`、`SceneRenderer`、`FrameManager`、`RenderTarget`、`Mesh`、`Texture`、`Material`、`ResourceManager` 等雏形。
- Shader 构建链路已经接入 CMake，通过 `glslangValidator` 将 GLSL 编译并生成头文件。
- 编辑器已有 ImGui Docking 基础和几个典型面板：Hierarchy、Inspector、Project、SceneView、GameView、Log。
- 测试基础已经存在，覆盖数学、配置、导出、日志、GLFW 初始化、Vulkan RAII 拥有关系和 Scene/ECS 基础行为。
- EnTT 已经作为依赖接入，`Scene`、`Entity` 以及 ID、名称、局部 Transform、MeshRenderer、Camera 等基础组件已经落地。
- Scene/ECS MVP 内核已覆盖实体创建、删除、查询、遍历和通用组件增删查。

### 当前主要形态

目前 Comet 仍接近一个 **带编辑器外壳的 Vulkan demo 引擎原型**，但 Scene/ECS 已经接入最小运行时渲染链路、
编辑器基础数据闭环和纯逻辑场景持久化。当前主要矛盾已经转变为：场景文件尚未接入编辑器操作，资产数据库尚未建立，
渲染接口仍偏 demo 化。

最明显的信号是：

- `Engine` 持有 Scene 和最小 Asset Registry，每帧提取 RenderScene；`SceneResolver` 选择主 Camera、生成 view/projection 并解析 Handle，`Renderer` 编排多个 render item。
- `Renderer` 已不再持有固定相机、demo mesh、texture 或模型矩阵，但当前仍使用固定 cube pipeline。
- 每个 draw 的模型矩阵已通过 push constant 提交；descriptor 资源按材质和 frame slot 缓存。
- Scene 已能管理运行期 `EntityId`、持久化 UUID、父子关系、局部 TRS 与派生世界矩阵，并可将基础组件保存为
  带版本字段的 `.scene` YAML。
- `MeshRendererComponent` 已使用统一的 `AssetHandle`，app 会注册 demo mesh/material，并通过该链路绘制两个不同 Transform 的实体。
- Hierarchy 和 Inspector 已绑定真实 Scene，支持基于 `EntityId` 的选择以及实体创建、删除、重命名；Inspector 通过
  显式组件/属性描述符编辑 Transform、MeshRenderer 和 Camera，Project、SceneView 拾取和 gizmo 仍是占位状态。
- 编辑器中的场景已按 frame slot 渲染到可采样离屏目标，再由 ImGui 显示在 SceneView/GameView 中；runtime app
  仍直接渲染到 swapchain。两个 View 当前共享场景主 Camera 输出，独立 editor camera 尚未实现。
- Material/ResourceManager 已有接口，但还没有资产数据库、资产序列化、导入器、热重载和编辑器检查器闭环。
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
- SceneView 拾取、gizmo 与现有 Editor Selection 的稳定连接。

Scene 数据模型已经有了地基，但只有完成渲染和编辑器闭环，Comet 才真正跨过从 demo 渲染器到游戏引擎的第一道门。

### 2. 序列化与项目格式

成熟编辑器引擎必须让用户的工作可持久化。基础 `.scene` YAML 契约、版本字段和纯 Scene 保存/加载已经完成；
当前仍缺少：

- `.comet` 项目文件或项目目录约定。
- `.mat` 材质文件格式。
- 资源 GUID、元数据文件和引用关系。
- 场景版本迁移策略。
- 编辑器中的场景打开、新建、另存为和自动保存。

建议前期继续使用 YAML，因为项目已经引入 `yaml-cpp`，调试成本低。

### 3. 资产系统与导入管线

当前 `ResourceManager` 能按路径加载纹理和创建 mesh，但成熟引擎需要 Asset Database，而不只是 runtime cache。缺少：

- `AssetHandle` 的持久化表示，以及它与资产 GUID/元数据的稳定映射。
- 资产扫描、导入、缓存和重新导入。
- GUID 到实际文件路径的映射。
- Texture、Mesh、Material、Shader、Scene 等资产类型。
- 模型导入器，例如 glTF。
- 贴图导入参数，例如 sRGB、wrap、filter、mipmap、压缩。
- 资产依赖追踪和热重载。
- 编辑器 Project 面板与真实文件系统/资产数据库绑定。

### 4. 编辑器数据闭环

当前编辑器已经完成第一段真实数据闭环：Hierarchy 读取 Scene 层级，Selection 通过 `EntityId` 连接 Hierarchy 与
Inspector，组件描述符驱动 Transform、MeshRenderer 和 Camera 属性编辑，创建、删除、重命名和场景文件操作也会
作用于真实 Scene；场景输出已通过离屏目标进入 View 面板。
接下来仍缺少：

- Project 读取真实项目资产。
- SceneView 使用 editor camera、选择、高亮、gizmo、拾取。
- SceneView/GameView 使用独立输出，并在 resize 后保持交互坐标与画面一致。
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

当前 `Application` 和 `Engine` 有基础生命周期，但成熟引擎需要更清晰的运行时系统。缺少：

- System 调度，例如 TransformSystem、RenderSystem、CameraSystem。
- Play/Edit 模式切换。
- Fixed Update 与普通 Update 分离。
- 输入系统。
- 时间缩放、暂停、单帧步进。
- Runtime Scene 克隆，避免编辑模式数据被运行时直接污染。
- 应用层访问场景和资源的稳定 API。

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

| 概念 | 职责 | 引入阶段 |
| --- | --- | --- |
| `AssetHandle` | 代码层使用的轻量、不透明、可比较和可序列化的资源引用 | 阶段 1B |
| Asset GUID/元数据 | 保证资产跨重启、改名和移动后的持久身份，并记录类型、路径和导入设置 | 阶段 2 定义持久化契约，阶段 3 完整实现 |
| Runtime/GPU Resource | `Mesh`、`Texture`、`Material`、`Buffer` 等已加载对象，只存在于资源缓存和渲染层 | 阶段 1B 起按需解析，永不写入 Scene |

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

| 线程 | 主要职责 | 禁止直接执行 |
| --- | --- | --- |
| Main/Update | GLFW 事件、ImGui 构建、Scene/EnTT 修改、脚本和 editor command | 并发修改 GPU runtime cache |
| Worker pool | 文件 I/O、解码、导入、Shader 编译和纯 CPU 数据处理 | 修改 Scene、访问 ImGui、提交 Vulkan Queue |
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
`Device`、`FrameManager`、`SceneRenderer` 和 `ResourceManager` 按后续需求逐步收敛到这些边界；不为匹配命名增加只做
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
          └── ResourceManager runtime/GPU cache
```

这是一张生命周期和调用方向图，不表示 `GraphicsDevice` 直接拥有下面所有模块。`InstanceContext` 先于 device 创建并
晚于 device 销毁；`FrameScheduler`、`SwapchainGeneration`、`UploadManager` 和 `ResourceManager` 依赖 device，但各自
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

现有 `FrameManager` 已经包含 slot 轮转、frame fence、image-available semaphore 和 command buffer，是
`FrameScheduler` 的核心。演进目标是先统一索引、submission serial 和 reset 时机，再按实际需求迁入 arena：

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

#### ResourceManager 与 RenderSystem

ResourceManager 以 `AssetHandle` 为 key 缓存 Mesh、Texture、Material、Shader 等 Runtime/GPU resource，并管理 pending、
ready、failed、evicted 和 revision 状态；它不扫描项目文件，也不拥有 Scene。Asset Registry/Asset Manager 负责元数据
和导入产物解析，UploadManager 负责传输，GpuRetirementQueue 负责旧 GPU owner 回收。

`RenderSystem` 是 Main/Update system schedule 中的高层协调者：读取 Scene 的稳定时点，调用 extraction/culling，生成
`RenderScene` 或 owned `RenderFramePacket` 并提交 Renderer。它不持有 Vulkan object，不从 RenderThread 读取可变
Scene，也不替代负责 GPU command/submit 的 Renderer。当前 Engine 主循环中的提取与提交代码可在运行时 System
调度建立后逐步收敛到该边界。

#### Resource State Tracker

Resource state tracker 不属于单个 RenderTarget。UploadManager 可使用 batch-local tracker，RenderGraph 使用
frame-compile tracker，持久资源通过明确 handoff state 连接两者；完整状态、subresource 和 Barrier2 规则见后文
“资源状态与 Barrier 编译”。

### GPU Completion 与延迟释放

当前 `FrameSlot` 已持有 in-flight fence，`FrameManager::begin_frame()` 会在复用 slot 前等待该 fence，但 slot 内没有
资源退休队列。viewport resize、swapchain recreation、渲染模式切换和 renderer cleanup 仍通过 `Device::wait_idle()`
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

当前 `SceneRenderer::recreate_swapchain()` 先执行 `Device::wait_idle()`，随后 `Swapchain::recreate()` 原地替换 handle 和
borrowed images，并在返回前销毁 old swapchain；runtime `SwapchainTarget`、FrameManager image state 和 editor ImGui
target 再由外层依次重建。该流程当前可工作，但所有权和失效传播分散，创建中途失败也难以保留一份完整旧状态。

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
`waitIdle()` 作为兼容回退。不能仅把 old swapchain 放入任意 FrameSlot 的 deferred batch。现阶段继续使用
`Device::wait_idle()` 是可接受基线，先建立 generation 所有权和事务，再缩小等待范围。

### Vulkan 同步演进顺序

当前 `Queue::submit()` 使用 legacy `vk::SubmitInfo`，所有 wait semaphore 共用单独构造的 stage-mask 数组；
`CommandBuffer::transition_image_layout()` 使用 `vk::ImageMemoryBarrier` 和 `pipelineBarrier()`，并按 layout pair
硬编码 stage/access。当前只有简单 graphics submit 和单个 swapchain wait，这套实现可以工作，不需要在阶段 1/2
仅为了 API 名称立即重写。

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

当前 `CommandBuffer::transition_image_layout()` 根据 old/new layout 组合硬编码 stage/access，适合 demo 的
`undefined -> transfer dst -> shader read` 上传链路，但 layout 本身不足以描述完整同步关系。随着 render target、
后处理、readback 和多 queue 增加，继续添加 layout-pair `if/else` 会让业务调用点共同承担同步推导，难以验证。

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

| Set/通道 | 资源 | 更新频率 |
| --- | --- | --- |
| set 0: Frame | view/projection、light、shadow | 每个 frame slot |
| set 1: Material | texture、sampler、稳定 material constants | material revision 变化时创建替代版本 |
| set 2: Object/Instance | skinning、instance buffer（未来） | 每对象或批次 |
| push constant | model matrix、object ID | 每次 draw |

`Material` 和 `MaterialLayout` 需要 revision/generation。`MaterialRuntimeCache` 以 material handle、material revision、
layout/pipeline ID 作为稳定缓存维度，只在材质或 layout 变化时重新解析参数。稳定的纹理/sampler descriptor set
不按 frame slot 复制；材质变化时创建新的不可变 descriptor set，旧版本等引用它的 in-flight frame 完成后再释放。
如果某类 material constants 使用 per-frame backing buffer，只为这部分维护 per-slot binding/offset state，不复制稳定
纹理 binding。同一材质被多个 render item 使用时，不应重复进行字符串查询和资源解析。

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
  -> ShaderCompiler 后台编译 SPIR-V
  -> Shader Reflection 生成新 ShaderInterface
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
5. 接入 SPIR-V shader reflection 生成 `ShaderInterface`，Material metadata 继续补充高层语义。
6. Material Inspector 基于 `MaterialLayout` 生成控件，修改参数后通过 revision 精确失效运行时缓存。
7. 增加 editor-only Shader 热加载，复用 reflection 比较接口并重建 pipeline/材质绑定。

### Pipeline 对象缓存与驱动 Cache 持久化

Comet 当前已经创建 `vk::PipelineCache`，并在 `createGraphicsPipeline()` 时传入，因此同一进程内的驱动级 cache
可以生效；但 `Device::create_pipeline_cache()` 总是从空 cache 启动，析构前也没有调用
`getPipelineCacheData()` 保存。另一方面，`PipelineManager` 当前只用字符串 name 作为对象缓存 key，在多 Shader、
热加载和多 render state 阶段不能保证复用正确性。

这两层 cache 必须分开设计：

| 层次 | 职责 | Key/兼容依据 |
| --- | --- | --- |
| Engine `PipelineKey` | 判断当前进程中两个 pipeline description 是否等价，复用 `vk::Pipeline` 对象 | Shader/layout/render state/render-target signature |
| Vulkan cache blob | 帮助驱动跨 pipeline 创建或跨进程复用内部编译结果 | `VkPipelineCacheHeaderVersionOne` 与设备/驱动标识 |

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
  -> 从 Library/Cache/Vulkan 读取候选 blob
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
- cache 文件放在项目 `Library/Cache` 或平台用户 cache 目录，不放进 `engine/assets`，默认不提交版本控制。
- Shader 或 render state 变化通过 `PipelineKey` 产生新的 pipeline create info，不需要因此删除整个驱动 blob；
  驱动 cache 会自行判断是否存在可复用条目。
- `vk::PipelineCache` 的 host access 需要遵守外部同步。引入 RenderThread 后，pipeline 创建、合并和 blob 读取/保存
  都由 RenderThread 串行执行，不从后台 ShaderCompiler 直接访问 cache。
- 不每帧保存。editor 可在一批 pipeline 编译完成后节流保存，runtime 至少在正常 shutdown 保存；异常退出允许
  丢失本次新增 cache 数据。

实现顺序：

1. 先建立结构化 `PipelineKey`，消除 name-only cache 的正确性风险。
2. Shader 热加载和多 pipeline 创建统一通过 `PipelineManager` 与 key/revision 路径。
3. 项目 `Library/Cache` 可用后增加 `PipelineCacheStore`，完成 blob 校验、加载和原子保存。
4. 通过日志和 profiler 比较 cold/warm pipeline creation 时间，但不把特定机器上的加速比例作为功能测试条件。

## 分阶段开发路线

### 阶段 0：基线收束

目标：把当前 demo 状态整理成可继续演进的稳定基线。

当前状态：已完成。`Renderer` 中 demo 资源的所有权已经被识别为应用层职责，实际迁移将在阶段 1B
随 Scene 和 Render Submission 一次完成，避免在相邻阶段重复改造。

已完成：

- 明确 demo mesh、texture、模型矩阵和固定 camera 属于示例场景，`Renderer` 只应保留渲染系统职责。
- 在 `docs/architecture/rendering-ownership.md` 记录 `RenderContext`、`SceneRenderer`、`ResourceManager`、
  `MaterialManager`、VMA 资源和 Device 的所有权与析构顺序。
- 为 Buffer、Image、Device、ResourceManager 和 MaterialInstance 增加无效参数保护与单元测试。
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
- [x] 建立 Selection 服务，只保存并按需解析 `EntityId`，连接 Hierarchy 和 Inspector，并为后续 SceneView 复用。
- [x] Inspector 直接读写 Name 与 Transform，并让修改进入下一帧的场景提取和渲染结果。
- [x] 支持创建、删除和重命名实体的最小编辑流程。
- [x] 实体删除或无效 ID 会自动清空 Selection，并有纯逻辑单元测试保护。

#### 阶段 1D：编辑器离屏视口基础（已完成）

- [x] 将编辑器的场景渲染目标从 swapchain 分离：场景进入离屏颜色/深度附件，swapchain 只承载 ImGui 和最终呈现。
- [x] 为 frame slot 提供独立的离屏资源，避免多个 frames-in-flight 同时读写同一张 viewport image。
- [x] 离屏颜色附件结束时进入 `ShaderReadOnlyOptimal`，并通过 ImGui Vulkan descriptor 注册为 `ImTextureID`。
- [x] `ViewPanel` 提供实际内容尺寸，Renderer 使用稳定尺寸按需重建离屏目标，并处理零尺寸和折叠状态。
- [x] SceneView 和 GameView 复用离屏提交基础；GameView 使用场景主 Camera，独立 editor camera 留到阶段 4。
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

当前状态：持久化 Entity UUID、`.scene` YAML v1、编辑器 New/Open/Save 和 descriptor 驱动的基础组件 Inspector
已完成；下一步让场景序列化复用同一份属性元数据，同时保持显式版本契约。

建议任务：

- [x] 引入 128-bit `EntityUuid` 和 `UuidComponent`，并与运行时 `EntityId` 区分；支持生成、解析、格式化、
  比较、哈希、指定 UUID 创建、重复拒绝和按 UUID 查找。
- [x] 定义 `AssetHandle` 的 YAML 无符号整数表示；路径不进入 Scene，后续由 Asset Database 建立 Handle 到资产的稳定映射。
- [x] 定义带 `version` 字段的 `.scene` YAML v1 格式，并记录实体、父 UUID 和基础组件契约。
- [x] 实现纯 Scene 的内存序列化往返和文件保存/加载；加载失败不会修改已有 Scene。
- [x] 菜单 New/Open/Save Scene 接入真实逻辑；Open 和首次 Save 使用路径输入，已有路径的 Scene 可直接保存。
- [x] New/Open Scene 后重绑定 Hierarchy 与 Selection，并在运行时 `EntityId` 可能复用时仍清理旧选择。
- [x] 建立 `ComponentRegistry`、`ComponentDescriptor`、`PropertyDescriptor` 和按属性类型分发的
  `PropertyEditorRegistry`。
- [x] 先为 Transform、MeshRenderer 和 Camera 显式注册元数据，Inspector 通过 descriptor 生成控件，不再为每个字段
  硬编码面板逻辑。
- [ ] 场景序列化复用同一份属性元数据，但仍由显式 stable ID、`serializable` 标记和版本迁移规则决定持久化契约。
- [ ] 实现 Play/Edit 模式的最小切换。
- [x] 为稳定排序、组件与层级往返、文件读写、重复 UUID、悬空父引用、层级环和格式错误补充单元测试。

验收标准：

- 在编辑器中新建场景，创建 cube/camera，保存后重启可以恢复。
- Inspector 修改 Transform 后，SceneView 立即反映。
- Inspector 可以通过通用 descriptor 编辑 Transform、MeshRenderer 和 Camera，新增已支持类型的字段时不需新增专用面板分支。
- Scene 文件可读、可 diff、可手动排查。
- Scene 保存和加载后，mesh/material 的 `AssetHandle` 保持不变。
- 场景序列化有单元测试。

### 阶段 3：资产数据库与项目系统

目标：从“按路径加载文件”升级为“项目资产管理”。

建议任务：

- 定义项目目录结构，例如 `Assets/`、`Library/`、`ProjectSettings/`。
- 实现 Asset Database：
  - 持久化 GUID 分配
  - 元数据文件
  - `AssetHandle` 到元数据、源文件和导入产物的索引
  - 类型识别
  - 依赖查询
- Project 面板读取真实资产目录。
- Texture Importer 支持基础导入参数。
- Material 资产可保存和加载。
- Mesh Importer 接入 glTF，至少支持静态网格。
- Asset Registry/Asset Manager 负责把 `AssetHandle` 解析为资产元数据和导入产物。
- ResourceManager 以 `AssetHandle` 为缓存键，创建或复用对应的 Runtime/GPU Resource，不直接承担资产扫描。
- 完成 GFX-002 后让现有 `Device` 暴露不可变 `CapabilitySet`，统一保存实际启用的 feature/extension、queue family、
  limits 和 format 能力；明确它承担 GraphicsDevice 职责，但不迁入 FrameSlot、资产或 RenderTarget 所有权。
- 建立最小 `TaskScheduler`/worker pool，将文件读取、纹理解码、mesh 导入和其他纯 CPU 任务移出主线程；worker
  只产出 CPU artifact，不直接修改 Scene、ResourceManager GPU cache 或提交 Vulkan Queue。
- 在实现异步 `UploadManager` 前完成 Synchronization 2 迁移：通过 GFX-002 的 Vulkan 1.3 feature chain 查询并启用
  `synchronization2`，将 `Queue::submit()` 改为 `vk::SubmitInfo2`/`submit2()`，将 image/buffer barrier 改为
  `vk::DependencyInfo` 和 `vk::*MemoryBarrier2`。
- 同步迁移时定义类型化 `ResourceState`/`ImageState` 与 usage-to-state 映射；现有 upload、swapchain 和 RenderPass
  路径必须提供 known-before/desired-after state，不再为新的 layout pair 扩展散落的 `if/else`。
- 先用现有 swapchain frame submit、texture upload 和 buffer copy 路径验证迁移等价性，再引入 timeline semaphore；
  不把 API 迁移、异步上传和 transfer queue 合并成一个无法单独回归的大改动。
- 在阶段 3 后半段建立 `UploadManager`：使用可复用的 staging pages/ring 和批量 copy command，将一次资源上传从
  “每个资源单独 `queue.waitIdle()`”演进为“提交 upload batch 并返回 completion token”。
- 明确 `upload_and_wait()`、`enqueue_upload()` 和 `flush_batch()` 契约。异步资源携带 ready token；首次 graphics
  消费在准确 stage 等待 upload timeline value，同一有序 queue 上的冗余 wait 可由 backend 消除。
- 建立 `GpuCompletionPoint` 与 owner-thread `GpuRetirementQueue`。FrameSlot 在 fence signal 后清理自己的
  `DeferredReleaseBatch`；UploadManager 按 timeline value 回收，不把循环 slot index 当成长期完成标识。
- 将 `FrameManager` 逐步收敛为 FrameScheduler contract：统一 slot index、frame submission serial，以及
  wait completion、collect retirement、reset per-frame arena、开始录制的顺序；本阶段不强制迁移全部 UBO/descriptor。
- `UploadManager` 根据 fence 或 timeline value 延迟回收 staging allocation、upload command buffer 和上传期间临时
  持有的资源；保留阻塞式 `upload_and_wait()`，用于启动期、工具和测试。
- ResourceManager 批量创建 Mesh/Texture 等 GPU Resource 时通过上传接口提交数据，不直接管理 staging lifetime，
  pending resource 必须携带 ready token，不能在缺少 completion 或 GPU-side wait 的情况下被当作 ready resource。
- 将 `VK_EXT_memory_budget` 作为 optional device capability；只在扩展实际启用后为 VMA allocator 设置
  `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`。
- memory budget 启用后，每个渲染帧用单调递增的 frame serial 调用 `vmaSetCurrentFrameIndex()`，不能传循环变化的
  FrameSlot 下标。`vmaGetHeapBudgets()` 按固定低频率及批量加载前采样，不每帧计算 detailed statistics。
- 区分关键 GPU 资源和可降级 streaming 资源。只有 ResourceManager 已支持返回错误、占位资源、重试或淘汰后，
  才为非关键 allocation 使用 `WITHIN_BUDGET`；render target 等关键资源保留独立失败策略。
- 支持资产改名、移动后的引用稳定性。

完整资源路径：

```text
Scene Component / RenderItem
          AssetHandle
              ↓
    Asset Registry / Asset Manager
              ↓
 metadata + source/imported artifact
              ↓
        ResourceManager cache
              ↓
     Mesh / Texture / Material
```

验收标准：

- Project 面板显示真实 `Assets/` 目录。
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

- SceneView 和 GameView 共享同一组按 frame slot 分配的离屏纹理、同一个场景主 Camera 输出和同一个渲染尺寸。
- 两个 View 接收相同的 `ImTextureID`；同时可见时由 SceneView 优先决定离屏目标尺寸，另一个面板只按宽高比显示同一张纹理。
- resize 会等待尺寸连续稳定，再通过 `Device::wait_idle()` 重建离屏 image、image view 和 framebuffer。
- Camera 垂直 FOV 与实体 Transform 不变，RenderTarget 尺寸只改变 projection aspect；ImGui 再把纹理等比放入面板。
- ImGui 逻辑尺寸目前直接作为 RenderTarget 像素尺寸，尚未纳入 HiDPI framebuffer scale。

#### 阶段 4A：SceneView/GameView 独立渲染状态

- 为每个 View 建立独立的 viewport render state，分别保存 Camera 来源、RenderTarget、frame-slot image 和 ImGui descriptor。
- SceneView 使用 editor-only camera；该 Camera 不属于 Scene entity，不参与场景保存，也不影响 runtime 主 Camera。
- GameView 继续使用场景中的 primary Camera，并在没有有效主 Camera 时只清屏和显示诊断。
- 两个 View 可以按各自尺寸独立渲染；隐藏或折叠的 View 跳过提交和 resize。
- 将当前只记录枚举值的 `RenderMode` 演进为明确的 viewport render request，避免在 `SceneRenderer` 内隐式切换全局模式。

验收标准：

- SceneView 和 GameView 同时可见时可以显示不同 Camera 角度，且互不改变对方的投影和尺寸。
- 移动 editor camera 不会修改 Scene，也不会改变 GameView。
- 每个可见 View 在两个 frames-in-flight 下都不会读写仍在使用的离屏附件。

#### 阶段 4B：渲染分辨率与显示策略

- 明确区分 panel content size、render resolution 和 image display rect，不再把三者视为同一尺寸。
- SceneView 默认按面板物理像素尺寸渲染，并结合 ImGui framebuffer scale 处理 Retina/HiDPI。
- GameView 支持固定分辨率和宽高比预设，例如 Free、16:9、1920x1080；面板 resize 默认只改变显示缩放，不改变固定 render resolution。
- 提供 Fit、1x 等显示倍率，保持宽高比并记录 letterbox/pillarbox 后的真实 image display rect。
- 保留 resize debounce，但用 frame fence 和延迟销毁逐步替代 `Device::wait_idle()`，避免拖拽面板时阻塞整个 GPU。
- resize 创建新的 `RenderTargetGeneration`，成功后切换 viewport 引用，并按最后使用它的 frame submission 延迟释放
  旧 image、image view、framebuffer 和 ImGui descriptor；创建失败时继续使用旧 generation。
- 将 swapchain 重建改为 prepare/create/commit/retire generation 流程。engine core、runtime present target 和 editor ImGui
  dependent generation 分层持有；format、sample count 或 image count 变化时精确重建兼容性相关对象。
- old swapchain 只有在 graphics use 和 presentation use 都完成后释放；没有 present completion 能力的平台保留
  present-queue idle 回退，不用不相关的全局 Device idle 代替依赖判断。
- 为超大 View 增加最大尺寸或 render scale 约束，避免无上限重建离屏资源。

验收标准：

- SceneView 在不同 DPI 和窗口缩放下保持清晰，RenderTarget 像素尺寸与实际显示需求一致。
- GameView 切换固定分辨率时 Camera aspect、输出纹理和留白区域正确，场景对象不会被非等比拉伸。
- 连续拖拽面板不会每帧重建资源，也不会依赖全局 Device idle。
- 新 swapchain handle 创建失败不会覆盖或提前销毁 current generation；创建成功后不会尝试从已 retired 的 old
  swapchain acquire。重建后 image count、format-dependent RenderPass/Pipeline、FrameManager image state 和 ImGui
  backend 保持一致，旧 generation 不会在 presentation 完成前释放。

#### 阶段 4C：视口交互闭环

- 基于 image display rect 将鼠标坐标映射到 RenderTarget 像素坐标，排除工具栏和 letterbox 区域。
- SceneView 实现 editor camera 的平移、环绕、缩放，以及真正生效的 2D/3D 模式。
- 实现对象拾取、Selection 同步、移动/旋转/缩放 gizmo 和选中对象高亮。
- 如果对象拾取采用 GPU ID buffer，按请求或 FrameSlot 持有 host-visible readback buffer；copy 完成并确认
  fence/timeline 后再 invalidate/read。若采用 CPU ray cast，则不为了预留能力提前建立通用 readback 系统。
- 将 gizmo 修改接入 Undo/Redo 命令系统，并支持复制、粘贴、删除和 duplicate。
- 支持 prefab 的最小版本，至少能保存一组实体为可复用资产。

验收标准：

- 用户可以通过鼠标在 SceneView 精确选择对象，点击留白区域不会产生错误拾取。
- 用户可以用 gizmo 修改对象并保存场景，Transform 修改可撤销和重做。
- SceneView 的拾取坐标、gizmo 和高亮在面板 resize、DPI 变化和显示倍率切换后仍与画面一致。

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
- 先使用手工 `MaterialLayout` 跑通动态参数链路，再通过 SPIR-V shader reflection 生成低层
  descriptor/push-constant `ShaderInterface`。
- Material Inspector 通过 `MaterialLayout` 和 material metadata 生成参数控件。
- 将 `PipelineManager` 从 name-only map 改为结构化 `PipelineKey`；Shader、layout、render state 和 render-target
  compatibility 任一变化都会生成不同 key，name 只保留为调试标签。
- 增加 `PipelineCacheStore`：从 `Library/Cache/Vulkan` 加载经过设备/驱动校验的 cache blob，通过
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
- Scene/EnTT 修改、ImGui draw data、ResourceManager cache 和 Vulkan Queue 均有唯一 owner，ThreadSanitizer/validation
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
- Runtime player 可加载打包资源。
- 示例项目模板。
- 项目设置面板。
- Crash/log 输出目录规范。
- CI 构建、测试、打包。
- 文档站或系统化开发文档。

验收标准：

- 一个示例项目可以从编辑器打包为独立可执行程序。
- 打包产物不依赖源码目录。
- CI 能至少构建 engine、editor、app、tests。
- 新贡献者能按文档完成环境初始化、构建、运行和测试。

## 推荐的优先级顺序

近期最应该做：

1. [x] 引入持久化 Entity UUID，并明确它与运行时 `EntityId` 的边界。
2. [x] 定义 `.scene` YAML 格式并实现保存、加载。
3. [x] 接通 New/Open/Save Scene 和 Selection 生命周期。
4. 定义场景版本字段和最小迁移策略。
5. [x] 为序列化往返、无效引用和加载失败补充测试。

暂时不要急着做：

1. 完整 PBR/Deferred/RenderGraph。
2. 复杂脚本语言。
3. 完整物理和动画系统。
4. 多平台打包。
5. 大规模材质图/节点编辑器。
6. 在 Scene/资源生命周期尚未稳定前提前拆独立 RenderThread 或并行 command recording。

原因很简单：Scene 已经连接渲染、编辑器和 Transform 层级，但持久化仍未闭环。先完成这条纵向链路，后面的 Asset、Play 模式和复杂渲染才有稳定落点。

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
- SceneView/GameView 区分 editor camera 和 game camera。

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

- 完成对象拾取、gizmo、Undo/Redo。
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

| 能力域 | 当前成熟度 | 目标成熟度 | 优先级 |
| --- | --- | --- | --- |
| CMake/工程结构 | 中 | 高 | 中 |
| Vulkan 基础封装 | 中 | 高 | 中 |
| 内存管理/VMA | 中低 | 高 | 中 |
| 渲染管线 | 低中 | 高 | 高 |
| 任务系统/多线程 | 无 | 中高 | 中 |
| Scene/ECS | 低中 | 高 | 最高 |
| 序列化 | 低 | 高 | 最高 |
| Asset Database | 低 | 高 | 高 |
| Editor UI | 低中 | 高 | 高 |
| Editor 数据闭环 | 低中 | 高 | 最高 |
| 脚本 | 无 | 中 | 中 |
| 输入 | 无 | 中 | 中 |
| 物理 | 无 | 中 | 中低 |
| 音频 | 无 | 中 | 中低 |
| 动画 | 无 | 中 | 低 |
| 打包发布 | 无 | 中 | 低 |
| 测试/CI | 中低 | 高 | 中 |

## 下一步建议

下一步继续 **阶段 2：场景序列化与编辑器闭环**。组件/属性描述元数据和通用 Inspector 已建立，接下来让
`SceneSerializer` 复用 descriptor 的 stable ID、类型访问器与 `serializable` 标记，同时继续显式处理版本迁移。

建议的职责边界：

- Engine/运行时层持有 Scene，app 和 editor 负责创建或修改场景内容。
- Scene 只拥有实体、可序列化组件和 `AssetHandle`，不依赖路径、Mesh、Texture、Buffer 等运行时/GPU对象。
- SceneExtractor 把 Transform、MeshRenderer、Camera 转换为只读 RenderScene；Camera Transform 的 scale 不影响 view matrix。
- Asset Registry 保存 `AssetHandle` 到运行时资源的映射，ResourceManager 创建或复用运行时/GPU资源。
- SceneResolver 选择 EntityId 最小的主 Camera、验证参数并将 RenderScene 解析为完整 RenderSubmission；Renderer 编排帧流程，SceneRenderer 管理帧资源并执行绘制。

阶段 2 的第一批最小范围：

1. [x] 为实体增加可持久化 UUID，运行时 `EntityId` 继续用于快速查询和编辑器 Selection。
2. [x] 定义 UUID 的生成、解析、格式化、比较、哈希和无效值语义，并拒绝无效或重复 UUID。
3. [x] 定义 `.scene` YAML 中实体、父子 UUID 引用和基础组件的表示。
4. [x] 实现纯 Scene 的内存序列化往返和文件保存/加载。
5. [x] 对重复 UUID、缺失父节点、循环引用和格式错误给出可测试的失败策略。
6. [x] 接入编辑器 New/Open/Save，并在替换 Scene 后清理失效 Selection。
7. [x] 建立组件/属性描述元数据，并让 Inspector 通过描述生成基础组件编辑控件。
8. [ ] 让场景序列化复用属性描述元数据，并保留显式版本迁移边界。
