# Comet 引擎长期开发路线图

生成日期：2026-07-05

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
- 测试基础已经存在，覆盖数学、配置、导出、日志、GLFW 初始化、Vulkan RAII 拥有关系等基础行为。
- EnTT 已经作为依赖接入，具备进一步建设 ECS/Scene 的基础。

### 当前主要形态

目前 Comet 更接近一个 **带编辑器外壳的 Vulkan demo 引擎原型**。底层图形资源封装在快速推进，但引擎核心数据模型还没有真正建立。

最明显的信号是：

- `Renderer` 中仍然直接持有 demo 用的 uniform buffer、texture、mesh 和模型矩阵。
- `SceneRenderer` 目前主要服务固定 cube pipeline，而不是遍历真实 Scene 数据。
- 编辑器面板显示的是示例对象和占位资产，Hierarchy/Inspector/Project 尚未绑定实际项目数据。
- Material/ResourceManager 已有接口，但还没有资产数据库、序列化、导入器、热重载和编辑器检查器闭环。
- EnTT 已链接，但实体、组件、场景、系统调度仍未落地。

## 距离成熟编辑器型引擎的核心缺口

### 1. 场景与实体组件模型

成熟编辑器引擎首先需要一个可保存、可加载、可编辑、可运行的 Scene 数据模型。当前缺少：

- `Scene`、`Entity`、`TransformComponent`、`NameComponent`、`MeshRendererComponent`、`CameraComponent`、`LightComponent` 等基础类型。
- 基于 EnTT 的组件注册、创建、删除、查询和遍历。
- 父子层级关系、局部/世界变换更新、实体生命周期管理。
- Scene Update 和 Render Submit 的边界。
- Scene 和 Editor Selection 的稳定对象 ID。

这是 Comet 从 demo 渲染器变成游戏引擎的第一道门。

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
5. 所有核心对象都要有稳定 ID，避免后期资产引用和 Undo/Redo 重做。
6. 渲染器只消费场景提交结果，不继续持有应用层 demo 状态。
7. 优先建设可测试的纯逻辑模块，把 Vulkan 相关测试控制在少量集成测试中。

## 分阶段开发路线

### 阶段 0：基线收束

目标：把当前 demo 状态整理成可继续演进的稳定基线。

建议任务：

- 清理 `Renderer` 中 demo 资源和引擎职责混杂的问题，明确哪些资源属于示例场景，哪些属于渲染系统。
- 明确 `RenderContext`、`SceneRenderer`、`ResourceManager`、`MaterialManager` 的所有权边界。
- 为 VMA、Buffer、Image 的拥有关系补充更直接的测试和错误路径保护。
- 梳理 CMake 目标，减少 `file(GLOB)` 带来的新增文件不可见风险，或至少建立约定。
- 建立 `docs/architecture/`，记录当前模块边界。

验收标准：

- 编辑器和 app 都能正常构建运行。
- 当前 cube demo 行为不回退。
- README 与实际依赖、构建命令保持一致。
- 关键资源释放顺序清晰，无明显 Vulkan validation error。

### 阶段 1：Scene/ECS MVP

目标：建立真实场景数据模型，让引擎能从 Scene 渲染对象，而不是从 `Renderer` 内部硬编码对象。

建议任务：

- 新增 `engine/src/scene/` 模块。
- 基于 EnTT 实现 `Scene`、`Entity` 包装类型。
- 实现基础组件：
  - `IDComponent`
  - `NameComponent`
  - `TransformComponent`
  - `MeshRendererComponent`
  - `CameraComponent`
  - `LightComponent`
- 实现 Transform 层级和世界矩阵更新。
- 修改渲染路径，让 `SceneRenderer` 接收由 Scene 提交的 render items。
- app 示例从代码创建一个 Scene，并渲染 cube。

验收标准：

- demo cube 不再由 `Renderer` 内部硬编码创建。
- Hierarchy 可以从真实 Scene 读取实体名称。
- Inspector 至少可以编辑选中实体的 Transform。
- 单元测试覆盖实体创建、删除、组件添加、Transform 计算。

### 阶段 2：场景序列化与编辑器闭环

目标：用户能在编辑器里创建、编辑、保存、加载一个简单场景。

建议任务：

- 定义 `.scene` YAML 格式。
- 实现场景保存和加载。
- 菜单 New/Open/Save Scene 接入真实逻辑。
- Hierarchy 支持创建、删除、重命名实体。
- Inspector 支持编辑 Transform、MeshRenderer、Camera、Light。
- 引入稳定 UUID/GUID，保存实体和资产引用。
- 建立编辑器 Selection 服务，避免各面板直接传字符串。
- 实现 Play/Edit 模式的最小切换。

验收标准：

- 在编辑器中新建场景，创建 cube/camera/light，保存后重启可以恢复。
- Inspector 修改 Transform 后，SceneView 立即反映。
- Scene 文件可读、可 diff、可手动排查。
- 场景序列化有单元测试。

### 阶段 3：资产数据库与项目系统

目标：从“按路径加载文件”升级为“项目资产管理”。

建议任务：

- 定义项目目录结构，例如 `Assets/`、`Library/`、`ProjectSettings/`。
- 实现 Asset Database：
  - GUID 分配
  - 元数据文件
  - 路径索引
  - 类型识别
  - 依赖查询
- Project 面板读取真实资产目录。
- Texture Importer 支持基础导入参数。
- Material 资产可保存和加载。
- Mesh Importer 接入 glTF，至少支持静态网格。
- ResourceManager 改为通过 AssetHandle/GUID 获取资源。
- 支持资产改名、移动后的引用稳定性。

验收标准：

- Project 面板显示真实 `Assets/` 目录。
- 拖拽 mesh/material 到实体后可以保存并重新加载。
- 删除或移动资产时有基本错误提示。
- 资产数据库重建后，场景引用仍然稳定。

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
- 建立 Render Queue，支持多 mesh、多 material、多 camera。
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

1. Scene/ECS MVP。
2. Transform、MeshRenderer、Camera、Light 基础组件。
3. Renderer 从硬编码 demo 状态迁移到 Scene 提交。
4. Hierarchy/Inspector 绑定真实 Scene。
5. `.scene` 序列化。

暂时不要急着做：

1. 完整 PBR/Deferred/RenderGraph。
2. 复杂脚本语言。
3. 完整物理和动画系统。
4. 多平台打包。
5. 大规模材质图/节点编辑器。

原因很简单：没有 Scene、Asset、Editor 数据闭环，后面的系统都会缺少落点。

## 12 个月建议里程碑

### 第 1-2 个月

- 完成 Scene/ECS MVP。
- 完成基础组件。
- Renderer 改为渲染 Scene。
- 编辑器显示真实实体树和 Transform。

### 第 3-4 个月

- 完成 `.scene` 保存/加载。
- 完成 New/Open/Save Scene。
- Inspector 支持 MeshRenderer、Camera、Light。
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

- 渲染升级到多对象、多材质、多光源。
- 初步 PBR、Shadow、Post-process。
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
| Scene/ECS | 低 | 高 | 最高 |
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

下一步建议开一个专门分支做 **Scene/ECS MVP**。这个任务是后续所有编辑器和运行时能力的地基，范围可以控制在：

- 新增 `engine/src/scene/`。
- 接入 EnTT registry。
- 实现 Scene/Entity/基础组件。
- 让 app 或 editor 创建一个真实 Scene。
- 让 Renderer 渲染 Scene 中的 MeshRenderer。
- 让 Hierarchy/Inspector 读写真实实体。

这个阶段完成后，Comet 的性质会从“渲染 demo + 编辑器壳”转向“真正有场景数据的编辑器型引擎”。后面的资产系统、序列化、Play 模式和渲染升级都会有稳定落点。
