# 引擎架构与运行时边界

描述当前运行时、编辑器、资产与渲染的调用链、所有权和失败边界；待实现能力与验收见[路线图](../engine-roadmap.md)。

## 模块依赖方向

| 模块 | 当前允许的主要依赖 | 边界与例外 |
| --- | --- | --- |
| `common/`、`input/` | 通用值、输入采样及映射 | 不引入 Render、Graphics 或窗口后端头；窗口事件的接线在 `core/window` |
| `scene/`、`scripting/`、`audio/` | 通用值、输入、资产身份／只读缓存；脚本实现可依赖 Lua，物理实现可依赖 Jolt，音频实现可依赖 miniaudio | Scene 组件和序列化不含 GPU／音频设备对象；System 不直接调用渲染后端 |
| `asset/` 的数据、索引、导入与序列化 | 稳定 Handle、CPU 数据、文件与后台任务 | 不依赖 Render／图形后端；`asset/data/texture_data.h` 暂复用不含 Vulkan 头的 `graphics/enums.h` |
| `asset/asset_manager` | 上述 CPU 能力、AssetRegistry，以及 RenderResourceFactory／Runtime Asset | 运行时加载与发布桥接；Render 依赖限定在两个实现文件，源文件编辑事务属于 `editor/assets/` |
| `tools/asset/` | Engine CPU 资产与共用 Shader 编译库 | 编辑器与 CLI 共用源编译；无窗口准备启动场景依赖，engine/app 不链接该工具库 |
| `render/` | Scene 提取结果、资产缓存、Graphics | Renderer 编排帧与离屏输出；SceneRenderer 拥有目标，不知道 ImGui |
| `graphics/` | Vulkan、平台窗口及通用能力 | 图形后端不依赖 Editor；`core/engine.cpp` 是宿主组合点，可使用 Graphics/Render |
| `ui/` | Engine 输入值／图形后端、ImGui | App／Editor 共用的设置界面和呈现后端，不依赖 Editor、Project 或编辑工作流 |
| `editor/`、`app/` | Engine 组合入口、明确的工作流接口、`comet_ui` | 宿主装配 UI；业务视口经 Renderer 离屏帧快照取图，不穿透 SceneRenderer |

`module_boundaries` CTest 检查直接 include：整个 engine 不得引入 Editor/ImGui；
`common/`、`input/`、`scene/`、`scripting/`、`audio/` 不得引入 Render、Graphics、Vulkan/GLFW 后端。
资产层也执行该限制，明确排除 AssetManager 的两个实现文件，并仅允许 TextureData 引用后端无关枚举。
`editor/src/` 功能代码不得直接包含 SceneRenderer、RenderContext、FrameScheduler、Presentation 或 Vulkan/GLFW 头；
ImGuiContext 位于共享 `ui/`，不再为 Editor 功能目录保留后端例外；`editor/editor.cpp` 是扫描范围外的宿主集成点。
这些是防止依赖倒退的轻量检查，不检查传递包含，也不等同于独立编译目标；当前 `engine` 仍是一个库。

## 先看哪个类

| 入口 | 职责 |
| --- | --- |
| `core/engine.h` | 组合宿主服务，统一 SceneRuntime 的绑定、启停与主循环 |
| `scene/scene_runtime.h` | 拥有串行 System，管理时间、固定步、暂停与单步，调用输入模块准备阶段数据 |
| `input/runtime_input.h` | 运行域输入：序号去重、固定步累积、动作求值、暂停基线和重置 |
| `input/input_state.h` | 同一授权／阶段的物理与动作只读快照，System／Lua 的统一消费入口 |
| `scene/systems/script_system.h` | Lua 行为实例的启动、阶段更新、寿命复核与逆序清理；字段仍属于 Scene 组件 |
| `scene/systems/physics_system.h` | 固定步 Jolt 世界，按 Scene 刚体／碰撞体组件同步；只在运行态持有物理对象 |
| `scene/systems/audio_system.h` | 普通更新同步声音源；运行期拥有设备与播放实例，Stop 清理 |
| `scene/material_parameters.h` | 非持久的材质覆盖快照与 CPU 校验契约；不属于 `.mat` 序列化数据 |
| `audio/audio.h` | AudioClip 已解码 CPU 数据与 AudioPlayback 播放实例；不向 Scene 公开 miniaudio 类型 |
| `render/renderer.h` | 渲染子系统组合根，编排帧、RenderView、overlay 与拾取 |
| `render/scene/scene_extractor.h` | Scene → 不含 GPU 对象的 RenderScene 快照 |
| `render/scene/scene_resolver.h` | Handle/Camera → RenderSubmission |
| `render/presentation.h` | acquire／submit／present 与交换链 dependent 有序重建 |
| `render/scene/scene_renderer.h` | 完整目标版本的创建／安装与多 pass 编排 |
| `render/material/material_renderer.h` | Frame/Material descriptor、材质 Pipeline、队列排序与 Mesh 绘制 |
| `render/material/material_runtime.h` | PreparedMaterial 快照与版本缓存；布局定义位于 material_layout |
| `graphics/pipeline/shader_interface.h` | 入口级 SPIR-V 反射结果与绑定覆盖校验；仅拥有 CPU 值 |
| `render/frame_scheduler.h` | FrameSlot 复用、提交及成功登记、image 关联、完成序号与 retention |
| `render/render_diagnostics.h` | 有界场景图 CPU/GPU 采样与低频预算快照，借用 FrameScheduler |
| `render/debug/line_draw_list.h` | 通用 CPU 线段列表；`render/debug/debug_renderer.h` 是当前 GPU 消费者 |
| `render/resource/render_resources.h` | 设备资源工厂、上传及 Sampler 共享资源 |
| `graphics/` | Vulkan 对象与显式同步后端 |
| `editor/src/viewport/viewport.h` | 组合 ViewportPanel/Gizmo，连接编辑器相机、选择反馈与 Renderer |
| `ui/src/imgui_context.h` | App／Editor 共用 UI 呈现、纹理绑定和交换链重建，不属于 engine |

engine 入口路径相对 `engine/src/`。Graphics 的 command/resource/pipeline/synchronization 按职责分目录；
Context、Device、Queue、Swapchain、RenderPass、FrameBuffer 保留在根层，因为它们跨越多个职责组。

底层共用类型按语义归属，不按“是否是枚举”集中：

| 位置 | 边界 |
| --- | --- |
| `graphics/enums.h` | 跨对象的图形参数：格式、用途、分配策略、同步访问等，不包含 Vulkan 头 |
| 类内枚举 | 所属对象的模式或操作结果，如 `Semaphore::Type`、`Queue::PresentStatus`、`Swapchain::RecreateStatus` |
| `graphics/result.h/.cpp` | GraphicsError 与 GpuResourceResult，共用于资源、命令和呈现，不依赖创建模板 |
| `graphics/creation.h` | device-owned 原生句柄接管／失败回收工具，仅后端实现和边界测试包含 |

GraphicsError 保留原生 `vk::Result` 和官方 Vulkan-Hpp 头依赖；隔离的是 Comet 创建工具，不宣称已完全隔离 Vulkan 头。
GpuResourceResult 的失败路径先保存错误码，调用 `error()` 时才生成诊断字符串；不改变现有失败访问契约。
测试中，`test_creation.cpp` 验证结果、部分句柄回收和所有权转移；Allocator 和 Shader 测试分别只关注自己的行为。

## Owner 结构

```text
Engine
├── Scene（组件、AssetHandle 与非持久运行态；不持有 GPU 资源）
├── SceneRuntime → System[]（活动时借用 Scene，停止时逆序退出）
│   └── PhysicsSystem → Jolt world / bodies（Play／app 专有；Stop 销毁）
│   └── AudioSystem → AudioPlayback / Voice（有声音源时创建；Stop 销毁）
├── TaskScheduler
├── AssetRegistry → Runtime Mesh / Texture / Material / Environment / Script / AudioClip / ShaderProgramArtifact
└── Renderer
    ├── RenderContext → Context / Device / Swapchain
    │                    Device → Allocator / queues / PipelineCache
    │                    Swapchain → active Generation
    ├── RenderResources → UploadManager / SamplerManager
    ├── MaterialPrograms → 已通过 GPU 准备的程序 CPU 版本与布局（跨目标重建）
    ├── FrameScheduler → FrameSlot[N] / SwapchainImageState[M]
    ├── RenderDiagnostics → GpuTimer[slot]（实际使用的池由 FrameSlot 保活）
    ├── Presentation（借用 RenderContext、FrameScheduler；有序协调 Scene／Overlay dependent）
    ├── RenderView / SceneResolver
    ├── LineDrawList（单帧 CPU 请求）
    └── SceneRenderer
        └── RenderState（完整兼容版本，FrameSlot 保活）
            ├── 场景 RenderPass / PipelineManager / HDR 与最终 RenderTarget
            ├── BloomPass（可选）→ 高亮提取 / 横纵模糊 / ping-pong Target[slot]
            ├── OutputPass → HDR 合成 / 色调映射 / 输出编码 / 采样绑定
            ├── ShadowPass → 深度 RenderPass / Pipeline / DepthTarget[slot]
            ├── SkyboxPass → Pipeline / Sampler / 不可变 cubemap Binding[slot]
            ├── DebugRenderer → 线段 Pipeline / VertexBuffer[slot]
            └── MaterialRenderer
                ├── PipelineState → MaterialLayout / material descriptor layout / Pipeline
                ├── FrameResources[slot] → 相机与光照 UBO / 阴影 View 与 Sampler / FrameSet / pool
                ├── MaterialRuntimeCache → PreparedMaterial → Texture / 参数字节
                └── MaterialResources[material version] → PreparedMaterial / PipelineState / Sampler / 参数 UBO / pool / MaterialSet

Editor
├── EditorAssets（编辑器项目索引 owner）
│   ├── AssetDatabase（项目索引）
│   ├── AssetSourceOperations（编辑器源文件事务，修改索引候选）
│   ├── AssetManager（借用同一索引，处理加载、失效与发布）
│   └── SceneAssetReferences（活动场景引用、待恢复与未解析集合；借用索引和 Manager）
├── RenderStatsPanel（只读 Engine/Renderer 快照，提交一次性采样／报告请求；报告由 RenderDiagnostics 生成）
├── EditorState / SceneDocument / EditorSceneSession / SelectionService
├── CommandHistory ← Inspector / TransformGizmo 各自的属性事务
├── InspectorPanel → AssetInspector（材质／纹理草稿和请求，只读 MaterialPrograms 的发布布局）
├── Viewport → ViewportPanel / TransformGizmo（借用状态、选择、Renderer、Registry 和 ImGuiContext；尺寸上限由 Renderer 提供）
└── ImGuiContext
    ├── RenderPass / SwapchainTarget / DescriptorPool
    └── TextureBinding[slot] → ImageView / Sampler / ImGui descriptor
```

项目资产与内置 Shader 共用编辑器内的 `FileRecheckTrigger`。它只提示“需要复核”，不判定文件是否真的变化。
macOS 后端使用递归 FSEvents，回调只置位，不读取文件或操作资源；主线程消费提示，已知资产路径仍局部复核，结构性目录快照由 TaskScheduler 捕获后回到主线程比较；Shader 输入按原有链路复核。
监听不可用或根目录失效时退回 500 ms 轮询；macOS 根目录恢复后会尝试重建原生监听，手动 Refresh 仍可直接复核。
目录快照采集的旧结果会在后续通知、显式刷新或编辑器写入后丢弃。结构变化触发的资产数据库扫描先在 Worker 枚举文件、解析 `.meta`／Material／Shader Program，主线程核对输入文件、目录和数据库版本后再生成缺失 `.meta`、分配身份、更新 revision 与发布索引。手动 Refresh 仍同步完成这两段；发布阶段的输入复核及源签名计算仍有主线程文件状态查询。
Finder 的 `.DS_Store` 与原子写临时文件不计入快照变化，
因此不会进一步触发数据库全量扫描；
内置 Shader 的待编译请求会被后续原生文件通知延后 200 ms。
按资产／依赖批次尾沿防抖、后台局部复核和 Windows 原生后端仍在路线图中，整个监听专项尚未完成。

- 引用表示必需且不可重绑定的借用；指针用于可空、可换 owner 或 moved-from 状态。
  unique_ptr 独占，shared_ptr 延长共享寿命；原生 Vulkan/GLFW handle 仍遵守各自协议。
- Renderer 是组合根，不是所有 GPU 对象的直接 owner；Device 也不反向拥有业务服务。
- Renderer 只向诊断消费者提供 const SceneRenderer 访问；修改生产状态走 Renderer 的帧／资源接口。
  Renderer 集成测试也走正常帧接口；需控制时间、提交与读回的底层测试独立创建 SceneRenderer，不访问 Renderer 私有成员。
  RenderContext 仍供 ImGui 宿主集成与后端诊断使用，不假装已完全隐藏后端。
- EditorAssets 中 SceneAssetReferences 先于其借用的 AssetManager 和 AssetDatabase 销毁，AssetManager 先于 AssetDatabase 销毁；开发态 app 的 AssetManager 自持索引。
  EditorAssets 和 AssetManager 从索引读取项目路径，提交后台任务时按值捕获路径快照，不让 Worker 借用数据库。
  app/editor 的 AssetManager 均先于 Engine 销毁；后台任务先结束，GPU 使用完成后再释放 Registry 和渲染资源。
- `PreparedFileImport::State` 直接拥有暂存路径、待发布文件和清理状态；放弃候选自动清理，发布仍执行输入复核及失败补偿。
  不再另包一层只转发 prepare/publish 的事务对象；这不改变批次并非崩溃原子的限制。

## 应用启动与失败清理

Application::run(Config) 完整执行：创建 Diagnostics／Engine → on_init → 引擎循环 → 私有 end。
Engine::create → Renderer::create → RenderContext::create 在局部准备 owner，全部成功才返回完整对象。
SceneRenderer::create 的 Swapchain 入口按 scene_output 选择输出，离屏尺寸入口用于显式离屏创建；
完整目标与管线准备成功后才返回对象；
构造函数保持私有，Renderer 和底层测试共用该创建边界，没有测试专用 friend 或初始化开关。
宿主以 `Config::Render::SceneOutput` 选择初始目标：app 直接呈现，Editor 离屏后由 ImGui 呈现，只创建一组场景资源。
Application::Options 提供具名宿主选项：缓存／日志目录，以及可选的输出模式／场景目标覆盖。
未指定覆盖时保留 run(Config) 的值；Editor 显式要求 SDR 和 Offscreen。
scene_output 不从 YAML 读取，也不代表 HDR／SDR 颜色模式。

- Engine 创建失败：释放 Diagnostics，不调用应用钩子，允许重试启动。
- on_init 一旦开始：预期失败沿 Result 返回，run 先做 Engine 关闭准备，再且仅一次调用 void on_shutdown。
  钩子需清理部分初始化的状态，不提供拒绝清理的失败协议；宿主资源释放后才销毁 Engine，Diagnostics 最后释放。
- 返回初始化或运行的原始结果。Error 保存消息和 std::error_code；图形边界通过 as_error 保留类别和数值。
  Comet::run 转换为退出码；YAML 解析异常仅在 ConfigLoader 适配。
- 底层不变量、第三方及标准库未预期异常不由 run／launch 捕获，不保证异常路径执行应用关闭钩子。
  LOG_FATAL 执行 assert／terminate、不展开栈，只用于明确终止的内部错误。

ImGuiContext 的 unique_ptr／私有 deleter 管理原生 Context，create 只发布完整候选。
Editor 通过 Renderer::set_overlay 一次绑定或解除绘制与交换链释放／重建钩子；拾取反馈是独立的当帧协议。
cleanup 先关闭实际存在的后端，再释放 pool／target；原生 Context 最后声明，保障构造展开时的清理顺序。
正常析构先等待 GPU、解除纹理注册；重复清理兼容重建中已关闭的后端。
Device::wait_idle_for_shutdown 提供析构等待边界，不等同于设备丢失恢复。

## 图像和目标

`FrameBuffer → ImageView → Image` 是共享所有权链；销毁时先销毁原生 framebuffer/view，再释放父引用。
Texture/RenderTarget 不重复保存同一个 Image 和 ImageView，取 image 通过 view 访问。
BorrowedImage 不销毁原生 image；交换链 image 的有效期还需要 Swapchain core owner。

SwapchainTarget 与 MultiTarget 是公开同级类型：

- SwapchainTarget 截取并共享构造时的 `Swapchain::Generation`，按实际 image 建 framebuffer。
- MultiTarget 不依赖 Swapchain，按 frame slot 建离屏附件。
- extent/frame count 构造后不变。resize 先完整创建候选 MultiTarget，成功才替换活动 owner；
  失败保留旧目标，旧版本由实际使用它的 slot 保留到完成。
- ImGui 的 TextureBinding 是 ImGuiContext 的私有嵌套类型，持有离屏 view/sampler 和 descriptor；
  只更新已完成 slot，不在 engine 增加 ImGui 专用资源类。

## 一帧经过哪里

```text
Engine::run → 内部 tick：事件与时间 → Application::on_update（消费上次 UI 请求、文档操作、资产维护与模式切换）
  → Renderer::prepare_frame
      回收完成的 upload → Presentation 等待 slot / acquire / 开始录制
  → Application::on_frame_ready（仅帧就绪后）
      ImGui begin → UI/请求收集、即时属性与 Gizmo、输入授权、最新 RenderView → ImGui end → 反馈提交
  → SceneRuntime::advance（Running：有界 Fixed Update → 一次普通 Update；Paused：仅显式单步推进）
  → SceneExtractor（读取此时的活动 Scene，更新 world transform）
  → Renderer::render_frame
      可见或直接呈现：SceneResolver → CPU pick → Shadow → Scene（Skybox / 物体 / 辅助线）
                    → 可选 Bloom → OutputPass
      隐藏离屏视图：消费拾取／辅助线请求，不解析资产或录制场景图
  → overlay render（录制已生成的 ImGui 数据）
  → Presentation submit / present
```

完整数据链为 `Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
Engine 拥有 Scene/Runtime，只同步借用宿主回调。Editor 为新场景统一执行资产准备与激活；准备失败不替换活动场景。
SceneDocument 只接收激活结果并更新文档路径／保存点；EditorSceneSession 保留 Edit Scene，负责 Play 副本与失败恢复，恢复时不重新准备资产。
文档操作错误通过 Result 返回，弹窗持有展示状态，不再在 SceneDocument 保存一份最近错误。
ProjectSession 的合法场景路径立即更新内存，偏好落盘失败只警告；后续记录同一路径仍可重试，不回滚成功的资产移动。
项目启动场景保存失败仍补偿源文件移动，不能将项目内容和本地会话偏好视为同一事务。
安装阶段由 `Editor::commit_scene` 结束旧交互、丢弃 Inspector 尚未执行的引用赋值并清除脚本编辑快照，
再经 Engine 停止旧 Runtime、交换 Scene owner；请求失效不能借用 Undo generation，因为 Play／Stop 保留 Edit 历史。
`SceneEditor::bind_scene` 统一重绑选择与资产引用，新 Edit 场景才重置历史。进入 Play 时历史仍指向保留的 Edit 场景，
Stop 返回该场景时也不重置历史。Hierarchy 的 UI 状态清理由宿主保留；首次启动尚无 SceneEditor 时先绑定文档历史，
服务装配完成后再绑定选择和引用追踪。
Editor 每次更新取走上一 UI 帧的场景请求，只执行一个：文件弹窗提交、菜单、Play 控制、结构编辑、重命名、Mesh 拖入、资产赋值依次优先；未保存确认期间仅接收文件弹窗提交，取消弹窗则全部丢弃。未选中的请求不延后重放。
Editor 保留帧顺序、跨面板请求仲裁与场景激活；ProjectSettings 负责项目名称、输入和启动场景的保存流程。
MaterialShaderReload 负责内置材质程序的请求组装、主线程发布、重试与日志；ShaderReload 只做文件监控、CPU 编译和结果交付，Worker 不接触 Renderer。
Renderer 不调用 UI 准备；SceneRenderer 不读 EditorMode/ImGui，不拥有 FrameScheduler 或呈现队列。
`on_frame_ready` 只在取得可绘制帧后运行，拾取反馈在 Runtime 更新和场景解析后、场景与 overlay 录制前同步应用，
因此选择框仍可进入当帧。交换链延期时不执行该回调或绘制，但 Runtime 继续推进。失败退出清空当前
CPU 诊断；正常关闭只取消未完成采样，保留上一条已完成帧的数据。

**运行与输入：** SceneRuntime 是时间截断的唯一入口，先有界固定更新再普通更新；暂停仍维护 UI、资产与绘制，
单步只推进一轮固定／普通更新。ViewportPanel 生产控制请求，由 Editor 在下一次 on_update 经 Engine 应用。
Runtime 每次先于各 System::on_start 通知 on_pause_changed(初始暂停值)，之后仅在 Running／Paused 实际切换时通知；通知期间禁止重入 Runtime，
不借该通知推进模拟或改场景结构。单步保持 Paused，不临时发出恢复／再暂停；Stop 直接清理，不先恢复子系统。
Runtime 统一保证 System 启停顺序和部分启动失败清理；具体 System 不重复保存仅用于检查调用顺序的 Scene owner。
两个宿主都收到同一个当帧 `Engine::FrameContext`：App 在 on_update 交付窗口 Gate 结果，Editor 在 on_frame_ready 交付 UI Gate 结果；上下文退出时丢弃授权，未授权释放按钮但不暂停模拟。
Viewport 在实际进入 Running 时一次性聚焦（Play／Resume），不在按钮发出请求时提前授权。
键盘／手柄跟随窗口焦点，鼠标另受画面悬停限制；Gate 分别维护整体与鼠标授权，避免工具栏点击／滚轮穿透，
且鼠标离开画面不再中断键盘输入。其他面板取得焦点、文本编辑、弹窗和失焦仍撤销整个授权。
隐藏离屏视图仍执行 UI、Runtime、Scene 提取和上传回收；暂时无呈现帧时只跳过 UI／提取／绘制。
最小化等待并重置墙钟增量、窗口瞬态及 Runtime 待处理按下；Gate 根据采样中断版本重新获取授权。

Project 持有默认 InputActions；App 启动、Editor 每次进入／重开 Play 时合成玩家覆盖，再交给 SceneRuntime 内的 RuntimeInput。
整份动作定义仍仅停止状态允许替换，不在运行中监视文件。Edit 的项目输入面板继续编辑默认值，不展示或回写玩家覆盖。
Project v2 为项目、动作和绑定保存非零 UUID：项目移动／改名、动作改名、绑定调参／排序不改变身份；
新建项目、动作或绑定才分配新身份。动作名仍是 Lua 的语义查询键，改名不自动改写脚本。
`common/Uuid` 复用原实体 UUID 实现，`EntityUuid` 保留为场景语义别名；不借用 AssetHandle 或依赖 Scene。
纯内存 InputActions 可匿名，Project 的读取／保存边界要求完整且有效的身份；旧 project.json 版本只报错，不迁移。
`InputOverrides` 是按动作／绑定 UUID 定位的稀疏值，control、scale、deadzone 分别可选，未覆盖字段始终继承当前默认。
disabled 是独立开关，不清空同一记录中的个人字段或子绑定；合成先检查动作身份／类型，再跳过禁用内容。
重新启用时按当前默认值重新校验保留的字段，不能因此抹掉不兼容记录；恢复默认才删除记录。
`PlayerInputSettings` 只负责用户配置目录、严格 JSON、加载基线与原子保存；不依赖 Project、Runtime 或 Editor。
宿主负责组合及一次性报告错误。未知身份／类型漂移只跳过对应记录，其他兼容覆盖继续使用，原记录不改；
结构错误使宿主回退默认，但加载不获得覆盖坏文件的空配置。保存失败保持原对象，不在启动时修复用户文件。
`comet_ui/PlayerInputPanel` 是 App／Editor 可共用的玩家覆盖草稿界面，不拥有 Project、文件或 Runtime；
Editor Play 与 App 共用面板。宿主加载文件、处理请求、先保存再提交重绑定，并把成功／失败交回面板。
App 在 on_update 提供延期帧输入回退；ready 帧恢复尚未消费的 Gate 快照，再按当帧 UI 授权计算一次最终输入。
弹窗关闭帧仍阻断键鼠，设置入口仅占用其命中的鼠标；不修改 Runtime 暂停状态。
面板仅返回候选与输入阻断状态，不增加跨层回调；翻译表由宿主借给当帧使用。关闭当帧也阻断输入，Esc 不穿透成 Stop。
加载错误由宿主持有并交共享 UI 绘制，场景／宿主退出清理；保存错误留在草稿面板的固定提示区，重试不丢草稿。
按键／手柄按钮录入读取原始物理帧，游戏仍读取 Gate 授权帧；录入只接受开始后的 pressed，不把 held 当作新输入。
Input 在真实失焦时也推进已有 interruption，重复焦点事件不推进；Gate 与录入可跨跳帧识别中断。
失焦仍保留必要的释放事件，不借用 discard_pending 清掉它们；恢复首帧不重放长按或位移。
`Input::Frame::first_connected_gamepad()` 供动作采样与录入共用；录入锁定该槽，观测到断开／首槽变化即取消。
槽号只是当前采样选择，不是持久设备或玩家身份；摇杆录制和多人分配不在此协议内。
`SceneRuntime::rebind_input_actions` 只允许非执行中的活动运行域，候选不能改变动作身份、名称、类型、归属或上下文定义。
RuntimeInput 在下一次 prepare 接收最后一份有效候选，按 UUID 保留未改绑定的固定步历史与路由；
只给新增／变动绑定建立基线，且等待实际授权和设备可用。动态动作组、普通／固定阶段电平与物理帧序号不重置。
Stop 取消尚未应用的候选；已经应用的映射保留，下一次 Play 由宿主重新合成文件配置。
`Window → Input::Frame → Gate → RuntimeInput → InputState → System／Lua`：
动作不读取平台或 ImGui，不绕过授权。RuntimeInput 拥有映射、活动动作组、普通／固定阶段快照和待消费输入；
SceneRuntime 只调用 prepare／consume_fixed／update 及生命周期接口，不处理按钮合并或分别安装物理／动作参数。
InputState 同时拥有该阶段的物理与动作值，只读公开，可复制保留；引用在输入 owner 下一次修改前有效。
零固定步不丢短按，多步不重复边沿，暂停／单步同时重建两类状态的基线。
Frame 的 `focused` 与 `pointer_enabled` 分别传递整体及鼠标授权；鼠标必须同时满足二者，
Gate 不能重新开放上游已经撤销的授权。Viewport 仅失去鼠标悬停时，RuntimeInput 丢弃未消费的鼠标点击和位移，
InputActions 将鼠标绑定标记为不可用，复用逐绑定清理；键盘／手柄及混合动作的其他来源仍保留。
已交付的鼠标按住状态产生一次释放，重新进入画面后仍需先松开再按下，不回放旧 delta。
Engine 启动 Runtime 使用 InputStart::Rebase，在首张已授权输入上丢弃旧边沿／位移并建立电平基线；
未授权帧不提前清除此意图。不把 Window 的物理 serial 当作 Gate 授权流的 serial，重开后长按键不会变成新按下。
多个绑定合为一个按钮电平，释放其中一个仍按住的动作不会产生释放；轴与位移不伪装成按钮。
CameraControllerSystem 只约定 `camera.*` 动作语义，具体设备、按键、反向和死区属于项目配置。

InputActions 保存 `Context{name, enabled, priority, consume}` 默认配置及 Action 的组引用，创建时统一校验；
最多 32 个组，名称沿用动作名规则，省略 enabled／priority／consume 时分别为 true／0／false。
enabled 的 consuming 组按物理 control 身份屏蔽严格低 priority 组的相同绑定；倍率和死区不改变 control 身份。
相同优先级共享输入，与配置顺序无关；无组公共动作既不被屏蔽也不参与消费。不同组不隐含互斥，
消费按绑定而非整个 action 或设备，不会让一个被阻挡的键连带禁用该动作的其他来源。
InputActions 的只读 `compare_bindings` 与实际路由共用消费判定；前者描述双方启用时的两两关系，
不读取运行态，也不推断其他组参与后的最终路由。项目输入面板只在草稿完整校验后调用它，
展示规范化 control 的共享／消费关系及默认禁用标记；无效草稿不沿用旧提示，合法重叠不阻止保存。
两面板共用 `comet_ui/input_widgets` 的关系正文。玩家侧只对 `InputOverrides::Resolution::actions` 展示关系，
与诊断共用同一合成结果；被拒绝补丁的控件值不冒充有效绑定。共享展示借用词表，不依赖 Editor 或新增翻译回调。
玩家面板按合成 issue 的身份标记拒绝状态；此时保留原补丁、显示有效默认，恢复绑定后才能编辑控制字段。
绑定仍可独立禁用／启用；动作类型已漂移时必须先恢复动作。禁用内容不显示默认控制冒充个人值，也不由 UI 另存备份。
诊断操作直接按 Issue 的动作／绑定 UUID 复用草稿恢复路径，不从错误文案判断类型，也不要求失效项仍在默认列表中。
它只删除对应记录；取消不保存，Apply 沿原持久化与运行时替换边界处理，不在合成时自动清洗。
正文与根操作栏分开，弹窗尺寸及位置在 Begin 前按 viewport 约束，缩小后不把取消／应用滚出可用区域。
RuntimeInput 保存本局的活动组状态，reset 恢复默认；InputState 仍是只读的阶段结果，System／Lua 不持有映射配置。
`comet.set_input_context → Scene::request_input_context → SceneRuntime::advance → RuntimeInput::set_context_enabled`：
Scene 只存非持久的、按组名合并的有界请求，不拥有输入状态。Runtime 在下一次 advance 的输入准备前消费，
因此同帧多次 Fixed Update、普通 Update 和通知 handler 使用同一套组状态；未知组走运行失败清理，不静默忽略。
请求允许在 on_start 发出；on_stop 无活动 Scene，只可向宿主的短期输出记录关闭组名，不能开启组。
ScriptSystem 在运行中的换绑、移除及成功换版后将清理输出交给同一 Scene 队列；完整 Stop／析构收集后丢弃。
旧 on_stop 先于新 on_start，同组仍按请求顺序最后写入生效。清理后续报错不撤销已经记录的关闭请求；
组名存在性仍在 RuntimeInput 验证，不另存输入配置。Stop／失败清空，暂停时可应用已排队请求但不推进模拟。

组切换依据绑定实际是否获得路由处理，也包括其他组被间接屏蔽／恢复的情况。
丢失绑定清掉该来源的固定步积累，动作最后一个已按住来源丢失时产生一次必要释放；
新获绑定只建立当前电平，不合成 pressed、不接收切换前或切换当帧的 delta；未受影响绑定保留自己的积累。
RuntimeInput 的逐绑定 pending 是固定步唯一动作历史，InputActions 共用采样／合成逻辑，
不会从原始物理 pending 再映射一次而重放已被消费的点击。组切换不触发全局 rebase，公共动作的积累不因此丢失。
路由只在组状态改变后重算，采样工作缓冲复用容量并逐槽覆盖；它不保存另一份跨帧业务状态。
手柄换槽不能继承旧槽的 pending，失焦／断连清理仍遵循输入授权规则。
底层物理快照保留原始授权输入供既有消费者查看，消费不修改窗口事件或 ImGui 快捷键；Lua 只读取具名动作。
这不是 Gameplay 广播通知：启停请求有唯一消费方 RuntimeInput，不经过 script.events，也没有新增回调链或 EventBus。

项目输入设置的链路是 `InputSettingsPanel 草稿 → ProjectSettings 校验／保存 → 宿主应用到停止态 RuntimeInput`。
ProjectSettings 返回配置保存结果，不依赖 Engine；面板不写文件、不操作 Runtime。
保存失败保留草稿，关闭丢弃未保存草稿，无变化保存由 Project 跳过写盘；运行时应用失败不冒充文件保存失败。
面板草稿保留既有 UUID，新建时生成；移除后新增不复用旧身份，UUID 不作为普通编辑字段展示。
动作、上下文和绑定数量上限由 InputActions 定义，项目解析和 UI 共用；键盘录入占用 ImGui 活动项及按键所有权，
Esc 取消，失焦／关闭结束录入，不把捕获键同时交给编辑器快捷键。该面板仅在 Edit 可用，不代表游戏内改键已实现。

PhysicsSystem 排在脚本之后：动态刚体的外部 Transform 写入作为传送同步，随后 Jolt 模拟并回写；
运动学刚体把 Transform 作为该固定步的目标，经 MoveKinematic 计算线／角速度，不回写 Scene。
目标不变时也更新运动学速度，避免残留上一固定步的速度。静态刚体只从 Scene 同步位置，不由模拟改写。
运动学可推动动态物体并触发静态 Trigger，但不是带阻挡／滑动的角色控制器；普通非动态物体之间不额外开启接触检测。
Collider 的尺寸乘以本地正缩放，球体暂要求均匀缩放，
刚体暂不允许父级，避免把局部 TRS 误当世界姿态。Scene 只保存 RigidBody／Collider 参数，
Play／app 启动时创建 Jolt 世界和 body，Stop／启动失败时清理；Edit Scene 不模拟。
RigidBody 保存显式 `mass`（kg，默认 1、最低 0.001），PropertyDescriptor 共用于编辑、撤销和序列化；
字段缺省取组件默认值，非法值不能通过文件或 Restore 绕过校验。Static／Kinematic 保留配置，质量响应仅作用于 Dynamic。
创建动态刚体时使用指定质量，惯性仍由已缩放的 Collider 计算，不再由形状体积隐式改变质量。
仅质量变化时在下一固定步原地更新质量与惯性、保留线／角速度并唤醒，不销毁 body 或重置接触身份；
同步先于待处理冲量，冲量使用更新后的质量。暂停不执行该同步，单步执行一次；当前未开放 Lua 改质量接口。
`comet.apply_impulse → Scene::request_apply_impulse → PhysicsSystem::fixed_update` 提交本实体的质心冲量，
方向为世界空间；Scene 只排队实体 ID 和有限 Vec3，不保存 BodyID／速度，不把命令写入刚体配置。
最多 128 条待处理请求；提交时要求当前运行场景中的动态刚体、Transform 与 Collider。
PhysicsSystem 在同步组件后、模拟前按提交顺序取出并执行一次，普通 Update／接触回调的请求留到后续固定步。
零固定步和暂停不消费，单步消费一次；Stop／失败清空。消费前已销毁、移除刚体或不再动态的目标丢弃，
单调实体 ID 防止同 UUID 重建接收旧请求；非法刚体配置仍由既有同步校验报错。
冲量由 Jolt 按质量改变速度并唤醒休眠体；执行前检查候选速度的有限性，防止巨大有限输入在质量换算或限速中溢出。
这不是全局事件通知，也不新增 PhysicsManager 或脚本对 System 的直连回调。
接触通知表示逻辑进入／离开，不把 Jolt 休眠后停止报告接触当作离开。仅延续两端整步未活动、BodyID 仍有效的既有接触；
静态体新增／移动或刚体移除／重建时，按受影响包围盒局部唤醒邻居，再由 Jolt 检测实际接触。
ScriptSystem 用事件参与实体的 UUID、组件寿命和脚本 Handle 直接查询实例，不为每条通知遍历全部脚本。

AudioSystem 接收同一暂停通知，停止 AudioPlayback 的设备回调，保留 Voice 播放状态；单步期间主线程独占混音推进，
按 Context::delta_time 读取并丢弃采样。先推进原有声音，再清理结束实例、同步组件和接收本步末的新请求，
避免提前消耗新音效时长；分数采样帧保留余量，不因连续单步积累截断误差。继续时从推进后的位置恢复设备输出，
不重播已结束音效；Stop 丢弃全部声音。单步首次创建播放设备时直接以暂停状态初始化，不先启动再关闭设备。
音频数据与设备仍由 Voice 保活，不向 Scene 或 Editor 暴露 miniaudio 类型。设备启停失败时清理声音并降级为本次运行静音。
Scene 的短音效待处理队列与 AudioSystem 的活跃播放预算分别有界：最多 64 个并发 one-shot，先到先播；
满额直接丢弃新请求，不保留延迟补播队列、不停止 Runtime，每次运行只警告一次。
自动 Audio Source 独立随组件管理，不计入 one-shot 预算；单步完成的 Voice 及时回收，Stop 清空播放与告警状态。
Offline 模式可同步读取 48 kHz 双声道 float PCM；Realtime 禁止外部读取，只有设备停止后才允许静默推进，
避免设备线程与主线程同时消费混音图。正常播放仍跟随设备时钟，单步新增音效只定位到更新末，不提供步内事件时间戳。

**运行失败：** System 更新失败逆序停止，不重试部分执行的模拟。Engine 不再提取部分写入的 Scene，
而是通过 `Renderer::render_frame()` 完成已 acquire 的无场景帧，再交给 Application::on_runtime_error；Editor 恢复 Edit，app 默认失败退出。
DeviceLost、空帧绘制或恢复失败仍退出。Scene 替换必须在 System 执行外，且先停止旧 Runtime。
启动加载期间尚未安装 Scene 也走无场景帧：保留清屏、UI 与提交，不执行场景相机诊断。
`render_frame(scene)` 表示真实场景，即使快照内容为空也仍检查主相机；不根据实体或相机数量猜测加载状态。
文件扫描、保存、同步加载和模式切换在 on_update 执行，不占用已 acquire 的帧，但仍可能占用主线程。
原生关闭可由 Editor 拦截，完成未保存决策后再 request_close。

**场景读取：** Scene 维护非持久化的 ID／UUID／父子索引，类型化 each 隔离 EnTT。
Transform 的 getter、each 和添加返回值均为只读；`Entity::try_set_transform` 提交完整值，
`try_edit_transform` 编辑临时副本后提交。成功立即更新本地 TRS，非法输入返回 false，相同值成功但不标脏；
对应的 `set_transform`／`edit_transform` 是 void 便捷入口，复用同一实现，调用约定失败走 LOG_FATAL。
前者用于脚本／属性编辑等可失败输入，后者用于确定有效的内部初始化，不通过静默忽略结果消除调用噪声。
读取本身不标脏，不再允许长期持有可变 Transform 引用绕过失效协议。
ComponentDescriptor 只公开只读组件访问，通过 assign_property 将属性写入交给类型化回调；
Transform 在副本上赋值后进入 try_set_transform，Inspector／Gizmo／Undo／Lua／CameraController 共用此边界。
Serializer 也通过该入口恢复属性；Restore 模式忽略 UI 可编辑标记并保留已存值，不执行编辑用的角度归一化。
NumericPropertyMetadata 的范围默认只是控件提示；明确设置 enforce_bounds 的属性才把范围作为数据契约。
Audio Source 音量采用该契约，编辑、恢复和保存共用 PropertyDescriptor 校验；AudioSystem 仍防御直接写入组件的越界值。
创建、TRS／父级变化和组件增删标记受影响子树；重复标记跳过已脏子树，销毁清除对应脏节点。
`update_world_transforms` 只消费脏集合，按父先子后更新；无变化时不扫描实体或比较 TRS。
`get_world_matrix` 是即时查询，仅同步该实体的脏祖先链；无关脏分支留给后续同步。
SceneExtractor 同步后直接读取 WorldTransformComponent，不在每个渲染项中触发更新或分配遍历容器。
WorldTransformComponent 是最近一次同步的只读缓存，不是独立冻结快照；需要跨修改保留时复制值，
RenderScene 则拥有本次提取的矩阵副本。pose_world_matrix 继承层级位置／旋转而忽略缩放。
SceneResolver 只解析 Camera、Mesh、Material 和 Environment 引用，不负责材质模板或参数合法性。

**渲染失败：** GraphicsError 沿 MaterialRenderer／DebugRenderer／SceneRenderer 返回。
准备阶段的普通失败可保留兼容旧材质或跳过调试批次；DeviceLost 原样传播。
不可恢复的准备／录制／提交／呈现失败使 Renderer 和 Engine 进入关闭准备，停止 System 与后台任务，
拒绝新帧、目标切换、Shader 发布和材质候选，仍允许解绑回调；之后 wait_idle 幂等。
prepare_frame 返回 `FramePreparation::Ready`／`Deferred` 区分就绪与延期，错误保留 GraphicsError；
render_frame 返回 `Result<void, GraphicsError>`。部分录制失败的命令缓冲不能结束后提交或复用；
这不同于上面 System 在场景录制前失败时完成空场景帧的恢复路径。

## Lua 脚本与参数

| 所属位置 | 持有与职责 |
| --- | --- |
| Script | 不可变入口／模块源码快照、字段定义（默认值与编辑语义）与事件声明；创建独立 Instance |
| Script::Instance | VM、保护调用、Lua 配置表与实例内模块缓存；共享只读源码，不共享 Lua table |
| 私有 lua_bindings | 当前实体／授权输入的 API 适配，不访问 Editor 或渲染资源 |
| ScriptSystem | 独占实例、保活所用 Script、同步组件寿命与阶段调用、交付场景通知 |
| ScriptComponent | 持久化 Handle 与稀疏覆盖；非持久化寿命与活动定义弱引用 |

复制组件不携带运行绑定。每实体独立 VM；阶段边界只查询脚本组件，新增批次按 UUID 启动，
按实际启动逆序停止，包含部分启动失败。on_stop 不访问实体／Scene；除普通日志外，只能记录受限输入组关闭输出；
宿主持有容器，回调不能直接改变路由，且完整退出不向世界追加请求。参数编辑不重启实例。

每个 VM 中，脚本返回的定义表与实例 `self` 分离；宿主创建仅含 `__index` 的私有元表，使缺失字段回退到本 VM 的定义表，
因此 `function script:helper()` 可由 `self:helper()` 调用，不要求作者手动安装元表。
实例自有字段优先，写入仍落在 `self`；定义中的普通值和 table 也可读，但不会跨实体共享。
`pairs(self)` 只列出实例自有字段。`self.parameters` 仍由每次调度安装只读配置；读取或修改 Lua 的
`properties/events` 表不等于修改已解析的 C++ 字段／事件声明，也不新增 Inspector 字段。
宿主始终从定义表取生命周期与已声明事件入口；给 `self.update` 赋值不重绑定宿主入口。
辅助方法共享本次保护调用与执行预算，不开新的保护边界；换版仍重建定义、self 和模块，不迁移 Lua 状态。

Inspector Edit 使用当前资产定义，Play 使用活动实例定义；Edit 定义切换会取消旧参数手势。
Play 实例换代后只清除旧脚本参数控件的活动状态，不打断其他属性／面板的输入，也不回写 Edit 历史。
更换脚本是 SceneEditor 的完整命令：先加载候选，再一次替换引用并清空覆盖，Edit 的 Undo 同时恢复二者。
清空引用同样清空覆盖；选同一引用不重置参数；失败不改变原绑定。Play 直接改运行副本，不写 Edit 历史。
源码变化由现有资产监听通知 AssetManager，Script 刷新先加载验证关联候选，再替换 Registry；失败保留旧版本。
同 Handle 的 Script 发布新版后，ScriptSystem 在实际 Fixed Update／Update 开始的 synchronize 中识别对象身份变化。
运行旧定义和候选定义的模块依赖共同决定关联组；候选删掉依赖也不能漏掉仍使用旧模块的实例。
先为需要换版的实例与同组新挂载组件创建候选 VM；旧实例按名称与 ParameterValue 类型保留兼容覆盖，
移除／改型字段使用新版默认值；新组件仍严格验证其覆盖，不借重载静默丢弃错误配置。
候选全部准备成功后，按实际启动逆序停止该组旧实例，再安装候选、更新组件活动定义和运行态覆盖，按 UUID 执行新版 on_start。
准备失败保留该组旧实例并延后新实例；失败快照记录实例身份、候选弱引用和参数，只有完整输入相同才跳过重试。
因此修复参数、删除阻塞组件或恢复缺失资产后仍可重试；失败记录不保活候选源码，Stop 清除。
语法／声明失败由原有资产加载入口拒绝，不发布到 Registry；ScriptSystem 不自己读文件、监听或另建资产版本缓存。
暂停中不运行 synchronize，继续或单步时切换；普通参数编辑不重建 VM，代码、定义和事件声明则随实例一起替换。
新 on_start 可能已修改 Scene，因此执行失败沿 Runtime 的整体停止／Editor 恢复 Edit 路径处理，不承诺回滚世界副作用。
仅 Lua 实例的 self 状态重置，Scene 会话值、实体、物理和待交付通知保留；pending 通知交给换代后的事件声明，不重放已消费通知。
不迁移任意 Lua 状态、不自动回写 Edit 参数；独立 app 可消费已发布新版，但没有新增源文件监听。

项目内 require 从 assets 根将点分名称解析为 `.module.lua`，模块是 source-only，不占 AssetRegistry／Handle／`.meta`。
`Script::module_path/module_name` 定义同一组名称与相对路径映射；编辑器创建入口复用该规则，不另写一套解析策略。
Project 的组件／模块请求沿 `EditorAssets → AssetSourceOperations` 共用文本文件创建事务，
模块不发布 metadata，只发布源码并提交候选扫描；成功仍沿既有依赖更新路径恢复消费者，不直接操作 Lua VM。
模块改名与跨目录拖放共用 move_module 请求／完成链路，使用路径请求而非 AssetHandle；Project 复用重命名弹窗，
EditorAssets 复用文件监听确认及 accept_scan，SourceOperations 执行无覆盖文件发布与候选扫描／失败回滚。
拖放使用独立的源码路径 payload，ImGui 复制路径字节；接收时只接受当前项目内的合法模块路径。
不自动改写 require；磁盘移动／改名成功不代表所有 Lua 引用已修好，后者继续由脚本关联组加载验证，失败保留 last-good。
模块删除同样使用路径请求，与资产删除共用确认框和文件事务；模块只有源码，资产仍为源码／metadata 对。
事务先暂存并扫描候选，再从原 assets 路径移入系统回收站，成功后提交索引；失败无覆盖恢复原文件。
回收站中可能已有部分副本；无法安全恢复的暂存内容保留并报告，不把它作为编辑器回收站或 Undo。
允许删除被 require 的模块，不清引用；活动脚本保留 last-good，首次加载失败，补回文件沿既有依赖链恢复。
模块仍不参与资产选择、资产引用拖放或场景 Undo，普通未知文件没有因此获得通用重命名／删除权限。
Script::load_group 逐入口执行初始化以收集传递闭包，同批共用读取字节，随后冻结；Instance 只能加载自身已准备的闭包。
模块返回 table，每 VM 独立缓存并诊断循环；运行回调只可返回已缓存模块，不在 Runtime 中找文件或执行新依赖。
不开放 package／io／os／原生加载，模块路径不允许符号链接别名，避免逻辑路径与依赖身份不一致。
源码限单文件 1 MiB／单批 8 MiB、256 文件、128 入口；每入口最多 64 模块、深度 16，VM 继续使用原有内存与指令预算。

AssetManager 的脚本加载与刷新集中在 `asset_manager_scripts.cpp`，仍是同一资源 owner，不新增 ScriptManager。
首次加载与刷新都按旧依赖及候选新依赖扩组，整批准备后更新既有 import dependency 路径索引，
再复核读取的真实字节、资产 revision 和 Registry 身份；owner 线程无外部回调地连续发布关联指针。
失败时保留旧依赖和已尝试路径的并集，缺失模块恢复可再次通知消费者；成功后只保留实际新依赖。
仅已证实过期的读取快照进入既有刷新重试队列；语法／声明错误等待新的文件变化，不每帧重读。
新消费者加入时，字节和自身闭包均未变化的旧 Script 保持指针身份，不因此重启旧实例。
模块不是跨实体共享可变状态的工具；项目需要共享玩法状态时仍显式使用 Scene 会话值或组件。

参数检查与合并分开：Inspector 调用 validate_overrides，不生成无用的完整参数表；
ScriptSystem 仅在覆盖变化时 resolve_parameters，Instance 在有效值或运行场景变化时重建 Lua 配置表。
两层快照分别检测覆盖和 Lua 输入，不引入跨层 revision 协议。
Script 的 retain_compatible_overrides 按声明名称和存储类型筛除失配覆盖，供 Runtime 换版和 Inspector 显式修复共用；
不把非法数值或过长字符串当作声明变更自动丢弃，过滤后仍须严格校验。
Edit 不自动迁移：Inspector 仅在存在失配项且剩余值合法时提供修复按钮，经原 Parameters 属性事务一次提交，
Undo 恢复原覆盖，包括原本的失配项；没有变化不产生历史。Play 始终按活动实例定义处理，不使用尚未运行的新资产定义。
明确编辑成默认值仍保存覆盖；“使用脚本默认值”删除单个覆盖，“恢复默认参数”清空全部覆盖。
两者共用参数编辑事务，Edit 可撤销、Play 不写 Edit 历史，均不重载源码。
self.parameters 及 Vec3／Vec4 配置只读，支持 pairs／索引／长度；运行状态写到 self 的其他字段，不持久化。
实体创建、会话值和事件载荷的 Vec3 共用读取规则，普通数组与只读参数代理均可输入；
必须恰好三个有限数值，拒绝额外键、数字字符串与错误维数，不通过复制参数表绕过只读语义。

Script::PropertyMap 是导出字段的单一真值，不另外保存一份 defaults：每项包含 ParameterValue 默认值与编辑语义。
裸三／四分量数组分别解析成 Math::Vec3／Vec4；`{type = "color", default = {r, g, b, a}}` 为 Vec4 添加 Color 语义。
Inspector 据此使用颜色控件或普通四分量控件，不按变量名推断；复用 Parameters 属性事务、撤销与 Play 副本。
Color 不是 Shader 参数类型，也不自动绑定 uniform；脚本仍显式调用材质 API。
Color／普通 Vec4 切换保留类型兼容的覆盖，Vec3／Vec4 不隐式升降维。所有分量必须是有限 float，颜色不限制在 0..1，
不自动转换色彩空间。`.scene` 用三／四元素数组保存覆盖，不复制 Lua 默认值或编辑语义。

实体参数用 `{type = "entity"}` 声明，ParameterValue 中保存独立的 EntityUuid 类型，
Serializer 写为 `{"entity": "UUID"}`；普通字符串不按内容猜测成引用，目标可以暂时缺失。
Editor 的实体选择控件显示名称／层级路径，与 Hierarchy 共用有文档世代校验的拖放载荷；
Hierarchy 在完成非拖放点击时才切换选择，起拖期间不改变 Inspector 目标，不额外维护面板锁定状态。
选择结果仍通过 Parameters 属性进入现有撤销历史，Play 仅修改运行副本且不接收 Edit 的实体拖放。
复制／粘贴子树时，SceneCommands 根据新旧 UUID 表重映射子树内引用；外部引用保留 UUID，
若目标在接收场景不存在则显示缺失。撤销删除保留原 UUID，不清除其他实体的配置。

VM 将 UUID 绑定成已有的受保护实体引用，不把 Scene 指针写进 Lua 配置；
引用同时校验场景世代和 EntityId，有效引用按实体实例比较，未分配或缺失引用可安全调用 `is_valid()`。
绑定后目标被删除、即使同 UUID 重建，已捕获的引用也不自动转向新实体；参数表重建或重新 Play 才重新解析配置。
`session_set/session_get` 立即读写当前运行场景的会话值，nil 表示删除；最多 128 个键，键长最多 128 字节。
值只接收 bool／有限 float／Vec3／最多 4096 字节的 string，不因共享 ParameterValue 类型而开放实体或 Vec4 存储。
暂停保留、单步照常读写，不进入 .scene 或 Edit 场景；on_stop 不访问会话状态。
Scene 的 begin_runtime／end_runtime 共用一份清理清单，清除会话、请求队列、材质覆盖和重开意图，
不把固定步冲量、阶段末结构变更和 Update 通知合并成同一种消费协议。

`comet.remove_rigid_body(reference)` 复用同一 EntityRequest 队列和 UUID／EntityId 身份检查，
只在阶段末移除 RigidBodyComponent，保留 Collider 配置、脚本、渲染和层级。重复请求或已无刚体幂等成功，
不重复占队列额度；失效／跨场景目标、非活动 Runtime 和首次请求遇队列满会失败。
`comet.has_rigid_body(reference)` 读取当前组件，不把尚未提交的意图当成已生效状态。
PhysicsSystem 在提交后的下一固定步复用既有组件同步移除物理 body、处理接触失效，不从 Lua 调 Jolt。
这不是任意组件增删反射接口；没有新组件字段或序列化格式，也没有第二份组件描述。
demo 从当前刚体是否存在恢复收集阶段，源码换版后可以重新开始短动画但不重复计分；不迁移任意 Lua self。

`script.events = { ["demo.score_changed"] = "on_score_changed" }` 声明场景内通知的接收方法，
Script 创建时校验名称、方法存在且可调用，每个脚本最多 128 项；活动实例使用其所保活版本的声明。
`comet.emit(name, value)` 只向 Scene 入队拥有值快照的 `Event`，不保存发布实体、Lua 表或函数引用。
名称非空且不含 NUL，最多 128 字节；载荷可为空或为 bool／有限 float／Vec3／最多 4096 字节的 string，
不接受 Entity、Vec4 或任意表。Scene 队列最多 1024 项，满时返回失败并走现有脚本错误路径。

```text
各次 Fixed Update → 提交结构请求
普通 Update：同步脚本实例 → update → 接触回调 → 取出一次通知批次 → 逐条交付
  → 后续 System（含 Audio）→ 提交结构请求
```

ScriptSystem 按入队顺序遍历通知，对每条通知按既有实例顺序调用 `self:handler(value)`；
载荷 Vec3 复用只读值绑定。交付对象以此时的活动实例为准，删除／更换组件的旧实例不接收；
交付前启动的新实例可以接收尚未消费的通知，已消费通知不会补发。结构删除请求仍在阶段末提交，
因此仅请求删除、尚未实际销毁的实例仍参与当前批次。handler 产生的新通知留在 Scene，下一次普通更新才取出。
`on_start` 可入队但不立即交付；暂停不消费，单步推进一批，Stop／失败／重新启动清空，不跨运行场景。
handler 复用 Instance 的保护调用、参数缓存与失败清理，不新增 Runtime 阶段、动态连接令牌或全局 EventBus。
会话值回答“当前状态是什么”，通知表达“刚发生了什么”；demo 的分数保留在会话，得分反馈改由通知触发。

`comet.create_entity(name, options)` 将初始 Transform 与可选 MeshRenderer 值快照交给 Scene::EntityCreation，
沿现有受限实体请求队列在阶段末提交，不在 Lua 回调内直接改 ECS 结构。
Lua 的 `mesh_source` 必须是当前场景的有效实体引用，入队前捕获其 mesh／material Handle；Scene 不保存源实体引用或 Lua 表。
新实体为根，只应用显式变换，不复制源的 Script／Physics／Audio、层级或材质运行覆盖。
省略 options 的空实体创建保持不变；未知字段、非法变换或缺失 MeshRenderer 在入队前失败。
Scene 也独立校验变换有限性和非零资源 Handle；阶段提交中完成变换和组件初始化，初始化失败回滚本实体。
这不保证句柄对应的资源已加载或 GPU 创建成功，既有资产／渲染路径仍负责这些失败。
暂停不执行脚本阶段，单步正常提交；失败或 Stop 丢弃未提交请求。已提交实体属于运行 Scene，
Editor Stop 丢弃 Play 副本，不是在 SceneRuntime::stop 内逐个删除运行中创建的实体。

`comet.restart_scene()` 只在更新调用中向 Scene 记录合并的重开意图；许可从 Script::Phase 得出，
输入指针只表示是否提供输入，不代表调用阶段。同阶段其他脚本／System 仍正常完成。
失败或 Stop 清掉意图；宿主在下一次 on_update 消费，不能从 Lua 栈内替换 Scene。
GameApp 保留启动时的 Scene 基线，EditorSceneSession 复用保留的 Edit Scene；两者都经 Serializer 克隆，
先完成候选准备，再停止旧 System、交换 Scene、启动新 System。基线只保存场景配置和 Handle，不复制 GPU 资源。
重开保留 Edit 文档与历史；目标、Transform、会话值、Lua 实例、物理和声音均来自新一局。
候选克隆／准备失败时旧局仍活动，请求已消费，不自动重试；替换后启动失败无法恢复旧模拟，
Editor 回到原 Edit，app 沿现有错误返回退出。暂停重开在 on_start 前向各 System 声明暂停，避免自动播放短暂发声。
这不是跨场景加载或资产版本快照：场景基线不重读磁盘，资产仍由 Registry 提供当前有效版本。

源码最多 1 MiB、每 VM 的 Lua 堆最多 8 MiB、每次保护调用最多约 20 万条指令；
不等于墙钟超时或安全沙箱。不开放文件、原生库、动态代码、元表和 rawset；require 仅限上述受控项目模块。
默认参数解析与生命周期分发都在 lua_pcall 内；错误可能通过 longjmp 返回，不能依赖回调内 C++ 局部对象的析构。
保护调用使用私有消息处理器在 Lua 栈展开前生成 traceback，成功和失败均恢复调用前的栈高度并移除指令 hook。
非字符串错误使用固定说明，不执行项目的 tostring；不开放 debug 库。Lua 内存耗尽会跳过消息处理器，
生成诊断本身失败也可能只能返回 Lua 的错误处理失败提示，不额外分配救援 VM 或保证完整调用栈。
诊断仍沿既有 Error／Result、ScriptSystem 实体上下文及 Log 传递，不增加平行错误对象或 UI。
正常诊断使用 `comet.log(string)`，LuaBindings 直接转交既有 Logger；文件／行号由 Lua 调用栈提供，
不暴露 debug 库，也不把项目字符串当格式串。只在 Instance::invoke 期间开放，包括 Stop；
Script 准备／模块顶层执行不产生日志副作用。每次调用的输出计数随 Context 重置，
单条超过 4096 字节或超过 16 条时省略消息并至多记录一次 Warning，不将输出饱和视为脚本失败。
这不是跨实例／跨帧的全局限流；项目仍应避免逐帧输出。参数数量／类型或阶段用错仍沿原错误边界诊断。
解析结果和绑定返回字符串由保护调用外层持有，回调只借用，正常或失败返回后统一释放；不承诺宿主内存耗尽后的恢复。
参数表与会话值复用单个名称／值校验；会话值额外限制类型，不为单次赋值构造临时参数表。
Lua 只借用当前阶段的 InputState，通过具名动作查询输入，统一遵守重绑定与动作组开关；不提供原始按键入口。
绑定层只复用 InputActions 的组名校验与容量约束，不采样或持有 RuntimeInput。
Script::Invocation 与 LuaBindings::Context 各只传一个 input，结束调用后解除借用，不自行采集或消耗输入。
材质写入也只借用当前调用的 MaterialParameterValidator；Engine 将 MaterialPrograms 接入 ScriptSystem，
Lua／Scene 不包含 render 或 graphics 头。Result 的错误先存入外层 Context，再调用 luaL_error，
不让 Result／字符串局部对象跨越 longjmp。更多组件操作与完整脚本调试按路线图扩展。

### 脚本材质覆盖

`comet.set_material_scalar/vector → Scene → MaterialParameterValidator → MaterialPrograms`。
Scene 检查活动运行态、本实体 MeshRenderer、参数名及有限值；MaterialPrograms 解析共享 Material，
复用 MaterialLayout 校验名称、类型和标量范围。有项目程序时优先使用已发布布局；从未发布时，
只对已加载 Artifact 调用既有 describe 预检，不提前宣称 GPU 发布成功，也不把新候选覆盖到旧发布布局。

Scene 按实体保存 `shared_ptr<const MaterialOverrides>`，包含材质 Handle、稳定运行实例身份与 float／Vec4 覆盖。
同值仍校验，但不替换快照；改变值才构造新快照，不复制共享 Material 或 Texture。
SceneExtractor 和 SceneResolver 逐层持有快照，渲染时按“实体覆盖 → Material 值 → 布局默认值”打包。
暂停保留、单步照常写入；Runtime 启停、实体／MeshRenderer 删除及可观察的材质 Handle 换绑清除覆盖。
覆盖不进入场景序列化、Edit 历史和 `.mat`；Stop 后旧提交仍持有自己的快照及 GPU 资源直到帧完成。
目前只修改当前实体，未开放纹理覆盖、全局参数或跨实体材质操作。

## 渲染诊断

Engine 在主循环标记阶段，FrameDiagnostics 负责计时、发布与保存上一完整循环的
events/update/prepare/render-submit 墙钟分段；prepare 包含帧等待和 UI，
render-submit 包含提取、解析、录制与提交／呈现调用。暂缓呈现记录 rendered=false；错误中止不发布半条样本，
最小化等待不作为正常帧采样。该运行时开关独立于 scope Profiler 的编译开关。

Renderer 拥有 RenderDiagnostics，SceneRenderer 只在录制图时借用，不再次扩大场景资源所有权。
主循环使用 FrameDiagnostics::Timing，图采样使用 RenderDiagnostics::GraphTiming：计量范围、序号和完成时刻不同，不合并成混合数据结构。
SceneRenderer::record_pass 负责具名 Pass 分发，局部 lambda 仅适配 RenderGraph 的同步回调，不保存或跨线程调度。
诊断包围既有 Plan::record：CPU 明细计量各回调，总时间还包含图校验与屏障录制；GPU 使用图首、各 pass 结束、
图尾导出屏障后的时间戳。相邻 GPU 边界包含依赖等待，不表示各 pass 独占硬件的时间。
场景图不含 ImGui overlay、present 完成或其他队列，CPU/GPU 快照分别带帧序号。
隐藏离屏视图将 scene_rendered 置 false，清除当前图样本与待发布的旧查询；不伪造零耗时图。
材质绘制／绑定等当帧活动计数归零；缓存材质数和 FrameSet 数按实际驻留状态查询，不因隐藏视口归零。
历史窗口自然老化，面板明确提示显示的是近期历史；CPU 整帧与显存采样不受影响。

每个 FrameSlot 懒创建固定 34 项的 GpuTimer，最多记录 32 个 pass、每个名称最多 128 字节。
超限仍执行完整图，截断 CPU 明细并跳过该图的 GPU 采样。保留槽位待完成样本、最新快照及有界时间统计。
FrameDiagnostics 与 RenderDiagnostics 共用纯 CPU 的 TimingHistory：100 个 50 ms 桶，逐样本累计 sum/count/max，
查询近 1 秒统计和近 5 秒趋势；图布局变化时清空对应历史。GPU 按确认完成后的收集时刻归桶，不伪装为执行时间线。
面板每 250 ms 复制显示快照，暂停只冻结显示，不影响采样。停止后保留最后窗口，重新启用清空旧采样。
graphics/gpu_timer 封装原生查询、有效位和周期换算，render 不直接操作 vk::QueryPool。
帧的完成 serial 经 FrameScheduler 确认后才能读取查询，不以 availability 代替完成证据，也不额外等待 GPU。
池在首次录制前交给 FrameSlot 保活；关闭采样或销毁诊断对象不提前释放在途池。Renderer 关闭仍先等待既有提交。
普通查询创建／读取失败只关闭 GPU 采样并报告一次；DeviceLost 沿 GraphicsError 返回，不吞成诊断降级。
图录制失败沿用原有中止帧协议，不发布半条样本、不提交部分命令、不在同一测量帧重试录制。

内存预算开启后至多每秒采样一次。详细分配报告由 Allocator 生成 VMA 原生 JSON，Device 转发；
Editor 在 on_update 消费面板请求并原子写入项目私有状态目录，不在 GPU 层处理项目路径，面板 getter 也不触发采样或写盘。
报告反映 VMA 管理的分配与驱动预算，并非所有 GPU 内存；手动生成／保存仍占用主线程。

## 材质、Shader 与 Pipeline

从哪里读代码：

| 入口 | 职责 | 不负责 |
| --- | --- | --- |
| `render/material/material.h` | Material 实例属性与 revision | 布局反射、资产身份、GPU 缓存、UI 控件 |
| `render/material/material_layout.h` | 不可变 MaterialLayout 参数布局、默认值、编辑语义与 Shader 校验 | 可变材质实例、GPU owner |
| `render/material/material_runtime.h` | MaterialRuntimeCache 准备并缓存 Texture 引用和参数字节 | 创建 Vulkan 对象 |
| `render/material/material_shader.h` | 具名程序字节码、内置程序与材质映射、固定接口校验、覆盖合并 | GPU owner、后台任务、发布事务 |
| `render/material/material_programs.h` | 跨目标重建保存已发布程序版本；项目程序预检与实体材质参数校验 | 目标相关 Pipeline、源码编译、文件监视 |
| `render/material/material_renderer.h` | 帧／材质 descriptor、Pipeline 选择、排序与绘制 | 解析 Scene 或资产文件 |
| `tools/shader/compiler.h` | CPU 源编译、依赖快照和诊断；CLI 负责文件输出 | Vulkan 对象、编辑器热重载编排 |
| `graphics/pipeline/shader_interface.h` | SPIR-V 入口级自有反射数据，仅公开 Comet 类型 | 自动生成编辑语义、完整字节码校验 |
| `graphics/pipeline/shader.h` | ShaderLayout 覆盖校验、局部 Shader GPU 候选 | 监视源码、名称缓存、启动编译任务 |
| `graphics/pipeline/pipeline.h` | PipelineLayout、Pipeline 与弱对象缓存 | 磁盘缓存策略、资产发布 |

### Forward 光照

`LightComponent → SceneExtractor → RenderScene.lights → RenderSubmission.lights → LightingData → FrameSet`。
组件只保存类型、启用、线性颜色、强度、范围、聚光半锥角和 casts_shadow；枚举以稳定字符串写入 JSON，
Inspector／Undo／Clone 复用 PropertyDescriptor，不增加灯光专用命令。
pose_world_matrix 共用相机姿态语义：世界位置含父级变换，方向只继承旋转，不继承本地或祖先缩放。

`render/lighting.h/.cpp` 负责值快照与 std140 打包，无 Scene 或 GPU owner。
按 EntityId 稳定选择前 32 个有效光源；非法参数与超限分别统计，数量变化时报告，不能当作空间筛选。
FrameSet binding 0 是相机，binding 1 是片元光照 UBO，binding 2 是阴影 sampler2D，binding 3/4/5 是环境 irradiance/specular/BRDF LUT。
UBO 每灯 64 字节，使用 position、type、direction、range、color、intensity、锥角和阴影标记等具名字段；
末尾是有效／超限／无效数量、shadow_view_projection、shadow_light_index、shadow_depth_bias 和 shadow_texel_size。
另有 environment vec4 保存照明强度、镜面最大 LOD 和旋转 sin/cos。类型、标记与计数仍用 float 编码，总计 2160 字节；C++ 静态断言和 Shader 反射测试核对偏移、数组步长与大小。
每个 slot 等待完成后写入，FrameResources 由在途帧保活；灯光变化不更新材质 revision 或重建 MaterialSet。

受光表面统一使用 `pbr`；点光使用有限范围衰减，聚光增加锥角权重。
法线按模型矩阵逆转置变换，近奇异变换输出零法线；无直接光和环境贡献时为黑色，不添加隐藏环境光。
强度是当前渲染参数，不承诺完整物理光度单位；尚无 clustered/tiled 筛选。

`pbr` 在同一场景 pass 内使用 GGX、height-correlated Smith 与 Schlick Fresnel 计算直接光照，
通过 `lighting/forward.glsl` 的 `sample_light` 和 `shadow_visibility` 获取光照，不增加 PBR pass 或 GPU 资源管理器。
MaterialSet 的 32 字节参数保存 base_color、metallic、roughness；沿用共享布局、Inspector 与不可变材质版本。
MaterialSet binding 1 为可选 base_color_texture，采样值在线性空间乘 base_color；sRGB 解码由纹理格式负责。
MaterialLayout 标记可选槽位，PreparedMaterial 的空纹理表示未指定；MaterialRenderer 绑定自身持有的 1×1 白色纹理。
实际绑定纹理由不可变 MaterialResources 保活，并与普通纹理一样参与上传等待；不往项目资产库注入默认纹理。
显式纹理引用仍经 AssetManager 解析，丢失或导入失败不冒充未指定。Inspector 清空槽位时删除属性，不序列化零 Handle。
Shader 将金属度限制到 0..1、粗糙度限制到 0.045..1，RGB 限制到 RGBA16F 有限范围；当前仍是不透明材质。
Frame binding 0 由 `common/frame.glsl` 统一声明，160 字节包括两矩阵、相机位置、正交标记和观察方向。
透视使用相机位置减世界位置，正交使用统一方向；投影判断只适用于当前引擎标准矩阵。
相机数据按已完成的帧槽写入，不修改材质 revision，不扩大 SceneRenderer 或 Inspector 的材质分支。

### 方向光阴影

`ShadowPass::prepare` 对提交网格的世界包围盒求并集，再由 LightingData 选择前 32 灯中
EntityId 最小、强度大于零且 casts_shadow 开启的方向光，构造覆盖场景的正交投影。
无相机、无有效网格或无符合条件的光源时，阴影索引为 -1；旧场景缺失开关时默认关闭。
阴影开关复用已有保存、克隆和属性撤销链路，不增加新组件或编辑命令。
PropertyDescriptor 默认仍要求字段存在；仅 casts_shadow 显式标记 required=false，
缺失时保留组件默认值，null／非法类型仍拒绝，不放宽其他必填字段。

SceneRenderer 的有序图是 ShadowPass → scene → 可选 Bloom → OutputPass。D32 深度图按帧槽位独立，
深度写入到片元采样的 layout/access/stage 转换由 RenderGraph 生成；不在材质内插入屏障。
ShadowPass 保留通道、目标、管线和 Mesh；FrameResources 保留实际采样 View 与 nearest Sampler，
完整 RenderState 替换与 resize 都不销毁在途帧使用的资源。各 pass 的上传等待按 semaphore 合并。
MaterialRenderer 只接收已准备的 LightingData 与有效采样 View，不创建隐式后备资源；
未启用阴影时 ShadowPass 仍清除深度图，保持固定图与有效 descriptor。

当前固定 1024²、单方向光、手工 3×3 PCF；使用接收平面深度梯度补偿邻域采样，
偏移覆盖 nearest texel 量化误差并按入射角调整。所有提交网格均按不透明遮挡物处理，
只有受光材质接收阴影。阴影视口使用正高度，与采样 UV 匹配；主场景仍使用负高度。

尚无级联、texel 稳定化、视锥筛选、透明裁切、逐物体投影开关或点／聚光阴影；
大场景或动态包围盒会降低阴影精度并可能抖动，后续按实际画面需求扩展。

### 场景环境与 IBL

SceneEnvironment 保存单一环境 Handle、独立背景／照明开关及强度，共享 Y 旋转；不包含 GPU owner，也不作为实体组件。
SceneExtractor 复制配置，SceneResolver 从 AssetRegistry 取得 Environment，RenderSubmission 持有本帧版本。
完整 Environment 拥有 background、irradiance、specular、brdf 四个 Texture，复用纹理上传；材质 2D 槽拒绝 Environment。
首次导入的临时 Environment 仅有 background，`has_lighting()` 为 false；MaterialRenderer 使用无 IBL 基线，不访问空光照纹理。
HDR 导入器生成六层 RGBA16F 和背景 mip 链，UploadBatch 一次提交全部 mip／layer，统一转入 SampledRead。
SkyboxPass 在场景 RenderPass 内先画全屏三角形，不读写深度；随后几何和辅助线按原流程绘制。
射线由逆投影、相机旋转和环境旋转重建，丢弃相机平移；正交视图也按射线方向采样。
SkyboxPass、BloomPass 和 OutputPass 共用不可变 SampledImageBinding，绑定拥有按连续 binding 排列的 views、layout、sampler 和 descriptor pool；帧保留绑定及管线，不改写在途 descriptor。
SkyboxPass 返回实际绘制所需的上传等待，SceneRenderer 只合并，不重复判断背景开关或资源条件。
MaterialRenderer 在同一场景 pass 消费 IBL，不新增 pass/System。槽位 fence 完成后更新 FrameSet 并保留整代 Environment，合并实际采样纹理的上传等待；缺失／禁用时绑定有效黑色占位并设置照明强度为零。
`environment/lighting.glsl` 使用 split-sum：irradiance 存 E/pi，specular 按 perceptual roughness 选择 GGX 预滤波 mip，BRDF LUT 的 RG 存 F0 的比例与偏置。
CPU 积分采用 Hammersley 256 样本、alpha=roughness²、height-correlated Smith，与直接光 GGX 约定一致；预滤波按 PDF 选源 mip，并跨 cubemap 面重投影过滤采样。
LUT 在线程安全静态初始化中只积分一次，随后随每个缓存保存；单散射近似不实现多重散射补偿、局部探针或环境遮蔽。
算法依据：[Filament 的 IBL 与预滤波推导](https://google.github.io/filament/main/filament.html#lighting/imagebasedlights)。

### 环境资产准备

`场景引用 → AssetManager::request_load → AssetTaskQueue → ImportService → EnvironmentArtifact → owner 发布 Environment`。
ComponentRegistry::collect_asset_references 区分 All 与 Runtime：All 保留文档中全部引用，
Runtime 跳过背景和照明均关闭的环境，App 与独立准备工具共用这一选择；不删除或改写保存的 Handle。
Editor 使用 All 预加载，方便编辑时随时启用。场景环境是可选引用，缺失时保留 Handle 并诊断；
app 拒绝必需引用失败，编辑器允许修复。DeviceLost 始终向上传递。
`request_load` 成功表示接受需求；`references_ready` 才判断完整环境已驻留。app 在等待期间保留候选 Scene，
继续正常窗口事件与帧循环，就绪后再安装并启动 Runtime；背景／照明都关闭时不请求环境，失败的可选环境可回退。
AssetManager 的私有 environment_state 从 Registry、当前 revision 的排队／预约及失败记录推导状态，不缓存第二份状态。
重复请求不重排队；同步加载拒绝抢跑正在准备的任务；低清预览不算完成。失败等待源变化，已有完整版本仍优先作为可用版本。
SceneResolver 不将 Registry 中尚未发布的环境当作错误，等待期间返回无环境纹理的提交；真实缺失／准备失败由资产层报告。
已发布对象不是 Environment 时，SceneResolver 报告类型错误并按 Handle 去重，不依赖 AssetManager 的调度状态。
ImportService 负责 CPU 导入与缓存，AssetManager 负责加载需求、revision 检查和运行时发布；二者不访问 ImGui。
后台首次准备与驻留重载共用缓存路径，输入路径／内容指纹和算法版本必须匹配；格式、尺寸、载荷长度和校验值不符则重建。
缓存原子写只保证单文件；缓存可独立存在，不代表 GPU 已发布。GPU 创建失败不替换 Registry，旧帧仍持有旧版本。
EnvironmentArtifact v2 将背景、最高 16² 漫反射、最高 128² 镜面 mip 链和 128² LUT 作为同一载荷校验；旧 v1 自动重建。
导入读取先比较头部版本与源指纹，再分配和校验完整载荷，避免为已过期的大产物做无效读取。
冷导入通过单次 Preview 交付最高 128²/面的背景，后台不操作 Registry/GPU；owner 在发布预算内取走快照并复核 revision。
正式资源仍在 GPU 四张纹理全部成功且 revision 有效后整组替换；失败撤销临时预览，已有完整版本保持不变。
缓存命中和完整版本重导入不生成临时背景。IBL 卷积按需解码并复用被采样的 mip，不在每个采样点重复转换 float16。
环境任务根据源尺寸估算工作集，默认共享 2 GiB CPU 预约预算；完成候选在发布或丢弃前不释放预约。
主线程仅做小型头部／文件大小预检和 GPU 发布，CPU 大块读取、转换、校验与缓存写入在 Worker；预检后源增长超预算会失败。
此预算不是进程 RSS 上限，也不覆盖普通纹理／Mesh 解码或 GPU 分配；GPU 创建仍使用现有资源工厂的预算及 Result。
显式同步 load_environment 保留给尚未排队的阻塞式工具调用；app/editor 场景需求不使用它。文件复制与其他资产首次加载仍可能阻塞主线程。

### 材质准备与寿命

MaterialRenderer 保留资源所有权，主流程按阶段组织：同步运行实例／材质输入 → 准备程序，
绘制时更新帧资源 → 准备并排序绘制列表 → 录制 → 回收未使用缓存。不另建转发 Manager。
材质、天空盒与阴影通过 Device::query_format_support 查询最优平铺图像的采样、线性过滤和深度附件能力；
该查询不替代具体尺寸、用途组合与采样数的创建校验，Vulkan 格式转换留在 graphics 实现内。

内置 `unlit_color` / `pbr` 的初始 metadata 由 MaterialLayout::find_builtin 共享。
内置与项目属性共用 MaterialLayout 的反射映射；项目 `.shader` 的 metadata 先转为属性声明，反射再填充 offset／块大小／binding。
映射按 shader_name（为空时使用逻辑属性名）匹配已登记属性；
名称、默认值、范围、步长和 Color/Vector 语义仍由 metadata 提供。参数块 binding 由布局指导创建和写入，不再固定为 0。
这是当前实现的职责位置，不表示 `.shader` 永久拥有材质编辑语义；后续边界和迁移前提见[内容资源约束](../engine-roadmap.md#内容资源的跨阶段约束)。
未知／缺失／改类型字段、多参数块与不支持的资源形状拒绝；相同物理布局复用原对象，不增加平行 revision 计数。
Editor 在初始化和内置程序成功热更后向 Inspector／Project 交付模板布局；项目材质优先读取 MaterialPrograms 已发布版本的布局，未使用的新程序可做 CPU 预览。控件和草稿校验使用对应布局，不清空草稿、不自动保存。
Editor 通过 Renderer 获取该快照，不直接访问 SceneRenderer 的目标实现；ImGui 初始化和显存诊断仍留在明确的图形集成入口。
独立面板初始化使用 MaterialLayout::builtins；收到发布列表后不再补回未发布模板。目标重建后同值布局仍可用于 UI，不让面板引用 Renderer。
布局构造后不可变，以对象身份区分版本；Material 可修改，以自身 revision 标记真实变化。
MaterialLayout 保留 metadata 声明顺序，PreparedMaterial 单独按 binding 排序纹理绑定；热更物理 binding 不改变 Inspector 槽位顺序。
MaterialRuntimeCache 按 `(Material Handle, instance_id)` 索引，0 表示共享材质基线，其余表示实体运行覆盖。
比较 Material 对象身份／revision、布局身份与覆盖快照身份；失败也缓存，输入变化后才重试。
prepare／rebind 返回 Result 和具体诊断，不自行写日志；MaterialRenderer 负责去重报告与回退。
未使用项按渲染周期回收，已取得的 PreparedMaterial 仍拥有当时的 Texture 和参数副本。

set 0 是按 slot 更新的相机 FrameSet；set 1 是按不可变材质版本创建的 MaterialSet；model matrix 使用 push constant。
MaterialResources 持有 PreparedMaterial、PipelineState、Sampler、参数 buffer 与 descriptor pool；
FrameResources 持有 frame layout、pool 和相机 buffer。实际绘制的 FrameSlot 保留它们及 Mesh，直到 GPU 完成。
CPU 缓存淘汰不代表 GPU 已完成，不能据此删除 retained owners。
可恢复的 GPU 候选失败可沿用旧 MaterialResources，同一 PreparedMaterial／PipelineState 候选延后 60 个 frame serial 重试，
新候选可立即尝试；失败记录只弱引用 PipelineState，不延长旧 Pipeline 寿命。
DeviceLost 则交给应用退出清理边界，不把失效设备当作可继续渲染的旧版本。
CPU 准备失败与 GPU 创建失败共用回退判断，只保留同 Handle／运行实例的完整旧 MaterialResources，
包括匹配它的 PipelineState；缓存限定于当前 RenderState，因此不跨不兼容 RenderPass 回退。无旧版、不支持模板则跳过。
清除引用、移除物体或切换到其他 Handle 不回退到无关材质；回退项仍标记使用。
绘制周期结束时清理未使用缓存；无相机的绘制周期也执行这一步。隐藏视口保留有效缓存，避免恢复时全部重建。
Renderer 每次 prepare_frame 在 acquire 前检查 Registry，移除已注销材质的 CPU／GPU 缓存以及项目程序的材质依赖引用，隐藏／延期同样执行。
依赖变化只解除相应材质／覆盖导致的失败，不让纯 Shader 失败因无关材质删除而重复尝试。
同 Handle 的新版本不触发这类淘汰，仍允许准备失败时回退旧兼容版本；在途帧保活不受缓存淘汰影响。
队列按模板名、材质 Handle 与运行实例身份排序。

编辑器材质文件修改采用显式准备／提交，区别于上述绘制时的延迟准备：
AssetManager::prepare_material_update 保留源 revision、序列化内容及只读运行时候选，不改原材质文件或该材质的 Registry 条目。
Renderer 在无活动帧时接收候选，MaterialRenderer 在局部缓存打包参数并创建完整 GPU 绑定；失败丢弃候选。
editor/assets/material_editing 的 apply_material_edit 统一串联以上步骤：提交文件和 Registry，成功才发布 GPU 候选；
MaterialUpdate 发布结束后释放候选自身的源引用；调用右值限定的 publish 并不意味着 C++ 对象已经析构。
保存失败时两类候选均释放，Inspector 恢复旧模板和参数。Editor 只分发请求；EditorAssets 保留底层 prepare/commit，
纹理使用明确的 apply_texture_edit，不再有绕过 GPU 准备的通用材质提交分支。
这两个编辑入口同时涉及源数据和运行时发布，不能仅按所在目录拆开提交步骤；它们不代表
`AssetManager` 重新接管了移动、删除、导入和创建等源文件工作流。
这一小段同步操作不得插入 Shader 发布或 renderer 重建；旧在途帧仍独立持有旧 MaterialResources。
不以回调把 Renderer 注入资产层；AssetManager 不认识 Pipeline/Descriptor，MaterialRenderer 不解析资产文件。
Project 新建材质只创建源和身份，首次指定给物体时沿用资产加载；模板切换不生成新 Handle，也不修改场景引用。

### 编译、反射与缓存边界

| 模块 | 输入／输出 | 约束 |
| --- | --- | --- |
| ShaderCompiler / CLI | 源码、include、选项 → SPIR-V 与依赖快照 | 静态依赖 glslang，不链接 engine，不创建 GPU 对象 |
| ShaderInterface | 指定入口字节码 → 自有反射值 | 不向业务层暴露原生 SPIR-V 类型，不等同于完整 validator |
| MaterialLayout | metadata + 反射 → 不可变参数布局 | 编辑语义来自 metadata；目前仅支持已登记字段和普通 float sampler2D |
| Shader / PipelineKey | 反射、布局、配置 → 完整候选 | 创建前检查 descriptor、push constant、顶点格式和阶段连接 |
| PipelineManager | 完整 key → 弱引用 Pipeline 缓存 | 使用方和 FrameSlot 持有实际对象，名称仅作标签 |

编译请求独占解析器与输入快照，成功前复核内容；失败不返回字节码，但保留诊断与失败依赖。
快照包括缺失 include 候选，读取错误不能伪装成缺失。depfile 仅列存在文件，新增遮蔽文件不保证触发构建；
SPIR-V 和 depfile 各自原子写，不是跨文件事务。第三方异常边界保留在编译工具内部。

反射检查是保守契约：阶段 I/O 仅接纳 location-based 32 位标量／向量；
数组、矩阵、结构体、64 位和非零 component 明确拒绝。允许未消费输出／顶点属性；
normalized／packed 转换、复杂插值、StorageImage 格式及完整附件兼容仍待扩展。
资源布局比较包含成员名称，不代表所有 Shader 行为兼容。

#### GPU 创建与错误

- 公开创建入口返回完整候选或 Result；CPU 校验错误不伪造 Vulkan 错误码。
- graphics/creation.h 接管 device-owned 句柄后判断返回码，失败回收包括部分创建的原生对象。
  Pipeline 先销毁自身句柄再释放 Layout；DescriptorSet 借用池内句柄，由池回收。
- 描述符写入立即消费 Buffer／ImageView／Sampler 引用，不保活资源，也不自动同步 GPU。
  消费者负责可修改时点与帧保活；ImGui 第三方后端内部失败不属于所有 Comet 工厂的覆盖保证。
- 普通创建失败按消费者策略保留旧版或跳过，DeviceLost 传到应用退出清理；不用 LOG_FATAL 替代可恢复错误。
  标准库等未预期异常仍可传播，不承诺 noexcept。完成队列在成功、失败或展开时均释放已消费槽位。
- SamplerManager 仅复用同名同配置对象；不同配置不覆盖旧对象，各向异性使用精确值作缓存身份。

### 材质 Shader 热发布

MaterialShaders 是完整顶点／片元程序的具名集合；未知名称、空集合和不完整程序在 GPU 创建前拒绝。
MaterialShader 复用 ShaderInterface 校验；未参与更新的程序保留原版本。MaterialPrograms 保存成功的内置覆盖字节码和项目程序版本，供目标重建使用。
生产目录和 include 约定见 [Shader 开发](../../README.md#shader-开发)。

Worker 只编译请求副本，不访问 Editor、Scene、Device；服务销毁后 CPU 工作可结束，但不会再发布。
每组一个在途任务和一个合并的最新请求，共用 TaskScheduler 背压。每批处理完整阶段集合，
消费时复核 revision、全部输入及缺失 include；失败结果也作为后续监视基线。当前只监视已登记材质程序。

Editor 在活动帧之外发起发布：固定 Frame/Object 契约保持不变，仅 MaterialSet 1 可重绑定布局。
MaterialRenderer 准备完整 PipelineState、CPU 缓存和驻留 GPU 材质候选；全部成功后统一切换。
项目程序先在场景 pass 录制前按本帧引用去重准备，MaterialRenderer 的逐物体绘制只消费已选 Pipeline；候选失败保留旧版。保存成功字节码用于目标重建，关闭编辑器不持久保存开发覆盖。DebugRenderer 只使用内嵌程序。

只换 Pipeline 而材质数据不变时复用参数 buffer／pool／set，不修改在途帧持有的旧包装。
缓存判等同时使用 PreparedMaterial 与 PipelineState；旧版回退限定同 Handle／运行实例及当前目标兼容域。
Shader 候选需同时通过驻留实体覆盖的布局校验；字段删除／改型不兼容时拒绝整批候选，保留旧画面并报告错误。
项目程序准备前按当前提交同步材质输入并淘汰已失效的运行实例。覆盖不兼容失败随覆盖快照变化／移除解除；
材质准备失败随该程序引用的材质集合、源对象或 revision 变化解除，重试读取当前源，不复用失败时的旧源。
输入不变时不重试；Shader 自身错误不因材质或覆盖参数动画反复重试。缓存移除不影响在途帧保活。
材质资产换模板导致旧覆盖不兼容时同样保留旧完整版本；覆盖不会自动猜测映射到新字段，Stop 后使用新资产基线。
ReloadReport 区分候选准备、CPU 打包、GPU 创建耗时；不设置固定性能倍数断言。
大量布局重建仍同步占用 owner，CPU 后台化不代表 GPU 创建没有主线程成本。

Shader GPU 发布和离屏 resize 仅对 Vulkan 主机／设备内存不足采用 1、2、4 秒退避，最多三次。
新输入／新尺寸重置身份与预算；重试前复核输入，不保留旧 RenderPass 的半成品。
RetryBackoff 只管理期限和次数，错误策略由各消费者决定，不建立全局重试服务。
resize 失败保持实际 Target 尺寸，纹理、viewport 与拾取始终使用实际目标；DeviceLost 退出。

#### 目标与缓存身份

完整 RenderState 在私有候选中创建，全部成功才安装；FrameSlot 保留版本及实际录制的 Target。
完整切换位于活动帧外，纯尺寸变化仅更换 MultiTarget，可在场景 pass 前安装，不重建材质管线。
ImGui 重建失败先关闭已初始化后端；WSI 有界重试与 Application 关闭准备不能被 fatal 包装替代。
当前 Vulkan 后端 Shutdown 也清除平台数据，因此格式／image count 重建同时重建 GLFW 后端，保留 Context/UI 状态。
共享 ImGuiContext 以明确 Options 区分 Editor 的 Clear 与 App 的 Preserve；后者要求场景已写入 Present 图像，
非空 UI 以 Load 合成，空 UI 不录制 pass。颜色 Load 具有前一颜色写入到读写的依赖，不能只依赖 acquire 等待。
App 的线性 HDR surface 使用 UI 专用片元阶段解码字体／纯色的 sRGB 值，保留 alpha 和背景 HDR 范围；
这不代表任意外部纹理的色彩空间处理、Editor HDR 或 HDR10/PQ 已完成。字体、布局路径和 docking 由宿主显式选择。

PipelineKey 包含字节码、入口、布局、规范化配置、RenderPass 身份和附件格式／采样数，
hash 不替代完整相等比较。动态状态无关值会规范化，同一副本用于实际创建。
Key 属于 Device／RenderPass 域，不是持久格式；过期弱引用在创建请求或显式回收时移除。
模板选择、Pipeline 对象缓存与驱动 PipelineCache 是不同职责。

### 驱动 PipelineCache 持久化

Device 独占 `graphics/pipeline/pipeline_cache`，Pipeline 和 ImGui 只借用原生缓存句柄。
Application::Options 接收可选缓存根目录，在 run 中传入 Config::Vulkan；Editor 从实际 ProjectPaths 取目录，
app 从 demo 项目取目录。RenderContext 在创建 Device 后、创建渲染资源前调用 restore。
图形层只接收目录，不依赖 Project、AssetDatabase 或 UI；空目录不读写磁盘，直接传 Config 的调用者也可指定目录。

Device 已有的空内存缓存是回退对象；有效磁盘数据创建临时候选并合并到它，不替换消费者借用的缓存句柄。
缺失、损坏或不兼容文件不阻断启动；驱动拒绝候选时保留内存缓存，OOM／DeviceLost 及合并失败返回 GraphicsError。
新读取、校验、合并与保存路径没有 try/catch/throw；Device 原有逻辑设备和初始空缓存创建边界未在本项扩展迁移。

文件名包含 vendorID、deviceID 和 pipelineCacheUUID。Comet 封装为固定小端 32 字节头：magic、版本、头长度、
payload 长度和 FNV-1a 校验和；payload 再校验 Vulkan v1 头及设备身份。先限制文件大小再分配，驱动数据上限 64 MiB。
校验和仅检测意外损坏，不是认证；缓存不能作为不可信远端数据的安全隔离措施。

正常关闭在原生 Device 销毁前保存；也可由 owner 显式调用 save，不在每帧写盘。
取数据最多处理三轮 INCOMPLETE，再复用公共原子文件写入；失败返回 Result 并保留原目标，析构只报告错误后继续释放缓存。
没有异常兜底，不承诺内存耗尽等未预期异常下仍能有序关闭。
多进程采用最后一次完整写入，不合并文件或加跨进程锁；崩溃可能丢失本次新增缓存，不影响资源正确性。
Restored 仅表示驱动接收并合并了兼容数据，不证明内部命中或固定性能收益。测试覆盖 CPU 格式、真实绘制、失败保存与跨进程恢复；
驱动拒绝候选和 GPU 内存失败尚无故障注入，历史 UUID 文件清理与目录总预算留待实际规模需要。

## 编辑命令与视口时序

帧顺序见「一帧经过哪里」。Renderer 消费 owned RenderScene，不持有可变 Scene/EnTT；
组件编辑、Undo/Redo 和拾取使用同一份编辑后快照。

| 入口 | 边界 |
| --- | --- |
| InspectorPanel | 选择分发、实体／场景属性、脚本定义选择；不持有材质或纹理草稿 |
| AssetInspector | 材质／纹理草稿、模板确认、读取与编辑请求；无 CommandHistory、Scene 或 Renderer 依赖 |
| SceneEditor | 场景安装后的编辑状态重绑；模式／代际检查、实体结构、脚本绑定、引用赋值及选择更新 |
| SceneCommands / CommandHistory | 具体逆操作与历史游标；不触发文件或 GPU 操作 |
| SceneDocument / EditorSceneSession | 保存点／文档操作，以及 Play 副本／恢复 |
| project/asset_operations | 协调场景资产移动与文档、启动场景、Session 路径；底层源文件事务仍由 EditorAssets 执行 |

Editor 调度跨面板请求，结束活动手势并安装场景，编辑状态重绑交给 SceneEditor。Inspector 与 Gizmo 各自持有 PropertyEditTransaction，
共享 CommandHistory；环境／后处理也走 begin／preview／commit／cancel。拖动预览，结束后只提交一次；
离散 apply 先结束旧手势，失败取消新事务。结束活动手势失败会拒绝后续请求。
资产请求携带 Handle/revision；AssetInspector 切换选择后清空旧草稿与请求，过期完成结果不覆盖当前选择。
EditorAssets 执行读取；material_editing 负责模板迁移、校验与完整提交，宿主负责文件／GPU 操作。
Play 组件调试不进入 Edit 历史，资产文件编辑也不混入场景历史。渲染生命周期保持同步回调，不改成事件总线。
MenuBar 一次交付带路径的 Request，宿主仲裁消费；不再分别提取命令和路径。
资产报告统一由宿主刷新 Project 与启动场景列表，面板的 complete 只处理对应对话框，不重复刷新索引视图。

编辑器移动 Scene 资产后同步当前文档、项目启动路径和已记录的 Session 路径，不重新加载 Scene 或修改保存点／Undo。
项目或 Session 保存失败会移回源文件并补偿已保存的项目设置；补偿自身失败明确报告并重新扫描，文档跟随实际索引位置。
这是可失败的补偿流程，不承诺跨文件崩溃原子性；外部 Finder 移动不自动改写 project.json。
当前文档与启动场景拒绝删除，需先打开其他文档或选择其他启动场景。普通资产继续使用系统回收站，不新增资产撤销系统。

UI 共用能力留在 `editor/src/ui`：`dialogs` 管确认选择，`widgets::input_text` 管 ImGui 与可增长字符串之间的适配。
实体名称、资产路径、项目名和属性值仍分别校验；共用控件不意味着合并它们的业务操作或保存策略。

结构命令保存完整组件快照，未知或不可恢复的组件会阻止破坏性操作；
撤销恢复 UUID 与父子关系，不恢复旧 EntityId、选择或展开状态。
复制只重映射已有层级协议中的内部 UUID；自定义组件实体引用须另外定义重映射协议。

LineDrawList 只保存世界空间端点与颜色，Renderer 在场景 pass 录制前接受多次追加并持有副本。
通常在 update/prepare 提交；本帧拾取的结果回调也可提交，因此点击产生的选择反馈不必等下一帧。
render_frame 消费后清空，准备失败、隐藏视口或无合法相机时丢弃，不跨帧保留。
DebugRenderer 使用场景的相机矩阵、RenderPass 格式和 MSAA；LineList、深度测试 LessEqual、不写深度、alpha 混合。
每 slot 独立的持久映射 CPU-to-GPU vertex buffer，只在等待当前 slot 完成后写入或扩容；
绘制使用的 buffer/Pipeline 同时被 FrameSlot 保留至 GPU 完成。扩容失败保留旧 buffer 并跳过本批，延后重试。
它不持有 Scene、Selection 或 ImGui；选中框等调用方自行转换成世界空间请求。

Viewport 在 UI 编辑命令完成后读取选中实体的 Mesh local bounds 和最新 world matrix，
用 LineDrawList::add_box(box, transform, color) 变换八角点并连接十二条边，不重新拟合世界 AABB。
普通帧在 prepare 提交；有视口拾取请求时，等结果更新 Selection 后再提交，避免旧框和新框同时出现。
选择状态仍由 SelectionService 持有，Scene/Mesh/Material 不保存 selected 标记；Play、隐藏视口或无有效 Mesh 时不提交。

Viewport 拥有 ViewportPanel 和 TransformGizmo，借用 EditorState、Selection、Renderer、AssetRegistry 和 ImGuiContext；
每次更新显式接收当前 Scene，不另存活动场景指针。Editor 负责挂接和解除帧回调、场景重绑以及跨面板命令。
TransformGizmo 是编辑器侧的投影、命中与平移／旋转／缩放事务，不是渲染资源。它与 Inspector 各自持有 PropertyEditTransaction，
共享同一个 CommandHistory；拖动用 UUID 定位，按模式预览 translation、rotation 或 scale，释放提交一次，取消恢复。
ViewportPanel 优先将普通左键交给 Gizmo，未命中才请求场景拾取；拖动时占有 ImGui active ID，阻止快捷键和相机导航。
UI 回调完成命令／相机更新后，ViewportPanel::draw_gizmo 将最新句柄追加到本帧窗口 draw list，随后 ImGui::Render。
箭头和旋转环作为可操作的 UI 覆盖层不受场景深度遮挡；显示与命中共用线段集合，不需要修改 DebugRenderer 或向 engine 注入编辑器状态。
点击拾取帧不显示旧选择的箭头，新选择箭头在下一 UI 帧出现；选中包围盒仍由拾取回调在当帧提交。

RenderView 的 CameraSelection 选择显式 editor camera 或 Scene primary camera；
请求 override 却缺少数据时不静默回退。没有合法 Camera 时清屏并保留 UI，不录制场景 draw。
RenderCamera 统一校验投影参数和 view 有限性，projection_matrix 同时供 SceneResolver、Gizmo 与放置计算使用。
它不选择活动相机，也不保存 GPU 状态；Runtime CameraComponent 的透视／正交配置从场景提取，Edit 相机仍独立覆盖。
geometry.h 中的 Comet::unproject_ray接收 inverse VP 与 NDC，返回 near/far 之间的归一化射线。
拾取先按实际纹理像素中心映射 NDC，保留远裁剪上限；Gizmo 使用连续逻辑坐标并放开射线上限，
允许拖出画面。两者共用计算，不共用输入坐标策略。
EditorCameraState 共享 target/clip/projection，独立保存 perspective position/up/FOV 与 orthographic height；
2D 固定 +Z 观察轴，平移同时移动共享 target 和 perspective position，切回 3D 不丢观察方向。

## 两种完成与资源发布

| 机制 | 保护范围 |
| --- | --- |
| FrameSlot fence + retained owners | 当前 graphics submission 使用的资源与 slot 可复用时点 |
| Queue timeline / GpuCompletionPoint | 上传等 submission 的完成身份 |
| present queue idle 回退 | 没有精确 present completion 时，旧交换链的呈现使用 |

slot 数 N 与 swapchain image 数 M 独立；image-available 属于 slot，render-finished 属于 image。
slot 循环索引不是永久 completion 身份；frame serial 用于帧身份和失败重试节流。
相机 UBO 只在对应 slot fence 完成后改写；材质参数与 descriptor 不原地改写，新版本替换缓存后，旧版本由在途 slot 保留。
retention 只保留真实资源 owner，不接受任意业务回调。

FrameScheduler::begin_frame 只取得已完成的 slot/image，录制期间不重置 fence、不登记 image 在途。
submit 负责 fence reset 和 Queue 提交；只有成功才登记 serial 与 image-slot 关联，不再由调用方单独 record_submission。
等待以成功提交的 serial 为依据，没有未完成提交就不等待 fence，避免 reset 后提交失败造成永久等待。
Presentation 检查提交结果，失败交给应用退出清理，不继续 present，也不自动复用已 acquire 的二进制 semaphore。

Queue::submit2 使用现有 GpuResourceResult 返回原生错误，成功才推进 timeline 值并给出 GpuCompletionPoint。
CommandContext 在结束录制前关闭本次提交机会，只有 Queue 成功才进入 Submitted，失败后不能追加录制或再次提交。
Mesh/Texture 静态工厂先创建完整 GPU owner，再通过 UploadBatch 提交 copy/barrier，检查成功并保存 ready completion 后返回，
不进行 CPU wait。SceneRenderer 按 VertexInput/FragmentShader 汇总实际资源的 timeline wait。
UploadManager pending batch 保留 staging page、CommandContext 与目标 owner，完成后才回收；
pending 容器在 Queue 提交前预留空间，成功后的所有权转移不再分配内存；staging 回收容器在初始化时按缓存上限预留。
staging 增长或 Queue 提交失败只 abort 自己尚未提交的 batch，不影响其他事务，也不发布 Mesh/Texture 候选。

VMA memory budget 只在扩展确实启用后使用；估算值不当作硬上限。
强失败创建与 GpuResourceResult 可恢复创建都不发布空句柄成功对象。
资产发布层决定失败保留旧对象，低层工厂只报告错误，不认识 AssetHandle。

ResourceState/ImageState 描述 stage/access/layout/subresource/queue owner，不保存在 Image 的单一 current_layout 中。
Barrier2 描述访问依赖，timeline 描述完成；跨 queue family 需配对 release/acquire 和 semaphore，不能只改 index。

## 有序 RenderGraph

RenderGraph 只收集 imported 资源、按顺序执行的 pass 和 exported usage；所有声明校验集中在
`compile() -> Result<Plan>`，不在 import/add_pass 时逐项传播错误。Plan 是不持有 GPU owner 的值快照，
不改变 pass 顺序，不分配资源，也不持有队列或全局图像 layout。ResourceId 仅在所属图内有效。
`add_pass()` 返回图内 PassId，录制回调收到同一 ID；调用方保存注册结果分发，不硬编码 pass 序号。
SceneRenderer 保存阴影与场景附件的 ResourceId，Bloom 保存 ping/pong 的 ResourceId。
每帧按 Plan::resource_count 分配绑定表，再按 ID 填写；资源声明顺序不再隐含在 append/emplace_back 中。

编译器按 image subresource 或 buffer offset/size 跟踪状态：保留实际 writer、已初始化内容和全部 reader scope，
处理 RAW/WAR/WAW 与布局转换；相同可见范围的重复读取不重复插入 barrier。
导出只建立外部消费者需要的可见性，不能凭空初始化内容或冒充新的 producer。
下一次提交可显式导入 `get_final_states()`；跨队列同步不由当前图实现。

`Plan::record` 先检查当前帧、设备、queue family、绑定类型、范围、图像 usage 和重叠别名，
再预建全部原生 barrier，随后保活绑定并按 barrier → pass callback → export 的顺序录制。
同一原生资源的非重叠范围可以分别声明；重叠范围必须合为一个声明。Buffer 创建 usage 仍由调用方保证，
不能检测不同句柄背后的内存别名。回调必须遵守声明，图不会解析实际 Vulkan 命令。
回调返回 GraphicsError 时立即停止并原样传播，已经录制的命令不回滚；上层必须退出该帧，不能提交部分结果。
回调同步执行且不保存，接收当前帧的 CommandBuffer&。SceneRenderer 的场景绘制集中在私有 draw_scene，
app/editor 均通过短回调选择场景或色调映射 pass，并收集场景返回的上传等待信息，不另设 Pass 类层次。

SceneRenderer 的 RenderState 保存编译后的有序 Plan，仅 Bloom 开关变化时重新编排。每帧绑定当前 slot 的 HDR 实际附件，
先转换到 attachment layout，再绘制材质和辅助线，将颜色／MSAA resolve 输出转为 SampledRead，供色调映射读取。
附件每次清除，slot 复用前已等待 GPU，因此允许从 Undefined 丢弃旧内容；resize 只替换实际绑定，
旧目标继续由在途帧保活。图管理阴影、HDR 附件和 Bloom ping-pong 的写读同步；最终输出 RenderPass 负责清除、
存储及 Present／ShaderReadOnly 转换，不在图中重复声明它的 layout。ImGui 和 WSI 提交仍在图外，
呈现 RenderPass 的 external dependency 对齐 acquire 等待阶段。

ImageInfo 支持显式 mip/layer 数量，但不自动生成 mip，也未新增完整数组纹理视图 API。
HostRead/HostWrite 只用于外部交接，不作为 GPU pass；CPU 读回仍必须等待 completion，并满足映射／缓存一致性要求。
Upload timeline、WSI semaphore 和资源初始化真实性仍是调用方契约。

测试覆盖 CPU 计划、四 pass 实际读回、mip/layer/buffer 区间、跨提交交接、绑定拒绝、回调失败、
MSAA 与离屏 resize。独立 `render_graph_sync_validation` CTest 开启同步校验，
以不提交的漏 barrier 命令作为负对照，确认校验层生效；不替代跨平台运行和人工视觉验收。

## HDR 与 SDR 输出

场景只保存外观数据，不持有 Pass 实例或执行顺序。物体可以参与阴影、主绘制等多个阶段；
材质／物体选择与屏幕空间处理分开，后者需要明确的颜色、深度或遮罩输入，由渲染管线编排资源与合成。
目前没有为 MeshRenderer 提供任意 Pass 列表或自定义后处理链。

场景颜色由 Config::Render::SCENE_COLOR_FORMAT 固定为 R16G16B16A16_SFLOAT，MSAA resolve 也保留 HDR。
RenderContext 把场景格式写入 DeviceCapabilityRequest；设备候选评估和场景创建复用
graphics 层的 validate_color_target，检查 attachment／blend／sampled、单采样 resolve 和场景 MSAA。
输出附件按实际选中的交换链格式单独检查 Count1，不把显示格式当成场景 MSAA 格式。
缺少场景能力的设备在候选阶段被拒绝；SceneRenderer 的复核失败仍返回 GraphicsError。
不静默退回 8 位场景颜色，也不自动更换场景格式。
这不是可独立关闭的 HDR 特效：主绘制就发生在浮点目标中，随后 OutputPass 完成显示映射。
相对 8 位直接输出，它增加颜色存储／带宽与全屏输出成本；不因 Bloom 关闭就自动消除。
是否增加轻量输出路径应基于实际 GPU 测量，并一起处理材质、高亮、MSAA 和编辑器离屏语义。

清屏颜色来自本帧 `RenderSubmission.environment.background_color`，不在 SceneRenderer 缓存配置副本。
它是 SceneEnvironment 的线性 RGB 值，有限范围 0..65504，默认黑色；清屏 alpha 固定为 1。
天空盒覆盖它；天空盒禁用或资源未就绪时作为背景。设置在录制开始前写入 RenderTarget，
已经录制的命令保留各自清屏值，因此不修改在途帧，也不需要重建目标／管线。

OutputPass 位于 `render/passes/`，由 SceneRenderer::RenderState 持有，是具体的最终输出步骤，
不是与 SceneRenderer 并列的渲染子系统，也不是底层 Vulkan RenderPass 的别名；不引入通用 Pass 基类。
OutputPass 只拥有固定输出 RenderPass、fullscreen Pipeline、sampler、descriptor layout
和每 slot 的输入 Binding，不拥有 Scene、Window 或 FrameScheduler。
创建、绑定准备和录制通过 Result 返回失败；旧 Binding 不原地修改，替换后由在途帧保留。
录制保留实际 Binding、输出目标及其 GPU 依赖，即使绘制器先销毁，已录制资源仍存活到帧完成。

app 输出到 SwapchainTarget，editor 输出到 SDR MultiTarget；对外 get_render_target 和
get_offscreen_color_view 仍代表最终显示目标，不暴露中间 HDR。
replace_targets 先准备 HDR 和输出目标，再准备启用中的 Bloom 双目标，全部成功才同时替换。普通离屏 resize 保留现有重试预算；
失败时不发布半套尺寸，旧帧保留实际使用的目标。运行时 WSI 重建同样重建整套目标，
但不改变交换链退休后不可回滚的原有规则。
输入 Binding 最多按 slot 保留旧 HDR view，直到该 slot 换用新 view 或绘制器销毁。

fullscreen triangle 不需要顶点缓冲，正高度 viewport 保持纹理方向。
色调映射为 H * (1 - exp(-max(color, 0) * exposure / H))，SDR 的 H=1，HDR 的 H=render.hdr_headroom；
H 表示相对白色的输出峰值（1..16，默认 4），不是显示器查询结果；曝光来自 PostProcessSettings，缺省为 1。
场景数据与渲染输入共用 PostProcessSettings::validate，拒绝非有限数和越界参数。sRGB 附件由硬件编码，UNORM 附件由 Shader 执行分段 sRGB 编码。
扩展线性 HDR 输出必须是 RGBA16F + ExtendedSrgbLinearEXT，不做 gamma 编码，不再将高亮压进 0..1。
白色基准 1 由系统合成器解释，不假定跨平台固定 nits；实际显示亮度仍由系统和屏幕决定。
不支持 HDR10/PQ、自动曝光或动态后处理节点。
世界空间辅助线与场景一起经过映射，ImGui 不经过场景色调映射。

render.output_mode 默认 sdr；hdr / auto 在 surface 枚举中优先选择上述 HDR 格式与颜色空间组合，
未提供时回退原配置的 SDR 组合并报告原因；不会挑选仅格式相同或仅颜色空间相同的条目。
Context 可选启用 VK_EXT_swapchain_colorspace，未提供扩展时仍可启动 SDR。
首次成功创建后 Swapchain 固定输出组合，resize / surface 恢复不重新切换模式；固定组合消失则按原有 Result 失败路径退出。
select_swapchain 通过独立的可选 fixed_output 接收该组合，优先严格校验；不修改请求的 OutputMode，
也不再使用 Sdr 表示“跳过自动选择”。
这是启动策略，不支持拖动跨屏或系统 HDR 热切换后的重新适配。auto 与 hdr 当前采用同一能力选择策略，日志保留不同请求值。
Editor 在 Application 启动前通过构造参数固定 SDR；共享 YAML 无法将编辑器换成 HDR。
SceneRenderer 的离屏输出独立使用配置的 SDR 格式，呈现输出使用交换链实际格式和颜色空间。

GPU 像素测试覆盖 RGBA/BGRA、sRGB/UNORM、曝光 1/0.25/0、高亮和暗部、上下方向及 alpha；
还覆盖浮点 HDR 的 H=1/4/16、大于 1 的像素和无 gamma 编码，以及三种启动模式的真实呈现和重建。
生产场景覆盖 MSAA 1/4、resize 和旧输出的在途保活，并验证输出绘制器提前销毁后的帧资源寿命。
双目标第二次分配的 OOM 尚无专项故障注入，不能把尺寸拒绝测试当成该失败路径已验证。

### Bloom

BloomPass 只拥有高亮提取与两遍模糊的 Pipeline、RenderPass、双目标和不可变采样绑定，不负责最终显示。
`append_passes` 返回当前图的三个 PassId 及两个 ResourceId；SceneRenderer 编排，RenderGraph 产生 ping/pong 的写读／读写屏障。
半分辨率按上取整计算，提取先对各有效源像素按 RGB 最大分量扣除阈值再平均，奇数边界不重复采样。
模糊使用归一化九 tap 二项核；display 手动双线性上采样，不增加浮点格式线性过滤的能力要求。
OutputPass 先合成 `HDR + bloom_strength * bloom`，限制到 half-float 有限范围，再曝光和显示映射。
提取／模糊的 push ABI 为 8 字节，display 为 16 字节，CPU static_assert 与 Shader 反射测试核对。

scene/scene_settings 定义 Scene 持有的环境与后处理值类型及合法性校验；不包含 GPU 对象或 Pass。
PostProcessSettings 的曝光为 0..100，独立泛光开关、强度 0..10、阈值 0..65504；UI 复用这些边界。
SceneSerializer 保存至 `.scene/post_process`；缺省使用曝光 1、泛光关闭，显式对象必须包含完整字段并通过校验。
Inspector 的场景后处理复用 PropertyEditTransaction 和 CommandHistory，提供实时预览、撤销、取消和保存；Play 克隆继承参数，面板只读。
环境与后处理通过类型化 SceneTarget（getter/setter）接入统一事务；事务不列举具体场景值类型。
提交捕获校验／归一化后的实际值并记录已应用命令，不先恢复旧值再重放；Undo/Redo 只修改场景数据，不操作 GPU。
数据沿 Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission 传递；app 与编辑器相机使用同一路径。
Renderer 先调用 prepare_post_process，再调用 SceneRenderer::render 录制图；缓存仅代表已准备的状态，不是另一份可写配置。
仅是否执行泛光变化时重编译图，其他变化只影响 push constant。OOM 不发布候选，仍用旧设置完成本帧；
依次等待 1、2、4 秒，最多重试三次，耗尽后等取消请求或目标尺寸变化。普通数值拖动不刷新资源重试预算。
取消开启请求立即清除重试。Scene 的用户意图不回滚；设备丢失、非法输入和非内存创建错误继续向上传递。
开始场景命令录制后的失败仍按原路径停止提交并关闭，不能套用准备阶段的降级规则。
开关关闭或强度为 0 时不声明、绑定或录制 Bloom pass，OutputPass 传入的合成强度为 0；关闭开关不会清空场景里的强度和阈值。
首次开启才创建资源，关闭后缓存最近目标供再次开启复用。
resize 先创建两个局部候选，再一次性替换；FrameSlot 保留真正使用的目标、Binding、Pipeline 与 RenderPass。
两张 RGBA16F 纹理每 slot 约占 `2 * ceil(W/2) * ceil(H/2) * 8` 字节，另计对齐与在途旧版本。
测试复用 `tests/support/render_gpu_test.h` 的设备、读回与校验日志夹具；不为测试增加引擎协议。
覆盖独立 CPU 像素参考、极小／奇数尺寸、SDR/HDR、关闭／阈值／曝光极值、MSAA、在途参数切换、resize 与 pass 提前销毁，
以及场景保存重开、Play 克隆、UI 手势历史和真实引擎循环内的场景替换。
独立 post_process_recovery 目标用测试工厂模拟 OOM／DeviceLost，真实执行 Renderer 与 SceneRenderer，验证继续提交、有限重试和错误分类。
这不等同于真实驱动显存耗尽或 Bloom 第二张目标创建失败；这些底层分配注入仍未覆盖。
暂不含多级金字塔、soft knee、镜头污渍和自动曝光。

## Swapchain 与关闭

交换链重建：等待所有 graphics slot 和 present queue → 释放 runtime/ImGui dependent →
创建 Generation → 重建 per-image state 与 dependent。Editor 离屏 MultiTarget 不因此重建。
extent 变化只重建 target；format/image count 变化还会影响 ImGui backend。
初始 RenderPass 使用实际选定的 surface format，runtime 不兼容格式目前明确终止。

Generation 的 shared ownership 只解决寿命，不保证 WSI 可继续 acquire：
传入 oldSwapchain 调用创建后，无论成功失败旧 core 都退休。调用前取走 active 引用，旧 owner 仅保活至创建调用结束，绝不再发布为 active。
新 Generation 用 UniqueSwapchainKHR 持有句柄，图像查询失败或包装异常都会释放新句柄。
Presentation 区分交换链重建、dependent 重建与 surface 重建阶段；任一阶段未完成时不 acquire、不录制。
恢复阶段的内存不足、OutOfDate、SurfaceLost、Timeout／NotReady／Incomplete 按 1／2／4 秒最多重试三次；
surface format／present mode 枚举单次最多四轮，持续 INCOMPLETE 返回恢复层，不能在 owner 线程无限循环。
dependent 暂时失败保留已成功创建的新 Generation，下次仅重建 dependent，重复释放必须兼容部分初始化状态。
零尺寸延期保持待重建状态；无呈现时主循环继续更新，并通过短时事件等待避免忙等。
SurfaceLost 在等待 graphics／present、释放 dependent 后重建 surface，并检查原呈现队列是否支持新 surface。
Generation 同时保活自己的 surface，旧代外部引用不能使 surface 提前释放；实例仍须晚于全部代销毁。
设备丢失、不支持的配置及重试耗尽以 Result 错误传过 Renderer／Engine，由 Application 统一进入退出清理。
request_swapchain_recreation 只登记请求并重置手动重试预算；begin_frame 是唯一推进恢复的入口。
acquire／present 的自动重建请求不重置预算；提交末尾不直接重建，避免一次调用隐含多个恢复入口。
帧准备／录制结果沿用[一帧经过哪里](#一帧经过哪里)的 Ready／Deferred／错误协议。
这不承诺全部底层录制／等待接口 noexcept；未迁移的第三方异常仍可能导致进程终止，不保证有序清理。
Context 对外只提供借用 Surface 句柄，Surface owner 仅在 Context／Swapchain 内部共享。
首次创建与恢复共用私有 Surface 候选创建函数，恢复路径额外校验当前呈现队列，再安装候选。

独立 swapchain_recovery 测试重编译真实 WSI 消费者，只在测试进程重命名 Vulkan 入口，不增加生产故障注入协议。
覆盖真实旧代退休、创建／枚举失败、OutOfDate、无 active 时关闭、有限重试与恢复后的提交，含离屏 owner 保持。
SurfaceLost 通过 dependent 错误驱动，不模拟平台真实丢失窗口，也不替代 ImGui 人工验收。

关闭先由 Engine 调用 TaskScheduler::shutdown 停止接收、排空任务并回收线程，再由 Renderer 停止新帧并等待 GPU；随后应用解绑捕获 Editor/ImGuiContext 的 callback 并释放资源。shutdown 由 owner 线程调用，不可从 Worker 调用，也不支持多个线程同时关闭；wait_idle 只等待瞬时空闲，不承担关闭职责。Engine 不直接访问 Device。独立底层 owner 的安全析构等待仍保留。资源释放顺序为：
ImGui dependent → Registry/SceneRenderer → RenderResources → Swapchain/Device/Context → Window。
Device 必须比 Buffer、Image、Mesh、Texture、completion token 活得更久；shutdown 允许 Device idle。
Swapchain::create 返回完整候选；recreate 的 Deferred 表示尚未调用原生创建、旧代未退休。
acquire 返回 Result<optional<uint32_t>, GraphicsError>：空索引表示 OutOfDate，成功／Suboptimal 才包含有效索引。
present 返回 Presented 或 RecreateRequired；负向错误保留 GraphicsError。Presentation 按错误码决定恢复阶段，重复 OutOfDate 不开始帧录制。
Device::wait_idle_for_shutdown 检查 Vulkan 原生等待返回码并报告；Engine／Renderer／RenderContext／ImGui 和上传析构复用此边界继续释放资源。
上传管理器仅在还有在途批次时做关闭等待，不再在析构中逐个等待可能因设备丢失失败的 completion；正常运行时的等待接口不变。

GLFW 由 Window 实现管理：首个窗口初始化，最后一个窗口释放后终止，创建／销毁在主线程执行。
原生窗口由 unique_ptr 与私有 deleter 持有，构造过程中取得窗口后发生异常也会释放。
Engine 不直接初始化 GLFW；普通调用方使用 `request_close()` / `is_minimized()`。
`Window::get()` 仅借出句柄给 Vulkan Surface、ImGui 后端和底层测试，不转移所有权；
调用者不得自行销毁句柄或在 Comet 窗口存活时终止 GLFW。独立的非 Comet 窗口生命周期暂不纳入管理。
GLFW 使用共享库，避免 Engine 动态库与 ImGui／测试各自静态链接一份全局状态；PRIVATE 链接仅控制接口传播，不代替这一运行时约束。

## Viewport 和拾取边界

ViewportPanel 采样 UI、维护 resize debounce 和一次性请求；ViewportLayout 计算逻辑尺寸、物理尺寸、display/visible rect。
实际纹理像素映射采用左上闭、右下开，排除工具栏、留白和 1x 裁切；debounce 中不使用尚未发布的尺寸。
上限取设备 maxImageDimension2D 与 editor 4096 软上限较小值，等比约束。
camera_controller 只做纯数学，不依赖 ImGui。

Mesh 在 GPU 创建前验证顶点并计算只读 local BoundingBox，不保留整份 CPU geometry。
CPU pick 反投影 near/far 射线，变换到局部后测包围盒，方向不再次归一化，保证非均匀缩放下距离参数可比较。
尺寸不符/隐藏丢弃请求，普通 miss 清空 Selection。F 聚焦由 Editor 按事件取最新 world bounds 后调整相机，
不改实体或 Scene Camera。当前没有 GPU readback 或三角形级选中轮廓。
