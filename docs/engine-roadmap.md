# Comet 引擎长期开发路线图

首次生成：2026-07-05

最近更新：2026-07-23

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

目前 Comet 仍接近一个 **带编辑器外壳的 Vulkan demo 引擎原型**，但已经建立了 Scene/ECS 数据模型内核。当前主要矛盾已经从“没有场景数据”转变为“场景数据尚未接入渲染和编辑器工作流”。

最明显的信号是：

- `Renderer` 中仍然直接持有 demo 用的 uniform buffer、texture、mesh 和模型矩阵。
- `SceneRenderer` 目前主要服务固定 cube pipeline，而不是遍历真实 Scene 数据。
- Scene 已能管理实体和基础组件，但目前只有运行期自增 `EntityId` 和局部 TRS，还没有持久化 UUID、父子关系与世界矩阵。
- `MeshRendererComponent` 仍以字符串保存 mesh/material 引用，还没有统一的 `AssetHandle`。
- 编辑器面板显示的是示例对象和占位资产，Hierarchy/Inspector/Project 尚未绑定实际项目数据。
- Material/ResourceManager 已有接口，但还没有资产数据库、序列化、导入器、热重载和编辑器检查器闭环。
- Scene Update、Render Submit 和运行时 System 调度的边界仍未落地。

## 距离成熟编辑器型引擎的核心缺口

### 1. 场景与实体组件模型

Scene/ECS MVP 内核已经完成：

- `Scene`、`Entity`、`IdComponent`、`NameComponent`、`TransformComponent`、`MeshRendererComponent` 和 `CameraComponent`。
- 基于 EnTT 的实体创建、删除、查询、遍历和通用组件访问。
- Scene 独占 registry，Entity 仅通过所属 Scene 访问组件。
- 对应的纯逻辑单元测试。

接下来仍缺少：

- Scene 到 Render Scene / Render Item 的提取边界。
- 父子层级关系、局部/世界变换更新。
- 运行时自增 `EntityId` 与持久化实体 UUID 的明确区分。
- Active Scene 的所有权、更新生命周期和运行时 System 调度。
- Scene 与 Editor Selection 的稳定连接。

Scene 数据模型已经有了地基，但只有完成渲染和编辑器闭环，Comet 才真正跨过从 demo 渲染器到游戏引擎的第一道门。

### 2. 序列化与项目格式

成熟编辑器引擎必须让用户的工作可持久化。当前缺少：

- `.comet` 项目文件或项目目录约定。
- `.scene` 场景文件格式。
- `.mat` 材质文件格式。
- 资源 GUID、元数据文件和引用关系。
- YAML/JSON/Binary 的版本迁移策略。
- 场景保存、打开、新建、另存为、自动保存。

建议前期继续使用 YAML，因为项目已经引入 `yaml-cpp`，调试成本低。

### 3. 资产系统与导入管线

当前 `ResourceManager` 能按路径加载纹理和创建 mesh，但成熟引擎需要 Asset Database，而不只是 runtime cache。缺少：

- 可序列化的统一 `AssetHandle` 及无效值约定。
- 资产扫描、导入、缓存和重新导入。
- GUID 到实际文件路径的映射。
- Texture、Mesh、Material、Shader、Scene 等资产类型。
- 模型导入器，例如 glTF。
- 贴图导入参数，例如 sRGB、wrap、filter、mipmap、压缩。
- 资产依赖追踪和热重载。
- 编辑器 Project 面板与真实文件系统/资产数据库绑定。

### 4. 编辑器数据闭环

当前编辑器已经有正确的面板方向，但还是静态 UI 原型。缺少：

- Hierarchy 读取真实 Scene 实体树。
- Inspector 通过组件反射/注册表编辑真实组件。
- Project 读取真实项目资产。
- SceneView 使用 editor camera、选择、高亮、gizmo、拾取。
- GameView 显示运行时 camera 输出。
- 菜单命令真正连接 New/Open/Save Scene。
- Undo/Redo、复制粘贴、删除、重命名、拖拽资源到实体。

编辑器成熟度的关键不是面板数量，而是每个操作都能修改真实项目数据并可保存。

### 5. 渲染架构可扩展性

当前渲染管线仍偏固定 demo。成熟引擎需要从“画一个 cube”演进到“提交一批可渲染对象”。缺少：

- Render Scene / Render Queue / Draw Item 抽象。
- MeshRendererComponent 到渲染提交的路径。
- CameraComponent 驱动 view/projection。
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

## 分阶段开发路线

### 阶段 0：基线收束

目标：把当前 demo 状态整理成可继续演进的稳定基线。

当前状态：已完成。`Renderer` 中 demo 资源的所有权已经被识别为应用层职责，实际迁移将在阶段 1B
随 Active Scene 和 Render Submission 一次完成，避免在相邻阶段重复改造。

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

当前状态：阶段 1A 已完成，阶段 1B 是下一项工作。

#### 阶段 1A：Scene/ECS Core（已完成）

- 新增 `engine/src/scene/` 模块。
- 基于 EnTT 实现 `Scene`、`Entity` 包装类型。
- 实现 `IdComponent`、`NameComponent`、`TransformComponent`、`MeshRendererComponent` 和 `CameraComponent`。
- 实现实体验证、创建、删除、按 ID 查询、遍历和通用组件操作。
- 使用单元测试约束 Entity 不公开 registry、原始 EnTT handle 和所属 Scene。

#### 阶段 1B：Scene Render Submission（下一步）

- 定义最小 `AssetHandle`，包含无效值、比较和哈希能力。
- 将 `MeshRendererComponent` 的 mesh/material 字符串替换为 `AssetHandle`。
- 提供最小内存 Asset Registry，支持 app 将 demo `AssetHandle` 注册到运行时资源；暂不实现目录扫描和导入。
- 定义最小 `RenderScene` / `RenderItem`，至少包含实体 ID、模型矩阵、mesh handle 和 material handle。
- 实现局部 TRS 到模型矩阵的转换，明确 Euler 旋转顺序和角度单位。
- 新增 Scene Render Extractor，提取具有 Transform 与 MeshRenderer 的实体；Scene 组件不持有文件路径或 GPU 资源。
- 让运行时层持有 Active Scene，由 app 创建 demo Scene 和 cube entity。
- 让 `Renderer` 只消费场景提交结果，并让 `SceneRenderer` 遍历 render items。
- 接入主 `CameraComponent` 驱动 view/projection；Camera FOV 统一使用角度。
- 每个 draw 的模型矩阵优先使用 push constant，避免单一 model uniform buffer 阻碍多对象渲染。
- 删除 `Renderer` 内部硬编码的 cube mesh、旋转逻辑和固定相机业务状态。

#### 阶段 1C：编辑器基础数据闭环

- Hierarchy 从真实 Scene 读取实体，先支持平铺列表，不提前实现伪层级。
- 建立 Selection 服务，以实体 ID 连接 Hierarchy、Inspector 和 SceneView。
- Inspector 直接读写 Name 与 Transform，并让修改立即反映到渲染结果。
- 支持创建、删除和重命名实体的最小编辑流程。

#### 阶段 1D：Transform 层级

- 新增父子关系组件和循环依赖保护。
- 实现局部矩阵、世界矩阵及脏标记更新。
- Hierarchy 显示真实父子树，支持最小 reparent 操作。
- 补充父子变换、销毁父节点和非法层级的单元测试。

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

建议任务：

- 引入持久化 Entity UUID，并与运行时 `EntityId` 区分。
- 定义 `AssetHandle` 的 YAML 表示和持久化稳定性；路径仅可作为导入/迁移提示，不能成为 Scene 的主引用。
- 定义 `.scene` YAML 格式。
- 实现场景保存和加载。
- 菜单 New/Open/Save Scene 接入真实逻辑。
- Hierarchy 支持创建、删除、重命名实体。
- Inspector 支持编辑 Transform、MeshRenderer 和 Camera。
- 实现 Play/Edit 模式的最小切换。

验收标准：

- 在编辑器中新建场景，创建 cube/camera，保存后重启可以恢复。
- Inspector 修改 Transform 后，SceneView 立即反映。
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

### 阶段 4：编辑器视口与交互能力

目标：让编辑器真正可用，而不是只显示数据。

建议任务：

- SceneView 使用独立 editor camera。
- GameView 使用场景中的主 Camera。
- 实现对象拾取。
- 实现移动、旋转、缩放 gizmo。
- 选中对象高亮和轮廓显示。
- 实现 Undo/Redo 命令系统。
- 支持复制、粘贴、删除、duplicate。
- 支持 prefab 的最小版本，至少能保存一组实体为可复用资产。

验收标准：

- 用户可以通过鼠标在 SceneView 选择对象。
- 用户可以用 gizmo 移动对象并保存场景。
- Transform 修改可撤销和重做。
- SceneView/GameView 的渲染目标尺寸随面板变化稳定更新。

### 阶段 5：渲染系统升级

目标：从基础 forward demo 进入可扩展渲染器。

建议任务：

- 抽象 RenderGraph 或轻量 RenderPass Pipeline。
- 扩展阶段 1 的基础 Render Queue，支持多 mesh、多 material、多 camera、排序和批处理。
- 新增 `LightComponent`，并接入真实 Forward Lighting 提交流程。
- 基础光照模型：方向光、点光、聚光灯。
- Shadow Map。
- PBR 材质基础：base color、normal、metallic、roughness。
- Shader reflection 或半自动 descriptor layout 生成。
- Material Inspector 与 shader 参数绑定。
- 后处理：tone mapping、gamma、简单 bloom。
- GPU/CPU 性能统计面板。

验收标准：

- 一个场景中可稳定渲染多个对象、多材质、多光源。
- 材质参数在 Inspector 修改后实时生效。
- 渲染路径不依赖硬编码 cube pipeline。
- 有基础性能统计，能定位 CPU/GPU frame time。

### 阶段 6：运行时游戏能力

目标：支持制作一个小型可交互 3D demo。

建议任务：

- 输入系统：键盘、鼠标、手柄抽象。
- Native Script 组件。
- Script 生命周期和 Inspector 字段暴露。
- Fixed Update。
- 物理引擎接入，建议先接入 Jolt 或 Bullet。
- Audio 基础，建议先接入 miniaudio。
- Runtime UI 的最小方案。
- Play 模式 Scene clone，退出 Play 不污染编辑数据。

验收标准：

- 能做一个可移动角色、可碰撞场景、带声音反馈的小 demo。
- Play/Edit 切换稳定。
- 脚本能读取输入、修改 Transform、触发音效。
- 物理调试绘制可在编辑器中打开。

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

1. 定义最小 `AssetHandle` 和内存 Asset Registry，替换 MeshRenderer 的字符串引用。
2. 完成 Scene 到 RenderItem 的提取和渲染提交。
3. 让主 CameraComponent 驱动 view/projection。
4. 从 `Renderer` 移除硬编码 demo mesh、模型矩阵和相机状态。
5. Hierarchy、Selection、Inspector 绑定真实 Scene。
6. 完成 Transform 父子层级和世界矩阵。
7. 引入持久化 Entity UUID，再开始 `.scene` 序列化。

暂时不要急着做：

1. 完整 PBR/Deferred/RenderGraph。
2. 复杂脚本语言。
3. 完整物理和动画系统。
4. 多平台打包。
5. 大规模材质图/节点编辑器。

原因很简单：Scene 内核已经存在，但还没有连接渲染、编辑器和持久化。先完成这条纵向链路，后面的 Asset、Play 模式和复杂渲染才有稳定落点。

## 12 个月建议里程碑

以下时间按一名全职开发者或小团队持续投入估算。兼职开发应保留里程碑顺序，不强行套用月份。

### 第 1-2 个月

- 完成最小 `AssetHandle` 和内存 Asset Registry。
- 完成 Scene Render Submission。
- Renderer 改为消费 RenderScene/RenderItem。
- 主 Camera 驱动渲染。
- 编辑器显示真实实体和 Transform。
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

### 第 7-8 个月

- 完成对象拾取、gizmo、Undo/Redo。
- 初步支持 prefab。
- 编辑器可完成一个小型静态场景搭建。

### 第 9-10 个月

- 渲染升级到多材质、多光源、排序和批处理。
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
| Scene/ECS | 低中 | 高 | 最高 |
| 序列化 | 低 | 高 | 最高 |
| Asset Database | 低 | 高 | 高 |
| Editor UI | 低中 | 高 | 高 |
| Editor 数据闭环 | 低 | 高 | 最高 |
| 脚本 | 无 | 中 | 中 |
| 输入 | 无 | 中 | 中 |
| 物理 | 无 | 中 | 中低 |
| 音频 | 无 | 中 | 中低 |
| 动画 | 无 | 中 | 低 |
| 打包发布 | 无 | 中 | 低 |
| 测试/CI | 中低 | 高 | 中 |

## 下一步建议

下一步应完成 **阶段 1B：Scene Render Submission**，并在同一项工作中接入真实调用方，避免留下暂时无人使用的抽象。

建议的职责边界：

- Engine/运行时层持有 Active Scene，app 和 editor 负责创建或修改场景内容。
- Scene 只拥有实体、可序列化组件和 `AssetHandle`，不依赖路径、Mesh、Texture、Buffer 等运行时/GPU对象。
- Scene Render Extractor 把 Transform、MeshRenderer、Camera 转换为只读的 RenderScene。
- Asset Registry 解析 `AssetHandle`，ResourceManager 创建或复用运行时/GPU资源。
- Renderer 消费解析后的 RenderScene，SceneRenderer 只执行具体绘制。

本项工作的最小范围：

1. 定义 `AssetHandle`、无效值、比较、哈希和最小内存注册表。
2. 将 MeshRenderer 的 mesh/material 字段改为 Handle，并迁移现有测试。
3. 定义 `RenderItem` 和局部 TRS 模型矩阵计算。
4. 提取所有可渲染实体，并为无效或无法解析的 mesh/material handle 提供可诊断的跳过策略。
5. 让 SceneRenderer 遍历 items；模型矩阵使用每 draw push constant。
6. app 注册 demo 资源并创建 Scene、Camera 和 cube entity，Renderer 不再创建 demo cube。
7. 单元测试覆盖 Handle、资源解析、提取过滤、字段映射、TRS、FOV 单位和无效资源。

验收时至少使用两个不同 Transform 的实体，确保实现不是“把单 cube 接口换了个名字”。完成后再进入阶段 1C，连接 Hierarchy、Selection 和 Inspector。
