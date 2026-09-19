# 渲染资源所有权

描述当前 owner、调用边界和销毁规则；未来项目 Shader／多 pass 扩展／RenderThread 设计见[路线图](../engine-roadmap.md)。

## 先看哪个类

| 入口 | 职责 |
| --- | --- |
| `core/engine.h` | 组合 Window、Scene、TaskScheduler、AssetRegistry 和 Renderer，驱动主循环 |
| `render/renderer.h` | 渲染子系统组合根，编排帧、RenderView、overlay 与拾取 |
| `render/scene/scene_extractor.h` | Scene → 不含 GPU 对象的 RenderScene 快照 |
| `render/scene/scene_resolver.h` | Handle/Camera → RenderSubmission |
| `render/presentation.h` | acquire／submit／present 与交换链 dependent 有序重建 |
| `render/scene/scene_renderer.h` | 完整目标版本的创建／安装与多 pass 编排 |
| `render/material/material_renderer.h` | Frame/Material descriptor、材质 Pipeline、队列排序与 Mesh 绘制 |
| `render/material/material_runtime.h` | 手工 MaterialLayout、PreparedMaterial 快照与版本缓存 |
| `graphics/pipeline/shader_interface.h` | 入口级 SPIR-V 反射结果与绑定覆盖校验；仅拥有 CPU 值 |
| `render/frame_scheduler.h` | FrameSlot 复用、提交及成功登记、image 关联、完成序号与 retention |
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
├── TaskScheduler
├── AssetRegistry → Runtime Mesh / Texture / Material
└── Renderer
    ├── RenderContext → Context / Device / Swapchain
    │                    Device → Allocator / queues / PipelineCache
    │                    Swapchain → active Generation
    ├── RenderResources → UploadManager / SamplerManager
    ├── FrameScheduler → FrameSlot[N] / SwapchainImageState[M]
    ├── Presentation（借用 RenderContext、FrameScheduler；有序协调 Scene／Overlay dependent）
    ├── RenderView / SceneResolver
    ├── LineDrawList（单帧 CPU 请求）
    └── SceneRenderer
        └── RenderState（完整兼容版本，FrameSlot 保活）
            ├── 场景 RenderPass / PipelineManager / HDR 与最终 RenderTarget
            ├── OutputPass → 色调映射 / 输出编码 / 采样绑定
            ├── ShadowPass → 深度 RenderPass / Pipeline / DepthTarget[slot]
            ├── DebugRenderer → 线段 Pipeline / VertexBuffer[slot]
            └── MaterialRenderer
                ├── PipelineState → MaterialLayout / material descriptor layout / Pipeline
                ├── FrameResources[slot] → 相机与光照 UBO / 阴影 View 与 Sampler / FrameSet / pool
                ├── MaterialRuntimeCache → PreparedMaterial → Texture / 参数字节
                └── MaterialResources[material version] → PreparedMaterial / PipelineState / Sampler / 参数 UBO / pool / MaterialSet

Editor
├── EditorAssets → AssetManager（借用 Engine 的服务）
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

Application 的实现集中在 runtime/application.cpp，对外只提供完整的 run(Config) 生命周期：
创建 Diagnostics／Engine → on_init → 引擎更新循环 → end。start/main_loop 不再作为可独立调用的接口。
Engine::create → Renderer::create → RenderContext::create 在局部准备 owner，成功后才移交私有构造器，不提供公开的半初始化对象。
交换链或场景目标准备失败返回原始错误，局部 owner 逆序释放；Engine 创建失败时释放 Diagnostics，不调用 on_init/on_shutdown，并允许重新启动。
这不是所有原生创建故障均可恢复的保证：Window、Context、Device 等底层已有的不变量/致命检查与标准库异常仍保持原约束。
初始化和更新失败显式返回 Result；on_init 一旦开始，就会尝试一次 on_shutdown，应用必须能关闭部分初始化的成员。
end 是内部操作，提前消费关闭标记，先完成 Engine 的关闭准备，再调用应用关闭钩子，失败也不会重复调用。
钩子失败时保留 Engine／Diagnostics，由应用析构先释放派生类剩余资源、再释放基类 owner；
原始初始化／更新错误继续向上传递，清理错误单独报告。关闭失败的实例不能重新运行。
run 和私有 end 返回 Result<void, Error>。通用 Error 只保存消息与 std::error_code，图形错误在边界通过 GraphicsError::as_error 保留原生类别与数值，不再把呈现 Result 转为异常或捕获后重抛。
应用钩子通过 Result 返回预期失败；run／end／launch 不捕获第三方或未预期异常，不保证异常路径调用关闭钩子。
Comet::run 消费结果并返回非零退出码；YAML 配置解析异常仅在 ConfigLoader 内转换，不在入口兜底。

ImGuiContext 的原生 Context 由带私有 ContextDeleter 的 unique_ptr 拥有；create 在私有候选中 initialize。
失败返回或异常展开都会销毁候选，由 cleanup 先关闭借用 GPU 资源的后端，再析构 pool／target。
原生 Context 保持最后声明，作为成员展开的顺序保障；不需要 catch 后 cleanup/rethrow。
正常析构仍先等待 GPU、解除纹理注册，再销毁后端及资源。
只关闭实际存在的后端，覆盖 swapchain 重建中旧后端已经关闭的状态。
等待 GPU 的析构保护集中于 Device::wait_idle_for_shutdown，不等同于设备丢失恢复。

LOG_FATAL 当前执行 assert／terminate，不展开栈，也不会进入上述异常清理；只适合明确终止的内部错误。

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
      ImGui begin → UI/请求收集、即时属性与 Gizmo、相机输入、最新 RenderView → ImGui end → 反馈提交
  → SceneExtractor（读取此时的活动 Scene，更新 world transform）
  → Renderer::render_frame
  → SceneResolver（使用实际 Target 尺寸）
  → 按请求 CPU pick → scene pass（场景物体 → DebugRenderer）
  → overlay render（录制已生成的 ImGui 数据）
  → Presentation submit / present
```

完整数据链为 `Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
文件扫描、复制、保存和同步资产加载在 on_update 执行，不占用已 acquire 的帧；这不是将全部 I/O 移出主线程。请求仍由唯一 Editor 执行，不新增事件总线。Window 可选择拦截原生关闭事件，Editor 处理未保存决策后才通过 request_close 确认退出。
Scene 维护 EntityId／UUID 查询索引与父子索引，结构修改时同步维护；这些索引不参与序列化。
Scene 的同步检查遍历全部节点，比较本地 TRS、组件是否存在、parent ID 和父级计算版本，仅重算变化节点。
单个 get_world_matrix 只检查祖先链；持续持有可变组件引用的写入同样在下次查询／提取时生效。
缓存属于 Scene 私有状态，不序列化；update_world_transforms 返回实际重算数量，静止场景为零。
Engine 同步借用 update 和 frame_ready 两个函数，不保存注册表；UI 修改后再同步变换并提取，避免使用上一帧数据。Renderer 不再调用 UI 准备，ImGuiContext 不再持有 UI 业务回调；Editor 在 on_frame_ready 显式调用 begin_frame/end_frame。
pose_world_matrix 使用层级旋转与普通世界矩阵的位置，本地及祖先缩放不进入相机朝向。
本地 TR 只计算一次，普通矩阵在其基础上应用 scale；相机与物体继续使用各自的父级矩阵。
SceneRenderer 不读 EditorMode/ImGui，不拥有 FrameScheduler，不访问呈现队列；录制时借用传入的帧上下文。

MaterialRenderer::render、DebugRenderer::render 与 SceneRenderer::render 返回 GraphicsError。
材质准备/调试缓冲增长遇到 DeviceLost 原样返回；普通失败仍沿用兼容旧材质或跳过调试批次。
Renderer 收到场景 pass 失败后停止 overlay 与提交，调用 prepare_shutdown 等待在途工作，然后返回 Engine。
部分录制的命令缓冲只由 owner 销毁，不结束并提交空帧，也不重新用于下一帧。
Editor 通过 Renderer 注册 Overlay 重建钩子、读取只读帧信息；整帧命令缓冲直接传给 Overlay。
SceneResolver 只解析 Camera、Mesh 和 Material 引用，不检查模板、属性名或纹理数量。

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
FrameSet binding 0 是相机，binding 1 是片元光照 UBO，binding 2 是阴影 sampler2D。
UBO 每灯 64 字节，使用 position、type、direction、range、color、intensity、锥角和阴影标记等具名字段；
末尾是有效／超限／无效数量、shadow_view_projection、shadow_light_index、shadow_depth_bias 和 shadow_texel_size。
类型、标记与计数仍用 float 编码，总计 2144 字节；C++ 静态断言和 Shader 反射测试核对偏移、数组步长与大小。
每个 slot 等待完成后写入，FrameResources 由在途帧保活；灯光变化不更新材质 revision 或重建 MaterialSet。

`lit_color` 提供纯色 albedo 和 Lambert 漫反射；点光使用有限范围衰减，聚光增加锥角权重。
法线按模型矩阵逆转置变换，近奇异变换输出零法线；无有效灯光时为黑色，不添加隐藏环境光。
强度是当前渲染参数，不承诺完整物理光度单位；尚无 PBR、IBL 或 clustered/tiled 筛选。

### 方向光阴影

`ShadowPass::prepare` 对提交网格的世界包围盒求并集，再由 LightingData 选择前 32 灯中
EntityId 最小、强度大于零且 casts_shadow 开启的方向光，构造覆盖场景的正交投影。
无相机、无有效网格或无符合条件的光源时，阴影索引为 -1；旧场景缺失开关时默认关闭。
阴影开关复用已有保存、克隆和属性撤销链路，不增加新组件或编辑命令。
PropertyDescriptor 默认仍要求字段存在；仅 casts_shadow 显式标记 required=false，
缺失时保留组件默认值，null／非法类型仍拒绝，不放宽其他必填字段。

SceneRenderer 的有序图是 ShadowPass → scene → OutputPass。D32 深度图按帧槽位独立，
深度写入到片元采样的 layout/access/stage 转换由 RenderGraph 生成；不在材质内插入屏障。
ShadowPass 保留通道、目标、管线和 Mesh；FrameResources 保留实际采样 View 与 nearest Sampler，
完整 RenderState 替换与 resize 都不销毁在途帧使用的资源。各 pass 的上传等待按 semaphore 合并。
MaterialRenderer 只接收已准备的 LightingData 与有效采样 View，不创建隐式后备资源；
未启用阴影时 ShadowPass 仍清除深度图，保持固定图与有效 descriptor。

当前固定 1024²、单方向光、手工 3×3 PCF；使用接收平面深度梯度补偿邻域采样，
偏移覆盖 nearest texel 量化误差并按入射角调整。所有提交网格均按不透明遮挡物处理，
只有受光材质接收阴影。阴影视口使用正高度，与采样 UV 匹配；主场景仍使用负高度。
尚无级联、texel 稳定化、视锥筛选、透明裁切、逐物体投影开关或点／聚光阴影；
大场景或动态包围盒会降低精度并可能抖动，后续按实际画面需求扩展。

Shader 热发布按程序接收完整顶点/片元对，可更新任意一个或多个程序；固定 Frame/Object 接口不可修改。
MaterialShaders 是具名程序集合，不依附 MaterialRenderer 的嵌套类型；未知名称、空集合或不完整程序在 GPU 创建前拒绝。
MaterialShader 模块复用 ShaderInterface 反射校验，MaterialRenderer 保留管线与材质版本的原子发布。
各组未参与更新时保留原版本，全部候选准备成功后发布；SceneRenderer 合并保存成功的各组字节码，
完整目标重建不会丢失其他程序的开发覆盖。编辑器监视六个材质阶段及其实际 include，
包括 `common/mesh_vertex.glsl` 与 `lighting/forward.glsl`；生产目录约定见
[README 的 Shader 开发](../../README.md#shader-开发)。

### 材质准备与寿命

内置 `unlit_texture_blend` / `unlit_color` / `lit_color` 的初始 metadata 由 MaterialLayout::find_builtin 共享。
MaterialLayout::reflect 按 shader_name（为空时使用逻辑属性名）匹配已登记属性，重建 offset／块大小／binding；
名称、默认值、范围、步长和 Color/Vector 语义仍由 metadata 提供。参数块 binding 由布局指导创建和写入，不再固定为 0。
未知／缺失／改类型字段、多参数块与不支持的资源形状拒绝；相同物理布局复用原对象，不增加平行 revision 计数。
Editor 在初始化和成功热更后向 Inspector 交付已发布布局快照；控件和草稿校验使用同一布局，不清空草稿、不自动保存。
独立面板未收到该模板布局时回退内置 metadata；目标重建后同值布局仍可用于 UI，不让面板引用 Renderer。
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
每个执行的帧都维护统计和缓存，无相机时不写相机 buffer、不录制 draw，但仍清理未使用项；未成功 acquire 的跳帧不伪造提交。
队列按模板名与材质 Handle 排序。

### 编译、反射与缓存边界

编译库静态依赖 glslang，不链接 engine；CLI 复用 common/file_io.cpp 原子写 SPIR-V 和 depfile，再由构建生成内嵌字节码头。
每个请求独占解析器与输入快照，进程初始化仅一次；快照记录逻辑路径、解析路径，以及存在或缺失的内容。
成功返回前复核输入，失败不返回字节码；include 诊断保留源文件／行号并追加具体原因。
参数、源文件读取与 include 限制使用明确失败返回，文件系统查询使用 error_code；缺失是可记录的依赖状态，读取错误不是缺失。
compile_source 负责单次编译结果，公开 compile 统一收集依赖和复核输入；不因早退漏掉失败请求的依赖。
第三方调用、include 处理和 CLI 写文件保留异常边界。
depfile 只列存在的依赖，新增遮蔽文件不保证自动触发构建。原子写针对单文件，不是 SPIR-V／depfile 的跨文件事务。
开发编辑器的 Worker 发布复核请求 revision 与输入，快照不等于文件锁；具体热更新所有权见下文。

Shader 先反射指定入口再创建 module，拥有字节码副本和反射值；作为创建 Pipeline 的局部候选，不保留全局名称缓存。
ShaderLayout 检查 descriptor 类型／数量／stage 和 push 覆盖；MaterialLayout 额外核对材质参数块大小、偏移、类型与纹理协议。
BlockMember 递归保存自有类型、数组维度／stride 和矩阵存储方式；push constant 保留成员信息。
采样图片保留维度、数值类型、数组／多采样／深度信息，当前 MaterialLayout 只接纳普通 float sampler2D。
has_same_resource_layout 是 descriptor／push 的保守资源比较，包含成员名称，不替代阶段连通性或完整 Shader 兼容验证；
尚未覆盖 StorageImage 的格式等通用计算资源契约。原生 SPIR-V 枚举不进入公共类型。
原生反射类型仅在 shader_interface.cpp 转换，Vulkan 布局对照留在 ShaderLayout 实现中。
反射 API 独立于 Device；当前 Worker 只编译，消费端核对固定契约，Shader 创建执行自身反射校验，
材质布局重绑定由 MaterialRenderer 调用 MaterialLayout::reflect 完成；
头和指令长度预检不是完整 SPIR-V validator。
ShaderInterface 按入口保存 user input/output 的名称、location 与 Comet Format，跳过 built-in；不向调用方暴露 SPIR-V 原生类型。
基础 I/O 限定为 location-based 32 位标量／向量，数组、矩阵、结构体、64 位与非零 component 明确拒绝。
PipelineKey 在配置副本规范化后校验 Vertex→Fragment 的 location／精确类型，并检查顶点 attribute 的 location／精确格式与 binding 存在性。
额外未消费的顶点属性／顶点输出允许保留；缺失、类型不符在创建 PipelineLayout／Pipeline 和查写缓存前返回错误。
这是当前 Comet 的保守输入契约，normalized／packed 格式转换、复杂插值和附件输出兼容性仍待扩展。

ShaderInterface::reflect、MaterialLayout::create、布局／specialization 校验及 PipelineKey::create
与资产导入、序列化统一使用 common/result.h 的 Result<T> 返回预期失败，不保留资产层别名或转发头。
反射和布局只返回完整候选，specialization 先完整校验再删除默认值，PipelineKey 在配置副本上规范化。
Shader::create 与 PipelineManager 检查这些结果，失败不发布候选。
Result 的错误类型可选，默认仍为字符串；Shader／Pipeline 链路使用 GraphicsError 保存消息和可选原生 Vulkan 结果码。
CPU 校验错误不伪造 Vulkan 错误码；GPU 创建使用 Vulkan-Hpp 返回码重载，不捕获 vk::SystemError 或调用 LOG_FATAL。
graphics/creation.h 统一将 device-owned 句柄纳入 UniqueHandle，再判断返回码，失败时连同部分创建的句柄一起回收。
Shader、PipelineLayout、Pipeline 的私有构造函数只接收已创建的 owner；Pipeline 先销毁自身句柄，再释放 Layout。
分配 C++ 容器等非预期异常仍可传播，不承诺 noexcept。
MaterialRenderer::create 在私有候选中初始化 frame 资源和内置管线；DebugRenderer::create 成功创建 Pipeline 后才构造对象。
SceneRenderer 的私有 create_state 创建完整 RenderState：场景 RenderPass、中间／最终 RenderTarget、
PipelineManager、MaterialRenderer、DebugRenderer 和 OutputPass。
全部成功后才安装；配置入口不再拆成可被调用方任意组合的 setup 阶段。
完整目标切换仅经 Renderer 在活动帧外执行，不增加全设备等待；失败保持旧代，成功后旧代仍由已提交帧保留。
RenderState 按依赖逆序析构，RenderPass 最后释放；FrameSlot 同时保留完整版本和实际录制的 Target，确保 resize 替换附件不丢旧引用。
单纯尺寸变化仍只创建 MultiTarget，不重建材质或管线；它允许在 Overlay prepare、场景 pass 开始前同步安装。
最外层 Renderer／Editor 将预期失败作为 Result 返回 Application；内置 MaterialLayout 常量错误属于内部不变量。
DescriptorSetLayout／DescriptorPool 创建及 DescriptorSet 分配也返回 Result<T, GraphicsError>。
布局和池使用 UniqueHandle，集合只借用句柄，由池统一回收；布局可共享，池工厂返回 unique_ptr，
FrameResources／MaterialResources 按实际保活需要转为 shared_ptr，ImGui 仍独占池。
集合分配先准备 CPU 容器，再调用 Vulkan-Hpp 返回码重载；失败不 reset 池，也不破坏已有集合。
材质准备显式检查 Buffer、Pool 与集合分配结果，成功写入 descriptor 后才发布，不再整段 catch std::exception。
启动消费者检查结果并返回失败；ImGui 后端内部调用不属于上述 Comet API 的覆盖范围。
DescriptorSet::update 接收嵌套的 UniformBufferWrite／ImageSamplerWrite，立即转换并批量写入；
写入项引用 Comet Buffer／ImageView／Sampler，不保存资源，也不自动同步 GPU，调用方仍须保证目标集合可安全修改。
CommandBuffer::bind_descriptor_sets 只接收 Comet Layout／Set，原生绑定点与句柄数组留在 graphics 实现中。
GpuResourceResult 通过 error() 提供 GraphicsError，业务层读取 message／is_device_lost()，不为了日志解析 vk::Result；
原生 result() 保留给 graphics 内部和诊断测试。这是消费接口收敛，不是完整的多后端抽象或 Vulkan 头文件隔离。
资产 Mesh／Texture 创建、调试 buffer 扩容和离屏 resize 在普通失败时保留原有降级策略；DeviceLost 必须向应用退出边界传播。
ensure_loaded 不兜底所有异常，材质创建和场景激活移出文件读写 catch；后台完成通过 future.get 检查任务结果，不为 owner 上的 GPU 发布增加异常兜底。
完成任务在发布成功或异常展开后均释放槽位，避免析构再次等待已 get 的 future；不提前释放正在发布的槽位，保持重入与预算语义。
Mesh／Texture 的无调用方 fatal 创建包装以及 RenderTarget 的 fatal 离屏包装已移除，现有消费者使用可失败入口。
Sampler::create 返回 Result<shared_ptr<Sampler>, GraphicsError>，校验配置后用返回码重载创建 UniqueSampler。
SamplerManager 的预设统一经过 create_sampler；同名同配置复用，同名不同配置返回错误，不替换已有对象。
linear-repeat 预设使用各向异性数值的精确位模式作为内部名称后缀，不以舍入后的显示字符串作缓存身份。
MaterialRenderer 向上传递 sampler 错误；Viewport 只在构造时取得 nearest-clamp 并持有，帧更新仅复用。
开发编辑器的 `render/shader_reload` 接收 1..16 个具名 CPU 请求；当前按同名 vert/frag 登记三个材质程序，不监视辅助线 Shader。
Worker 只捕获请求副本和共享结果，不访问 Editor、Scene 或 Device。销毁服务后已有 CPU 工作可以结束，但不会再发布。
每组最多一个在途任务及合并的最新请求，共用 TaskScheduler 背压；无 GPU 类型、发布回调或全局 EventBus。
每批任务编译所有阶段，消费时复核 revision、全部输入及缺失 include 候选；失败结果也作为下一次监视的基线。
CPU 编译成功和 GPU 发布成功分开；Editor 负责具体程序映射、日志与 Inspector 同步。
Editor 仅对 GraphicsError::is_out_of_memory 判定的 Vulkan 主机／设备内存不足请求 retry_delivery；
ShaderReload 使用 common/RetryBackoff 保存重新交付期限和次数，同一 revision 依次等待 1、2、4 秒，最多重试三次，复用已编译 CPU 结果，不持有 GPU 候选或调用发布回调。
重复预约不延后期限或消耗次数；额度耗尽后记录停止日志，等待新请求。新请求同时重置次数。
重试前复核 revision 和全部输入；新请求取消旧重试。消费成功或不可重试失败不再请求交付，编译失败不能重试交付。
每个 revision 的内存不足重试提示只记录一次，其他发布错误仍独立报告；DeviceLost 沿应用清理边界退出。
每次 GPU 准备针对当前 SceneRenderer 的目标重新执行，不保存旧 RenderPass 的半成品；未改变目标重建和 WSI 失败策略。
主线程在 Engine 帧准备和绘制前调用 Renderer::reload_material_shaders；入口拒绝活动帧内发布，内部再交 SceneRenderer。
MaterialRenderer 先对照构建内嵌程序的固定资源契约；仅允许片元 MaterialSet 1 进入布局重绑定。
随后准备完整候选管线表，并复制 CPU 准备缓存与驻留材质索引，在候选中按新布局重打包、创建材质绑定；
任一失败不切换已发布索引。全部成功后 swap 管线、CPU 缓存与 GPU 材质，SceneRenderer 才记录新的覆盖字节码。
ReloadReport 报告管线准备、候选索引复制、材质 CPU 准备与材质 GPU 创建耗时及数量；Editor 成功发布时写入日志。
管线准备时间包含反射／校验，不伪称纯 GPU 时间；这些是观测值，不设耗时阈值测试，也不据此预先跨帧拆分事务。
create_material 共用于普通材质更新和热更事务，只创建资源／返回结果；错误策略由调用方决定，不修改全局统计或缓存。
初次创建、热更和 Renderer 重建使用同一基线，不能把待验证覆盖当作初始可信接口。
这保护运行时热更，不自动证明重新构建后的内嵌 Shader 与 C++ ABI 一致；修改内置协议仍须同时更新 C++ 与测试。
ShaderModule 只在候选创建期间存在；不设名称缓存，PipelineManager 缓存仍是弱引用。
MaterialResources 缓存同时比较 PreparedMaterial 和 PipelineState；旧资源可暂作分配失败时的回退，并由在途帧持有至槽位回收。
固定布局复用 DescriptorSetLayout；命中原 Pipeline 时保留原 PipelineState。
只换 Pipeline、材质数据未变时复制 MaterialResources 的共享引用并替换新包装内的 PipelineState，不重建 Buffer／Pool／Set，
也不修改在途帧保留的旧包装。material_versions_created 统计版本，material_bindings_created 只统计实际新建的材质绑定。
两者是 render 期间统计；提前重建的工作量由 reload_shaders 返回的 ReloadReport 提供，不混入下一帧统计。
相同 Pipeline 批次无需复制驻留缓存；大量布局重建仍在 owner 线程同步准备，存在峰值内存与帧时间成本。
GPU ShaderModule 仍会为候选临时创建，重复发布无新 Pipeline／材质版本不代表完全无 GPU API 调用。
SceneRenderer 保存最后成功的字节码，重建目标／管线时沿用，不因重建恢复到嵌入版本；关闭编辑器后不持久保存开发覆盖。
DebugRenderer 初始化和目标重建统一通过 create → create_pipeline 使用构建内嵌 Shader，
由 Shader／PipelineKey 校验布局、顶点输入和阶段连接；不提供热发布入口或覆盖字节码。
Debug 不使用 RenderResources；render 把实际 Pipeline／buffer 交给 FrameSlot 保活。
内置 MaterialLayout 仅支持已登记属性的布局重绑定；新增属性语义、复杂 I/O 和项目程序资产仍待后续。CPU 后台化不等于 GPU 创建无主线程开销。
Sampler 只拥有自身 UniqueSampler，不另存 Device 句柄；管理器借用 Device，设备仍必须活到所有 sampler 释放之后。
RenderPass::create 使用 UniqueRenderPass，构造仅接管完整附件描述和句柄；错误返回 GraphicsError。
交换链目标与离屏目标共用附件创建逻辑，前者复用 Generation 的呈现图像，其余附件独立创建。

SceneRenderer 对离屏 resize 保存失败尺寸及 RetryBackoff：同一请求仅在 Vulkan 主机／设备内存不足时
按 1、2、4 秒最多重试三次，耗尽或其他错误停止；新尺寸（包括回到实际尺寸）或目标重新安装清除旧失败状态。
相同请求每帧只检查期限，不重复创建；第一次失败与最终停止分别记录日志。DeviceLost 仍退出。
resize 在 on_frame_ready 的 Viewport::update 中同步准备并安装，不改变 ImGui 更新顺序；失败不能覆盖实际 Target 尺寸，
纹理绑定、场景 viewport 和拾取分辨率继续使用实际目标。该限制按单个尺寸请求计算，不限制连续不同尺寸的尝试。
RetryBackoff 是无资源、无线程的值类型，只管理预约／一次性到期消费／次数／重置；默认三次指数退避，可按实例指定策略。
请求身份、输入复核、可重试错误判断与日志仍由 ShaderReload／Editor／SceneRenderer 各自处理，不集中为全局重试服务。
SwapchainTarget 只发布完成全部 framebuffer 的候选，失败先释放 framebuffer/view，再释放 Generation 引用。
FrameBuffer 无调用方的 fatal 创建包装已移除，目标统一使用 try_create。
ImGuiContext::create 和重建返回结果，失败时关闭已初始化后端，再销毁池、目标与 pass；不发布半初始化 UI。
重建已释放旧依赖；暂时失败由 Presentation 有界退避重试，设备丢失或重试耗尽以 Result 返回应用退出清理。
ImGui 第三方后端内部创建目前仍不能靠 Init 的 bool 完整报告 GPU 失败；回调处还有未交给后端 owner 的局部资源，不直接抛异常跳过释放。
当前后端的 Vulkan Shutdown 还清除主视口平台数据，因此 format/image count 重建同时关闭并重建 GLFW 后端；保留 ImGui Context 和 UI 状态。
Application 的失败清理及 Device 关闭等待保护必须保留，不以 LOG_FATAL 替代可恢复错误。

PipelineConfig 与状态位于 pipeline_config.h/.cpp，PipelineKey 的完整判等、规范化与哈希位于 pipeline_key.h/.cpp。
Key 包含完整 Shader 内容／入口、layout、配置、RenderPass 身份与附件格式／采样数；名称只作标签，hash 不代替相等比较。
动态 viewport/scissor 的无关静态值和无关顺序会规范化；规范化配置、静态 viewport/scissor 和 subpass 都用于实际创建。
Key 属于 Device/RenderPass 域，不是持久格式；其中 Vulkan 值不传播到材质或 ShaderInterface API。
PipelineManager 只持 weak_ptr，使用方与 FrameSlot 持有实际 Pipeline；创建请求或 collect_unused 清理过期键，不每帧扫描。
get_cached_pipeline_count 包括尚未清理的过期项。模板选择、Pipeline 对象复用、驱动 PipelineCache 是三种不同职责。
材质热发布同时切换 PipelineState 与 GPU 材质缓存；复杂接口的后续安排见[路线图](../engine-roadmap.md#阶段-5渲染架构升级)。

### 驱动 PipelineCache 持久化

Device 独占 `graphics/pipeline/pipeline_cache`，Pipeline 和 ImGui 只借用原生缓存句柄。
Application 构造时接收可选缓存根目录，在 run 中传入 Config::Vulkan；Editor 从实际 ProjectPaths 取目录，
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

只有 prepare_frame 成功且值为 true 才调用 frame_ready；frame_ready 返回 Result<void, Error>，成功后才提取并提交。它可以修改或替换 Scene，Engine 在其返回后重新读取 owner。失败时 Engine 执行关闭准备并返回原始错误，不再绘制或重用已取得的帧；这不是可恢复的单帧取消接口，失败后的 Engine 拒绝再次运行。
Renderer 不接收 Scene getter/provider，仍只消费 owned RenderScene；不持有可变 Scene 或 EnTT 引用。
编辑命令完成后提取，因此组件修改、Undo/Redo 和当前帧拾取使用同一份场景快照。
Editor::finish_active_edit 统一取消未完成 Gizmo、提交 Inspector 编辑；失败时拒绝后续请求。
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

SceneRenderer 的 RenderState 保存一次编译的双 pass Plan。每帧绑定当前 slot 的 HDR 实际附件，
先转换到 attachment layout，再绘制材质和辅助线，将颜色／MSAA resolve 输出转为 SampledRead，供色调映射读取。
附件每次清除，slot 复用前已等待 GPU，因此允许从 Undefined 丢弃旧内容；resize 只替换实际绑定，
旧目标继续由在途帧保活。图管理 HDR 附件和两个 pass 之间的同步；最终输出 RenderPass 负责清除、
存储及 Present／ShaderReadOnly 转换，不在图中重复声明它的 layout。ImGui 和 WSI 提交仍在图外，
呈现 RenderPass 的 external dependency 对齐 acquire 等待阶段。

ImageInfo 支持显式 mip/layer 数量，但不自动生成 mip，也未新增完整数组纹理视图 API。
HostRead/HostWrite 只用于外部交接，不作为 GPU pass；CPU 读回仍必须等待 completion，并满足映射／缓存一致性要求。
Upload timeline、WSI semaphore 和资源初始化真实性仍是调用方契约。

测试覆盖 CPU 计划、四 pass 实际读回、mip/layer/buffer 区间、跨提交交接、绑定拒绝、回调失败、
MSAA 与离屏 resize。独立 `render_graph_sync_validation` CTest 开启同步校验，
以不提交的漏 barrier 命令作为负对照，确认校验层生效；不替代跨平台运行和人工视觉验收。

## HDR 与 SDR 输出

场景颜色由 Config::Render::SCENE_COLOR_FORMAT 固定为 R16G16B16A16_SFLOAT，MSAA resolve 也保留 HDR。
RenderContext 把场景格式写入 DeviceCapabilityRequest；设备候选评估和场景创建复用
graphics 层的 validate_color_target，检查 attachment／blend／sampled、单采样 resolve 和场景 MSAA。
输出附件按实际选中的交换链格式单独检查 Count1，不把显示格式当成场景 MSAA 格式。
缺少场景能力的设备在候选阶段被拒绝；SceneRenderer 的复核失败仍返回 GraphicsError。
不静默退回 8 位场景颜色，也不自动更换场景格式。

OutputPass 位于 `render/passes/`，由 SceneRenderer::RenderState 持有，是具体的最终输出步骤，
不是与 SceneRenderer 并列的渲染子系统，也不是底层 Vulkan RenderPass 的别名；不引入通用 Pass 基类。
OutputPass 只拥有固定输出 RenderPass、fullscreen Pipeline、sampler、descriptor layout
和每 slot 的输入 Binding，不拥有 Scene、Window 或 FrameScheduler。
创建、绑定准备和录制通过 Result 返回失败；旧 Binding 不原地修改，替换后由在途帧保留。
录制保留实际 Binding、输出目标及其 GPU 依赖，即使绘制器先销毁，已录制资源仍存活到帧完成。

app 输出到 SwapchainTarget，editor 输出到 SDR MultiTarget；对外 get_render_target 和
get_offscreen_color_view 仍代表最终显示目标，不暴露中间 HDR。
replace_targets 先准备 HDR 和输出目标，全部成功才同时替换。普通离屏 resize 保留现有重试预算；
失败时不发布半套尺寸，旧帧保留实际两套目标。运行时 WSI 重建同样重建 HDR／输出配对，
但不改变交换链退休后不可回滚的原有规则。
输入 Binding 最多按 slot 保留旧 HDR view，直到该 slot 换用新 view 或绘制器销毁。

fullscreen triangle 不需要顶点缓冲，正高度 viewport 保持纹理方向。
色调映射为 H * (1 - exp(-max(color, 0) * exposure / H))，SDR 的 H=1，HDR 的 H=render.hdr_headroom；
H 表示相对白色的输出峰值（1..16，默认 4），不是显示器查询结果；当前场景曝光固定为 1。
pass 接口拒绝负数、NaN 和无穷曝光。sRGB 附件由硬件编码，UNORM 附件由 Shader 执行分段 sRGB 编码。
扩展线性 HDR 输出必须是 RGBA16F + ExtendedSrgbLinearEXT，不做 gamma 编码，不再将高亮压进 0..1。
白色基准 1 由系统合成器解释，不假定跨平台固定 nits；实际显示亮度仍由系统和屏幕决定。
不支持 HDR10/PQ、自动曝光、Bloom 或动态后处理节点。
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

旧 027 的无呈现恢复已按当前 Presentation 所有权核对：独立 swapchain_recovery 测试目标重编译实际 WSI 消费者，
仅在测试进程重命名 Vulkan 入口，生产接口没有故障注入字段或回调。
创建失败测试先真实创建并退休旧交换链，再销毁候选、返回 OOM；同时覆盖图像枚举失败、连续 acquire OutOfDate、
present 后结束已提交帧、无 active 时关闭、surface 枚举有界及自动重试预算耗尽。
直接呈现与附带离屏目标两种配置均执行真实 clear／submit／present，验证暂停期间不 acquire／提交、恢复使用空 oldSwapchain、离屏 owner 不变。
这不是完整 ImGui 人工验收；SurfaceLost 的已有回归从 dependent 返回错误驱动恢复，不模拟平台真实丢失窗口。
与旧 027 不同，当前回调必须允许重复释放，以清理 dependent 部分重建后的资源；不迁回旧的固定 100 ms 无限重试。

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
