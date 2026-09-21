# 渲染资源所有权

描述当前 owner、调用边界和销毁规则；未来项目 Shader／多 pass 扩展／RenderThread 设计见[路线图](../engine-roadmap.md)。

## 先看哪个类

| 入口 | 职责 |
| --- | --- |
| `core/engine.h` | 组合宿主服务，统一 SceneRuntime 的绑定、启停与主循环 |
| `scene/scene_runtime.h` | 拥有串行 System，管理时间、固定步、输入消费、暂停与单步 |
| `scene/systems/script_system.h` | Lua 行为实例的启动、阶段更新、寿命复核与逆序清理；字段仍属于 Scene 组件 |
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
| `editor/src/ui/imgui_context.h` | 编辑器 UI 最终呈现和私有纹理绑定，不属于 engine |

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
├── Scene（只有组件与 AssetHandle）
├── SceneRuntime → System[]（活动时借用 Scene，停止时逆序退出）
├── TaskScheduler
├── AssetRegistry → Runtime Mesh / Texture / Material / Environment
└── Renderer
    ├── RenderContext → Context / Device / Swapchain
    │                    Device → Allocator / queues / PipelineCache
    │                    Swapchain → active Generation
    ├── RenderResources → UploadManager / SamplerManager
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
├── EditorAssets → AssetManager（借用 Engine 的服务）
├── RenderStatsPanel（只读 Engine/Renderer 快照，提交一次性采样／报告请求）
├── EditorState / SceneDocument / EditorSceneSession / SelectionService
├── CommandHistory ← Inspector / TransformGizmo 各自的属性事务
├── Viewport → ViewportPanel / TransformGizmo（借用状态、选择、Renderer、Registry 和 ImGuiContext）
└── ImGuiContext
    ├── RenderPass / SwapchainTarget / DescriptorPool
    └── TextureBinding[slot] → ImageView / Sampler / ImGui descriptor
```

- 引用表示必需且不可重绑定的借用；指针用于可空、可换 owner 或 moved-from 状态。
  unique_ptr 独占，shared_ptr 延长共享寿命；原生 Vulkan/GLFW handle 仍遵守各自协议。
- Renderer 是组合根，不是所有 GPU 对象的直接 owner；Device 也不反向拥有业务服务。
- app/editor 的 AssetManager 先于 Engine 销毁；后台任务先结束，GPU 使用完成后再释放 Registry 和渲染资源。

## 应用启动与失败清理

Application::run(Config) 完整执行：创建 Diagnostics／Engine → on_init → 引擎循环 → 私有 end。
Engine::create → Renderer::create → RenderContext::create 在局部准备 owner，全部成功才返回完整对象。
宿主以 `Config::Render::SceneOutput` 选择初始目标：app 直接呈现，Editor 离屏后由 ImGui 呈现，只创建一组场景资源。
Application::Options 提供具名宿主选项：缓存／日志目录，以及可选的输出模式／场景目标覆盖。
未指定覆盖时保留 run(Config) 的值；Editor 显式要求 SDR 和 Offscreen。
scene_output 不从 YAML 读取，也不代表 HDR／SDR 颜色模式。

- Engine 创建失败：释放 Diagnostics，不调用应用钩子，允许重试启动。
- on_init 一旦开始：预期失败沿 Result 返回，end 先做 Engine 关闭准备，再且仅一次调用 on_shutdown。
  钩子需兼容部分初始化；关闭失败保留 Engine／Diagnostics，派生类资源先析构，且实例不能重启。
- 原始错误优先返回，清理错误单独报告。Error 保存消息和 std::error_code；图形边界通过 as_error 保留类别和数值。
  Comet::run 转换为退出码；YAML 解析异常仅在 ConfigLoader 适配。
- 底层不变量、第三方及标准库未预期异常不由 run／end／launch 捕获，不保证异常路径执行应用关闭钩子。
  LOG_FATAL 执行 assert／terminate、不展开栈，只用于明确终止的内部错误。

ImGuiContext 的 unique_ptr／私有 deleter 管理原生 Context，create 只发布完整候选。
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
Engine 持有 Runtime 和 Scene，统一绑定与启停；App 和 EditorSceneSession 只请求启动 Engine 的当前 Scene。
EditorSceneSession 管 Play 副本和失败回滚；Stop 走场景替换，由 Engine 先停止 System 再恢复 Edit，不重复管理调度器。
输入授权每帧清空：App 交付窗口快照，Editor 交付当帧 UI Gate 结果；未授权时释放按钮，不代替 Runtime 的暂停状态。
暂停只停止 System 的时间推进，宿主维护、UI、场景提取与绘制继续；单步执行一次固定更新和普通更新，然后保持暂停。
面板只读 Runtime 状态，控制请求由 Editor 在下一次宿主更新经 Engine 应用；暂停／单步不克隆场景，不触发 System 重启。
PlayCommand 属于 editor_state 的工作流协议，ViewportPanel 只生产请求；它不拥有运行时控制权。
时间截断仅由 SceneRuntime 的 max_frame_delta 决定，CameraControllerSystem 消费完整 delta，不另作 0.1 秒截断。
隐藏离屏视图仍运行 UI、Runtime、Scene 提取与上传回收；再显示时准备最新场景设置。直接呈现不走隐藏跳过分支。
prepare_frame 暂时无可呈现帧时跳过 UI／提取／绘制，仍执行 Runtime；最小化继续等待并重置墙钟增量。
最小化同时清除窗口瞬态与 Runtime 待处理按下；采样中断版本使 Gate 先释放再获取，不要求 UI 消费恢复首帧。
System 更新失败逆序停止并返回 Result；Engine 随后进入关闭清理，不继续使用已 acquire 的帧。
替换 Scene 必须发生在 System 执行之外，先停止旧 Runtime；shutdown 在宿主、Scene 和服务释放前停止并销毁 System。
Script 资产保存 Lua 源码与默认参数；ScriptComponent 属于 Scene，只保存资产引用和参数覆盖。
ScriptSystem 借用 AssetRegistry，持有 Script 资产和独立 VM 实例，不持有组件地址；Lua API 留在 scripting/script.cpp。
启动批次按实体 UUID 排序，停止按实际启动顺序逆序，不把动态新增后的排序当作启动顺序。
阶段边界同步宿主对实体／组件的增删；第一版 Lua API 不开放结构增删。每次调用读取最新参数，运行中保留启动时源码。
on_stop 只清理自有资源，不访问已删除实体或重入 Runtime；字段编辑不重启，组件删除再添加才产生新生命周期。
文件扫描、复制、保存和同步资产加载在 on_update 执行，不占用已 acquire 的帧；这不是将全部 I/O 移出主线程。请求仍由唯一 Editor 执行，不新增事件总线。Window 可选择拦截原生关闭事件，Editor 处理未保存决策后才通过 request_close 确认退出。
Scene 维护 EntityId／UUID 查询索引与父子索引，结构修改时同步维护；这些索引不参与序列化。
SceneExtractor 与 CameraControllerSystem 共用 Scene 的类型化 each 查询；渲染提取不再是 Scene 的 friend，也不直接访问 EnTT registry。
Scene 的同步检查遍历全部节点，比较本地 TRS、组件是否存在、parent ID 和父级计算版本，仅重算变化节点。
单个 get_world_matrix 只检查祖先链；持续持有可变组件引用的写入同样在下次查询／提取时生效。
缓存属于 Scene 私有状态，不序列化；update_world_transforms 返回实际重算数量，静止场景为零。
Engine 同步借用 update 和 frame_ready 两个函数，不保存宿主回调注册表；UI 与 System 修改后再同步变换并提取，避免使用上一帧数据。Renderer 不再调用 UI 准备，ImGuiContext 不再持有 UI 业务回调；Editor 在 on_frame_ready 显式调用 begin_frame/end_frame。
pose_world_matrix 使用层级旋转与普通世界矩阵的位置，本地及祖先缩放不进入相机朝向。
本地 TR 只计算一次，普通矩阵在其基础上应用 scale；相机与物体继续使用各自的父级矩阵。
SceneRenderer 不读 EditorMode/ImGui，不拥有 FrameScheduler，不访问呈现队列；录制时借用传入的帧上下文。

MaterialRenderer::render、DebugRenderer::render 与 SceneRenderer::render 返回 GraphicsError。
材质准备/调试缓冲增长遇到 DeviceLost 原样返回；普通失败仍沿用兼容旧材质或跳过调试批次。
Renderer 的帧准备、后处理准备、录制、提交或呈现返回不可恢复错误时，统一进入 prepare_shutdown，拒绝再次准备／绘制。
关闭后也拒绝目标切换、Shader 发布和材质候选创建；仍允许解除宿主回调。
prepare_shutdown 已执行设备等待，之后的 wait_idle 为幂等空操作；正常运行时仍禁止等待未提交的活动帧。
Engine 接收到帧错误后也进入关闭准备，停止 System 和后台任务；不依赖 Application 才完成终止。
prepare_frame 成功返回 false 仍表示可恢复的延期，不进入关闭；acquire 前的宿主更新错误不改变既有重入策略。
部分录制的命令缓冲只由 owner 销毁，不结束并提交空帧，也不重新用于下一帧。
Editor 通过 Renderer 注册 Overlay 重建钩子、读取只读帧信息；整帧命令缓冲直接传给 Overlay。
SceneResolver 只解析 Camera、Mesh、Material 和 Environment 引用，不检查模板、属性名或纹理数量。

## 渲染诊断

Engine 保存上一完整循环的 events/update/prepare/render-submit 墙钟分段；prepare 包含帧等待和 UI，
render-submit 包含提取、解析、录制与提交／呈现调用。暂缓呈现记录 rendered=false；错误中止不发布半条样本，
最小化等待不作为正常帧采样。该运行时开关独立于 scope Profiler 的编译开关。

Renderer 拥有 RenderDiagnostics，SceneRenderer 只在录制图时借用，不再次扩大场景资源所有权。
主循环使用 Engine::FrameTiming，图采样使用 RenderDiagnostics::GraphTiming：计量范围、序号和完成时刻不同，不合并成混合数据结构。
SceneRenderer::record_pass 负责具名 Pass 分发，局部 lambda 仅适配 RenderGraph 的同步回调，不保存或跨线程调度。
诊断包围既有 Plan::record：CPU 明细计量各回调，总时间还包含图校验与屏障录制；GPU 使用图首、各 pass 结束、
图尾导出屏障后的时间戳。相邻 GPU 边界包含依赖等待，不表示各 pass 独占硬件的时间。
场景图不含 ImGui overlay、present 完成或其他队列，CPU/GPU 快照分别带帧序号。
隐藏离屏视图将 scene_rendered 置 false，清除当前图样本与待发布的旧查询；不伪造零耗时图。
材质绘制／绑定等当帧活动计数归零；缓存材质数和 FrameSet 数按实际驻留状态查询，不因隐藏视口归零。
历史窗口自然老化，面板明确提示显示的是近期历史；CPU 整帧与显存采样不受影响。

每个 FrameSlot 懒创建固定 34 项的 GpuTimer，最多记录 32 个 pass、每个名称最多 128 字节。
超限仍执行完整图，截断 CPU 明细并跳过该图的 GPU 采样。保留槽位待完成样本、最新快照及有界时间统计。
Engine 与 RenderDiagnostics 共用纯 CPU 的 TimingHistory：100 个 50 ms 桶，逐样本累计 sum/count/max，
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
Environment 拥有 background、irradiance、specular、brdf 四个 Texture，复用纹理上传；材质 2D 槽拒绝 Environment。
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
app/editor 共用引用准备：场景环境是可选引用，缺失时保留 Handle 并诊断；app 拒绝必需引用失败，编辑器允许修复。DeviceLost 始终向上传递。
SceneResolver 不将 Registry 中尚未发布的环境当作错误，等待期间返回无环境纹理的提交；真实缺失／准备失败由资产层报告。
已发布对象不是 Environment 时，SceneResolver 报告类型错误并按 Handle 去重，不依赖 AssetManager 的调度状态。
ImportService 负责 CPU 导入与缓存，AssetManager 负责加载需求、revision 检查和运行时发布；二者不访问 ImGui。
后台首次准备与驻留重载共用缓存路径，输入路径／内容指纹和算法版本必须匹配；格式、尺寸、载荷长度和校验值不符则重建。
缓存原子写只保证单文件；缓存可独立存在，不代表 GPU 已发布。GPU 创建失败不替换 Registry，旧帧仍持有旧版本。
EnvironmentArtifact v2 将背景、最高 16² 漫反射、最高 128² 镜面 mip 链和 128² LUT 作为同一载荷校验；旧 v1 自动重建。GPU 四张纹理全部成功且 revision 仍有效后才替换 Registry，部分上传失败不发布半成品。
环境任务根据源尺寸估算工作集，默认共享 2 GiB CPU 预约预算；完成候选在发布或丢弃前不释放预约。
主线程仅做小型头部／文件大小预检和 GPU 发布，CPU 大块读取、转换、校验与缓存写入在 Worker；预检后源增长超预算会失败。
此预算不是进程 RSS 上限，也不覆盖普通纹理／Mesh 解码或 GPU 分配；GPU 创建仍使用现有资源工厂的预算及 Result。
显式同步 load_environment 保留给阻塞式工具调用；app/editor 场景需求不使用它。文件复制与其他资产首次加载仍可能阻塞主线程。


### 材质准备与寿命

内置 `unlit_color` / `pbr` 的初始 metadata 由 MaterialLayout::find_builtin 共享。
MaterialLayout::reflect 按 shader_name（为空时使用逻辑属性名）匹配已登记属性，重建 offset／块大小／binding；
名称、默认值、范围、步长和 Color/Vector 语义仍由 metadata 提供。参数块 binding 由布局指导创建和写入，不再固定为 0。
未知／缺失／改类型字段、多参数块与不支持的资源形状拒绝；相同物理布局复用原对象，不增加平行 revision 计数。
Editor 在初始化和成功热更后向 Inspector／Project 交付已发布布局快照；控件、模板候选和草稿校验使用同一布局，不清空草稿、不自动保存。
独立面板初始化使用 MaterialLayout::builtins；收到发布列表后不再补回未发布模板。目标重建后同值布局仍可用于 UI，不让面板引用 Renderer。
布局构造后不可变，以对象身份区分版本；Material 可修改，以自身 revision 标记真实变化。
MaterialLayout 保留 metadata 声明顺序，PreparedMaterial 单独按 binding 排序纹理绑定；热更物理 binding 不改变 Inspector 槽位顺序。
MaterialRuntimeCache 按 Handle 索引，比较 Material 对象身份／revision 和布局身份；失败也缓存，变化后才重试。
prepare／rebind 返回 Result 和具体诊断，不自行写日志；MaterialRenderer 负责去重报告与回退。
未使用项按渲染周期回收，已取得的 PreparedMaterial 仍拥有当时的 Texture 和参数副本。

set 0 是按 slot 更新的相机 FrameSet；set 1 是按不可变材质版本创建的 MaterialSet；model matrix 使用 push constant。
MaterialResources 持有 PreparedMaterial、PipelineState、Sampler、参数 buffer 与 descriptor pool；
FrameResources 持有 frame layout、pool 和相机 buffer。实际绘制的 FrameSlot 保留它们及 Mesh，直到 GPU 完成。
CPU 缓存淘汰不代表 GPU 已完成，不能据此删除 retained owners。
可恢复的 GPU 候选失败可沿用旧 MaterialResources，同一 PreparedMaterial／PipelineState 候选延后 60 个 frame serial 重试，
新候选可立即尝试；失败记录只弱引用 PipelineState，不延长旧 Pipeline 寿命。
DeviceLost 则交给应用退出清理边界，不把失效设备当作可继续渲染的旧版本。
CPU 准备失败与 GPU 创建失败共用回退判断，只保留同 Handle 且匹配当前 PipelineState 的旧版本；无旧版、不支持模板则跳过。
清除引用、移除物体或切换到其他 Handle 不回退到无关材质；回退项仍标记使用。
绘制周期结束时清理未使用缓存；无相机的绘制周期也执行这一步。隐藏视口保留有效缓存，避免恢复时全部重建。
Renderer 每次 prepare_frame 在 acquire 前检查 Registry，移除已注销材质的 CPU／GPU 缓存，隐藏／延期同样执行。
同 Handle 的新版本不触发这类淘汰，仍允许准备失败时回退旧兼容版本；在途帧保活不受缓存淘汰影响。
队列按模板名与材质 Handle 排序。

编辑器材质文件修改采用显式准备／提交，区别于上述绘制时的延迟准备：
AssetManager::prepare_material_update 保留源 revision、序列化内容及只读运行时候选，不改原材质文件或该材质的 Registry 条目。
Renderer 在无活动帧时接收候选，MaterialRenderer 在局部缓存打包参数并创建完整 GPU 绑定；失败丢弃候选。
editor/assets/material_editing 的 apply_material_edit 统一串联以上步骤：提交文件和 Registry，成功才发布 GPU 候选；
保存失败时两类候选均释放，Inspector 恢复旧模板和参数。Editor 只分发请求；EditorAssets 保留底层 prepare/commit，
纹理使用明确的 apply_texture_edit，不再有绕过 GPU 准备的通用材质提交分支。
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
MaterialShader 复用 ShaderInterface 校验；未参与更新的程序保留原版本，SceneRenderer 合并成功字节码供完整目标重建。
生产目录和 include 约定见 [Shader 开发](../../README.md#shader-开发)。

Worker 只编译请求副本，不访问 Editor、Scene、Device；服务销毁后 CPU 工作可结束，但不会再发布。
每组一个在途任务和一个合并的最新请求，共用 TaskScheduler 背压。每批处理完整阶段集合，
消费时复核 revision、全部输入及缺失 include；失败结果也作为后续监视基线。当前只监视已登记材质程序。

Editor 在活动帧之外发起发布：固定 Frame/Object 契约保持不变，仅 MaterialSet 1 可重绑定布局。
MaterialRenderer 准备完整 PipelineState、CPU 缓存和驻留 GPU 材质候选；全部成功后统一切换。
保存成功字节码用于目标重建，关闭编辑器不持久保存开发覆盖。DebugRenderer 只使用内嵌程序。

只换 Pipeline 而材质数据不变时复用参数 buffer／pool／set，不修改在途帧持有的旧包装。
缓存判等同时使用 PreparedMaterial 与 PipelineState；旧版回退只能用于同 Handle 且兼容当前管线。
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

帧的调用顺序与失败协议见「一帧经过哪里」。Renderer 只消费 owned RenderScene，不持有可变 Scene 或 EnTT 引用；
组件修改、Undo/Redo 和当前帧拾取使用同一份编辑后快照。
Editor::finish_active_edit 统一取消未完成 Gizmo、调用 Inspector::finish_edit；失败时拒绝后续请求。
组件属性与场景环境共用 PropertyEditTransaction 的 begin／preview／commit／cancel 和文档代际检查。
Inspector 只跟踪 ImGui 活动控件；保存、切场景或模式切换不再维护环境专用事务。
环境与后处理通过 PropertyEditResult 汇总控件手势，共用一次 begin／preview／commit／cancel 适配。
SceneEditor 是实体结构编辑、撤销／重做、Mesh 插入和资产赋值的 CPU 执行入口，校验模式、文档代际、属性契约并更新选择。
SceneCommands 保留具体命令及逆操作；SceneDocument 管保存点，EditorSceneSession 管 Play 副本，不互相兼任。
Editor 只负责请求优先级、结束 UI 活动项和安装场景后的重绑；不把渲染生命周期回调改成事件。
Inspector 发出带 Handle／revision 的材质读取请求，由 EditorAssets 解析；返回时再核对选择与 revision，过期结果丢弃。
材质默认值、模板迁移、草稿校验与完整提交集中在 material_editing；面板不直接解析文件。
请求仍在 UI 遍历结束后执行，并保留文档 generation／资产 revision 校验与菜单优先级。
离散属性赋值使用 PropertyEditTransaction::apply：结束已有手势，再 begin／preview／commit；
失败取消新事务。持续拖动仍使用独立的 begin／preview／commit，不在每帧创建历史记录。
Play 引用调试直接写克隆场景，不经过 Edit 历史；资产文件写入也不混入场景历史。

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
它不选择活动相机，也不保存 GPU 状态；当前 Runtime CameraComponent 仍提取为透视。
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
prepare_frame 返回 Result<bool, GraphicsError>：true 可绘制、false 延期、失败保留原生码；render_frame 返回 Result<void, GraphicsError>。
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
