# Comet 引擎长期开发路线图

首次生成：2026-07-05

最近更新：2026-08-04

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

目前 Comet 仍接近一个 **带编辑器外壳的 Vulkan demo 引擎原型**，但 Scene/ECS 已经接入最小运行时渲染链路。当前主要矛盾已经从“场景数据尚未接入渲染”转变为“渲染接口仍偏 demo 化，场景数据尚未接入编辑器工作流”。

最明显的信号是：

- `Engine` 持有 Scene 和最小 Asset Registry，每帧提取 RenderScene；`RenderSceneResolver` 选择主 Camera、生成 view/projection 并解析 Handle，`Renderer` 编排多个 render item。
- `Renderer` 已不再持有固定相机、demo mesh、texture 或模型矩阵，但当前仍使用固定 cube pipeline。
- 每个 draw 的模型矩阵已通过 push constant 提交；descriptor 资源按材质和 frame slot 缓存。
- Scene 已能管理实体和基础组件，但目前只有运行期自增 `EntityId` 和局部 TRS，还没有持久化 UUID、父子关系与世界矩阵。
- `MeshRendererComponent` 已使用统一的 `AssetHandle`，app 会注册 demo mesh/material，并通过该链路绘制两个不同 Transform 的实体。
- Hierarchy 和 Inspector 已绑定真实 Scene，支持基于 `EntityId` 的选择以及实体创建、删除、重命名和 TRS 编辑；
  Project、SceneView 拾取和 gizmo 仍是占位状态。
- 编辑器中的场景已按 frame slot 渲染到可采样离屏目标，再由 ImGui 显示在 SceneView/GameView 中；runtime app
  仍直接渲染到 swapchain。两个 View 当前共享场景主 Camera 输出，独立 editor camera 尚未实现。
- Material/ResourceManager 已有接口，但还没有资产数据库、序列化、导入器、热重载和编辑器检查器闭环。
- Scene Update 和 Render Submit 的最小边界已经落地，运行时 System 调度仍未建立。

## 距离成熟编辑器型引擎的核心缺口

### 1. 场景与实体组件模型

Scene/ECS MVP 内核已经完成：

- `Scene`、`Entity`、`IdComponent`、`NameComponent`、`TransformComponent`、`MeshRendererComponent` 和 `CameraComponent`。
- 基于 EnTT 的实体创建、删除、查询、遍历和通用组件访问。
- Scene 独占 registry，Entity 仅通过所属 Scene 访问组件。
- 对应的纯逻辑单元测试。

接下来仍缺少：

- 父子层级关系、局部/世界变换更新。
- 运行时自增 `EntityId` 与持久化实体 UUID 的明确区分。
- 运行时 System 调度。
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

当前编辑器已经完成第一段真实数据闭环：Hierarchy 读取 Scene 的平铺实体列表，Selection 通过 `EntityId`
连接 Hierarchy 与 Inspector，Inspector 可以修改 Name 和局部 Transform，创建、删除和重命名也会作用于真实 Scene；
场景输出也已通过离屏目标进入 View 面板。
接下来仍缺少：

- Project 读取真实项目资产。
- SceneView 使用 editor camera、选择、高亮、gizmo、拾取。
- SceneView/GameView 使用独立输出，并在 resize 后保持交互坐标与画面一致。
- 菜单命令真正连接 New/Open/Save Scene。
- Inspector 的组件注册/反射，以及 MeshRenderer、Camera 等组件编辑。
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

当前状态：阶段 1A、1B、1C 和 1D 已完成；Scene、Camera、AssetHandle、多对象渲染、编辑器基础数据闭环和
离屏视口基础已经建立。下一步进入阶段 1E。

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
- [x] 新增 Scene Render Extractor，提取具有 Transform 与 MeshRenderer 的实体；Scene 组件不持有文件路径或 GPU 资源。
- [x] 让运行时层持有 Scene，由 app 创建 demo Scene 和 cube entity。
- [x] 让 `Renderer` 消费场景提交结果，逐项交给 `SceneRenderer` 绘制；消费端诊断并跳过无效或未解析的 Handle。
- [x] 使用 `RenderSceneResolver` 和 `RenderSubmission` 封装资源解析；SceneRenderer 批量消费已解析对象并内部管理 per-frame UBO 与 descriptor。
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

#### 阶段 1E：Transform 层级

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
- New/Open Scene 后重建编辑器上下文并清理失效 Selection。
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
- 为超大 View 增加最大尺寸或 render scale 约束，避免无上限重建离屏资源。

验收标准：

- SceneView 在不同 DPI 和窗口缩放下保持清晰，RenderTarget 像素尺寸与实际显示需求一致。
- GameView 切换固定分辨率时 Camera aspect、输出纹理和留白区域正确，场景对象不会被非等比拉伸。
- 连续拖拽面板不会每帧重建资源，也不会依赖全局 Device idle。

#### 阶段 4C：视口交互闭环

- 基于 image display rect 将鼠标坐标映射到 RenderTarget 像素坐标，排除工具栏和 letterbox 区域。
- SceneView 实现 editor camera 的平移、环绕、缩放，以及真正生效的 2D/3D 模式。
- 实现对象拾取、Selection 同步、移动/旋转/缩放 gizmo 和选中对象高亮。
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

1. 完成 Transform 父子层级、世界矩阵和脏标记更新。
2. 让 Hierarchy 显示真实父子树并支持最小 reparent。
3. 引入持久化 Entity UUID，并明确它与运行时 `EntityId` 的边界。
4. 定义 `.scene` YAML 格式并实现保存、加载。
5. 接通 New/Open/Save Scene 和 Selection 生命周期。

暂时不要急着做：

1. 完整 PBR/Deferred/RenderGraph。
2. 复杂脚本语言。
3. 完整物理和动画系统。
4. 多平台打包。
5. 大规模材质图/节点编辑器。

原因很简单：Scene 已经连接渲染和编辑器，但层级与持久化仍未闭环。先完成这条纵向链路，后面的 Asset、Play 模式和复杂渲染才有稳定落点。

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
| Editor 数据闭环 | 低中 | 高 | 最高 |
| 脚本 | 无 | 中 | 中 |
| 输入 | 无 | 中 | 中 |
| 物理 | 无 | 中 | 中低 |
| 音频 | 无 | 中 | 中低 |
| 动画 | 无 | 中 | 低 |
| 打包发布 | 无 | 中 | 低 |
| 测试/CI | 中低 | 高 | 中 |

## 下一步建议

下一步进入 **阶段 1E：Transform 层级**，在现有局部 TRS 基础上建立父子关系、世界矩阵和脏标记更新，
并让 Hierarchy 从平铺列表升级为真实树结构。

建议的职责边界：

- Engine/运行时层持有 Scene，app 和 editor 负责创建或修改场景内容。
- Scene 只拥有实体、可序列化组件和 `AssetHandle`，不依赖路径、Mesh、Texture、Buffer 等运行时/GPU对象。
- Scene Render Extractor 把 Transform、MeshRenderer、Camera 转换为只读 RenderScene；Camera Transform 的 scale 不影响 view matrix。
- Asset Registry 保存 `AssetHandle` 到运行时资源的映射，ResourceManager 创建或复用运行时/GPU资源。
- RenderSceneResolver 选择 EntityId 最小的主 Camera、验证参数并将 RenderScene 解析为完整 RenderSubmission；Renderer 编排帧流程，SceneRenderer 管理帧资源并执行绘制。

阶段 1E 的最小范围：

1. 定义父子关系组件及 Scene 层级 API，不向调用方暴露 EnTT registry。
2. 阻止实体成为自己的父节点或形成祖先循环，并明确 reparent 时局部/世界 Transform 的保持策略。
3. 计算局部矩阵和世界矩阵，以脏标记向后代传播更新。
4. 让 Scene Render Extractor 提交世界矩阵，让 Hierarchy 显示真实父子树并支持最小 reparent。
5. 补充父子变换、循环保护、reparent 和父节点销毁策略的纯逻辑测试。
