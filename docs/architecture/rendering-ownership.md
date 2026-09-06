# 渲染资源所有权

描述当前 owner、调用边界和销毁规则；更广的 Shader 语义、RenderGraph/RenderThread 后续设计见[路线图](../engine-roadmap.md)。

## 先看哪个类

| 入口 | 职责 |
| --- | --- |
| `core/engine.h` | 组合 Window、Scene、TaskScheduler、AssetRegistry 和 Renderer，驱动主循环 |
| `render/renderer.h` | 渲染子系统组合根，编排帧、RenderView、overlay 与拾取 |
| `render/scene/scene_extractor.h` | Scene → 不含 GPU 对象的 RenderScene 快照 |
| `render/scene/scene_resolver.h` | Handle/Camera → RenderSubmission |
| `render/scene/scene_renderer.h` | Target、RenderPass、帧与场景 pass 编排 |
| `render/material_runtime.h` | 布局契约、PreparedMaterial 快照与 revision 缓存 |
| `render/material_renderer.h` | Mesh 队列排序、多布局 Pipeline、FrameSet/MaterialSet 与物体绘制 |
| `render/shadow_renderer.h` | 场景边界准备、方向光深度 pass 及每 slot 阴影图；不读取 Scene |
| `render/frame_scheduler.h` | FrameSlot 复用、image 关联、完成序号与 retention |
| `render/render_graph.h` | 有序 pass 声明、不可变 Barrier2 计划、导入／导出状态和 frame 录制；实现同目录 .cpp |
| `render/line_draw_list.h` | 通用 CPU 线段列表；`render/debug/debug_renderer.h` 是当前 GPU 消费者 |
| `render/resource/resource_manager.h` | 设备资源工厂、上传及 Shader/Sampler 共享资源 |
| `graphics/` | Vulkan 对象与显式同步后端 |
| `graphics/pipeline/pipeline.h` | PipelineConfig/Key、设备 Pipeline 与弱引用缓存；键实现见 pipeline_key.cpp |
| `graphics/pipeline/pipeline_cache.h` | Device 拥有的驱动缓存、磁盘封装校验与原子保存；不保存业务 Pipeline 索引 |
| `graphics/pipeline/shader_interface.h` | SPIR-V 的自有 CPU 接口值，不持有设备或反射库指针 |
| `tools/shader/compiler.h`（仓库根路径） | CPU GLSL 编译与输入快照；构建 CLI 共用，engine 不依赖该工具库 |
| `editor/src/imgui_context.h` | 编辑器 UI 最终呈现和私有纹理绑定，不属于 engine |
| `editor/src/shader_reload.h` | 单个 Shader 编译组的监控、防抖、有界后台编译和 CPU 候选；不持有 Device/Renderer |

engine 入口路径相对 `engine/src/`。Graphics 的 command/resource/pipeline/synchronization 按职责分目录；
Context、Device、Queue、Swapchain、RenderPass、FrameBuffer 保留在根层，因为它们跨越多个职责组。

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
    ├── ResourceManager → UploadManager / ShaderManager / SamplerManager
    ├── RenderView / SceneResolver
    ├── LineDrawList（单帧 CPU 请求）
    └── SceneRenderer
        ├── RenderPass / PipelineManager（结构化 key → weak Pipeline）
        ├── FrameScheduler → FrameSlot[N] / SwapchainImageState[M]
        ├── RenderGraph::Plan（Shadow→HDR Scene→后处理采样，只有 CPU 状态）
        ├── MaterialRenderer
        │   ├── FrameResources[slot] → FrameSet / ViewProjectBuffer / LightingBuffer
        │   │                           / shadow ImageView / nearest Sampler
        │   ├── PipelineState[layout] → MaterialLayout / set layouts / Pipeline
        │   ├── MaterialRuntimeCache → PreparedMaterial → Texture / parameter bytes
        │   └── MaterialResources[revision] → PreparedMaterial / MaterialSet / parameter buffer
        ├── DebugRenderer → 线段 Pipeline / VertexBuffer[slot]
        ├── ShadowRenderer → 深度 RenderPass / Pipeline / D32 MultiTarget[slot]
        ├── HDR MultiTarget[slot]（RGBA16F / depth / 可选 resolve）
        ├── PostProcessRenderer → 输出 RenderPass / Pipeline / Sampler / Binding[slot]
        └── SDR RenderTarget：runtime SwapchainTarget 或 editor MultiTarget

Editor
├── AssetManager（借用 Engine 的服务）
├── ShaderReload：Material 五 Shader 组 / Debug 两 Shader 组（只产生 CPU 候选）
├── EditorState / SceneDocument / EditorSceneSession / SelectionService
├── CommandHistory ← Inspector / TransformGizmo 各自的属性事务
└── ImGuiContext
    ├── RenderPass / SwapchainTarget / DescriptorPool
    └── TextureBinding[slot] → ImageView / Sampler / ImGui descriptor
```

- 引用表示必需且不可重绑定的借用；指针用于可空、可换 owner 或 moved-from 状态。
  unique_ptr 独占，shared_ptr 延长共享寿命；原生 Vulkan/GLFW handle 仍遵守各自协议。
- Renderer 是组合根，不是所有 GPU 对象的直接 owner；Device 也不反向拥有业务服务。
- app/editor 的 AssetManager 先于 Engine 销毁；后台任务先结束，GPU 使用完成后再释放 Registry 和渲染资源。

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
Engine：事件 → Application 更新
  → Renderer::prepare_frame
      回收完成的 upload → 等待 slot / acquire / 开始录制
      → overlay prepare（UI、编辑命令、相机输入、最新 RenderView）
  → SceneExtractor（读取此时的活动 Scene，更新 world transform）
  → Renderer::render_frame
  → SceneResolver（使用实际 Target 尺寸）
  → 按请求 CPU pick → Shadow 深度 pass → HDR scene pass（物体 → DebugRenderer）→ fullscreen SDR 输出
  → overlay render（录制已生成的 ImGui 数据）
  → submit / present
```

完整数据链为 `Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。

LightComponent 是场景数据，LightType 是被组件/渲染快照共用的 CPU 枚举；RenderLight 不持有 Entity 或 Scene 指针。
`render/lighting.h/.cpp` 集中 RenderLight 和 GPU ABI 的 LightingData，prepare 校验、按 EntityId 稳定排序并限制 32 灯；
灯光位置/方向、线性颜色/强度和锥角打包为 vec4，固定 FrameSet binding 1。GPU owner 仍是 MaterialRenderer::FrameResources，
不为灯光新增 LightManager、EventBus 或持有 Scene 的渲染 System。
lit_color 使用独立 vertex/fragment Shader，原 unlit 材质不改变语义。FrameSet 每 slot 更新，灯光变化不失效 MaterialSet。
热更新 API 接受完整 unlit 三 Shader、完整 lit 两 Shader或完整五 Shader；editor 当前整组编译五个，失败不部分发布。
lighting.glsl 是实际共享头文件，构建 depfile 和 editor 输入快照都跟踪它；Frame ABI 变化仍拒绝热发布。

ShadowRenderer 从 resolved Mesh 的 local bounds 和 model matrix 求世界边界，LightingData::prepare_shadow 做纯 CPU 正交投影拟合。
选中已保留的首个 casts_shadow 方向光；1024² D32 深度图按 slot 分配，3×3 PCF 与偏移在 lighting.glsl 中计算。
RenderGraph 管理 DepthStencilAttachmentWrite→fragment SampledRead，RenderPass 不重复改变最终 layout。
FrameSet binding 2 采样当前 slot 深度 view，只在 view 改变且 slot 已等待后更新 descriptor；没有每材质阴影 binding。
独立 MaterialRenderer 用 1×1 白色中性图保证 sampler descriptor 有效，构造阶段等待该微小上传，正常帧不 CPU 等待阴影。
阴影 owner 可以在录制后释放，FrameScheduler 保留 pass/target/pipeline/Mesh 到完成；多个 pass 的上传 wait 合并最大 timeline 和阶段并集。
没有阴影灯时仍清除深度图，避免复用旧阴影；普通 viewport resize 不改变固定阴影分辨率。
当前无级联、点光/聚光阴影、透明裁切或静态缓存，全部 resolved Mesh 都按不透明几何投影；shadow Shader 暂随构建更新，不加入热编译组。
SceneRenderer 不读 EditorMode/ImGui。SceneResolver 只解析 Mesh/Material，不检查 template、属性名称和数量。
MaterialRenderer 选择 MaterialLayout，MaterialRuntimeCache 按材质身份/revision 和不可变 layout 身份准备纹理 binding 与参数字节。
MaterialLayout 从反射重绑定 offset／块大小／binding，保留显式编辑语义；发布后的只读描述交付 Inspector 生成控件，
不依赖 Device 或 ImGui，显示语义不是 SPIR-V 反射信息。
缺槽或不匹配在缓存层记录诊断；同版本不重复解析。未使用缓存按帧回收，已交付的 PreparedMaterial 快照独立保活。
生产 GPU 支持 cube_texture 和 unlit_color 两套 MaterialSet 布局；FrameSet 共用相机契约。
当前仅不透明物体按 pipeline/material 排序；语义仍需登记，不等同于已经支持任意 Shader 或透明排序。

Shader 保存不可变 SPIR-V 内容与 ShaderInterface；同标签加载不同内容时先成功创建候选，再替换 ShaderManager 的旧条目。
ShaderInterface 校验 GPU 布局覆盖，MaterialLayout 另校验参数块大小、偏移与类型；不是从反射推断编辑语义。
PipelineKey 使用完整字节码/入口、descriptor/push 范围、PipelineConfig、RenderPass 身份及附件格式/采样数；
键相等不依赖名字、Shader 地址、VkShaderModule 或 VkDescriptorSetLayout 的句柄相等。
specialization 按 stage/ID/类型/原始位模式参与键，显式默认值规范化后传入 Vulkan；不同 RenderPass 不尝试兼容复用。
PipelineKey 不是跨进程磁盘格式。
`LaunchOptions.cache_directory` 经 Config/RenderContext 传给 Device；app/editor 的启动入口默认提供项目 `.comet/cache`。
PipelineCache 按 vendor/device/UUID 读取 `vulkan/*.bin`，先检查封装版本／长度／FNV-1a 校验和，再检查 Vulkan 32 字节小端头；
缺失／损坏／不匹配使用空缓存，正常关闭原子保存。空路径禁用磁盘但仍保留内存驱动缓存；路径不写入共享 YAML。
Pipeline 和 ImGui backend 只借用原生 cache handle，Device 最后释放 owner；磁盘缓存不延长 Pipeline 生命周期。
PipelineManager 只弱引用 Pipeline，实际 owner 是 MaterialRenderer、DebugRenderer 和录制过它的 FrameSlot。
最后一个实际 owner 释放即销毁 GPU 对象；过期 key 在下次创建或 collect_unused 时清理，不阻塞 GPU 等待。

Editor 持有两个 ShaderReload，分别服务 Material 三 Shader 与 Debug 两 Shader；各自最多一个在途编译和一个最新待执行请求。
Worker 捕获自有请求和 CPU 结果，不捕获 this 或设备。on_update 只消费最新且输入仍匹配的候选，SceneRenderer 拒绝活动帧内发布。
MaterialRenderer 先准备 Pipeline、反射布局和全部驻留 CPU/GPU 材质，成功才以 noexcept swap 一起发布。
兼容更新共享原 buffer/descriptor/pool；布局改变则重打包参数并创建新绑定。Inspector 随成功发布取得新布局快照。
DebugRenderer 检查完整固定接口，先创建候选 Pipeline，再与 Shader 快照一起切换；不重新创建已有 slot 顶点缓冲。
每组内部原子发布，两组相互独立；共享 include 变化也不承诺跨组同时生效，若未来形成真实共享 ABI 再合并发布域。
旧帧持有真实 MaterialResources/Pipeline，不能原地修改旧对象。两种 Renderer 重建时只补缺失 Shader，不覆盖已发布版本。
固定 Frame/vertex/push 接口、未知材质语义仍拒绝改变；兼容检查包含矩阵布局、数组、图片形状及阶段输入输出。

只有 prepare_frame 成功才提取并提交；overlay prepare 可以修改或替换 Scene，Engine 在其返回后重新读取 owner。
Renderer 不接收 Scene getter/provider，仍只消费 owned RenderScene；不持有可变 Scene 或 EnTT 引用。
编辑命令完成后提取，因此组件修改、Undo/Redo 和当前帧拾取使用同一份场景快照。

LineDrawList 只保存世界空间端点与颜色，Renderer 在场景 pass 录制前接受多次追加并持有副本。
通常在 update/prepare 提交；本帧拾取的结果回调也可提交，因此点击产生的选择反馈不必等下一帧。
render_frame 消费后清空，准备失败、隐藏视口或无合法相机时丢弃，不跨帧保留。
DebugRenderer 使用场景的相机矩阵、RenderPass 格式和 MSAA；LineList、深度测试 LessEqual、不写深度、alpha 混合。
每 slot 独立的持久映射 CPU-to-GPU vertex buffer，只在等待当前 slot 完成后写入或扩容；
绘制使用的 buffer/Pipeline 同时被 FrameSlot 保留至 GPU 完成。扩容失败保留旧 buffer 并跳过本批，延后重试。
它不持有 Scene、Selection 或 ImGui；选中框等调用方自行转换成世界空间请求。

Editor 在 UI 编辑命令完成后读取选中实体的 Mesh local bounds 和最新 world matrix，
用 LineDrawList::add_box(box, transform, color) 变换八角点并连接十二条边，不重新拟合世界 AABB。
普通帧在 prepare 提交；有视口拾取请求时，等结果更新 Selection 后再提交，避免旧框和新框同时出现。
选择状态仍由 SelectionService 持有，Scene/Mesh/Material 不保存 selected 标记；Play、隐藏视口或无有效 Mesh 时不提交。

TransformGizmo 是编辑器侧的投影、命中与平移/旋转/缩放事务，不是渲染资源。它与 Inspector 各自持有 PropertyEditTransaction，
共享同一个 CommandHistory；拖动用 UUID 定位并预览对应 Transform 属性，释放提交一次，取消恢复。
ViewPanel 优先将普通左键交给 Gizmo，未命中才请求场景拾取；拖动时占有 ImGui active ID，阻止快捷键和相机导航。
UI 回调完成命令／相机更新后，ViewPanel::draw_gizmo 将最新句柄追加到本帧窗口 draw list，随后 ImGui::Render。
手柄作为可操作的 UI 覆盖层不受场景深度遮挡，不需要修改 DebugRenderer 或向 engine 注入编辑器状态。
点击拾取帧不显示旧选择的手柄，新选择手柄在下一 UI 帧出现；选中包围盒仍由拾取回调在当帧提交。

RenderView 的 CameraSelection 选择显式 editor camera 或 Scene primary camera；
请求 override 却缺少数据时不静默回退。没有合法 Camera 时清屏并保留 UI，不录制场景 draw。
RenderCamera 支持透视/正交；当前 Runtime CameraComponent 仍提取为透视。
EditorCameraState 共享 target/clip/projection，独立保存 perspective position/up/FOV 与 orthographic height；
2D 固定 +Z 观察轴，平移同时移动共享 target 和 perspective position，切回 3D 不丢观察方向。

## 两种完成与资源发布

| 机制 | 保护范围 |
| --- | --- |
| FrameSlot fence + retained owners | 当前 graphics submission 使用的资源与 slot 可复用时点 |
| Queue timeline / GpuCompletionPoint | 上传等 submission 的完成身份 |
| present queue idle 回退 | 没有精确 present completion 时，旧交换链的呈现使用 |

slot 数 N 与 swapchain image 数 M 独立；image-available 属于 slot，render-finished 属于 image。
slot 循环索引不是永久 completion 身份。Frame UBO 只有对应 fence 完成后才允许 CPU 改写。
MaterialSet、参数 buffer 和其 Texture/Pipeline/layout/Sampler owner 构成不可变版本；不会原地改写在途版本。
同版本跨 slot 共用，替换/缓存清除后由使用它的 FrameSlot 保留至 fence 完成。
GPU 材质候选创建失败保留旧版本，同候选延迟 60 个 frame serial 再试，新材质 revision 可立即重试；初次失败跳过物体。
retention 只保留真实资源 owner，不接受任意业务回调。

Mesh/Texture 静态工厂先创建完整 GPU owner，再通过 UploadBatch 提交 copy/barrier，保存 ready completion 后返回，
不进行 CPU wait。MaterialRenderer 按 VertexInput/FragmentShader 汇总实际资源的 timeline wait，SceneRenderer 统一提交。
UploadManager pending batch 保留 staging page、CommandContext 与目标 owner，完成后才回收；
staging 增长失败只 abort 自己尚未提交的 batch，不影响其他事务。

VMA memory budget 只在扩展确实启用后使用；估算值不当作硬上限。
强失败创建与 GpuResourceResult 可恢复创建都不发布空句柄成功对象。
资产发布层决定失败保留旧对象，低层工厂只报告错误，不认识 AssetHandle。

ResourceState/ImageState 描述 stage/access/layout/subresource/queue owner，不保存在 Image 的单一 current_layout 中。
Barrier2 描述访问依赖，timeline 描述完成；跨 queue family 需配对 release/acquire 和 semaphore，不能只改 index。

RenderGraph 以资源的固定 image subresource range／buffer byte range 声明 imported 状态，Pass 只引用本图的 ResourceId 和 usage。
compile 不接触设备；按显式顺序生成 Plan，在读前拒绝未初始化资源，编排 layout、RAW/WAR/WAW。
读可见性按 stage/access 成对保存，不错误地做两个独立并集；写入前等待全部 reader。
导出只转换边界状态，不凭空生产内容，final state 保留实际生产者／读者的保守 scope，供下一个 submission 显式 import。
跨提交示例使用同一 graphics queue，靠 barrier 建依赖；需要其他队列时仍须独立的 ownership/semaphore 协议。

Plan::record 先验证全部 binding、区间、图像 usage／aspect、queue family 及别名重叠，再生成原生 barrier 并录制。
绑定的 Image/Buffer 留在 FrameSlot 至完成；Plan 自身不持有 GPU owner。record callback 是同步命令录制入口，调用后不保存，
不是 EventBus 或线程任务。调用方必须保证 callback 的访问与声明一致、开始／结束自己的 RenderPass，并另外保留 Pipeline/Framebuffer 等 owner。
HostRead/HostWrite 只允许作外部 handoff，CPU 仍需等待 GPU completion，不能在 pass callback 中直接读回尚未执行的数据。

Scene 的双 pass Plan 在 setup 时编译一次，resize 只重新绑定当前 slot 的 HDR FrameBuffer attachments。
每 slot 在 fence 完成后可丢弃旧内容，从 Undefined 转到 Color/Depth attachment；RenderPass 内不再隐式转到 ShaderReadOnly。
MSAA resolve 的 initial/final layout 同为 ColorAttachmentOptimal，Graph 在 tone mapping 前转换 HDR 为 SampledRead。
最终 SDR 附件是图外的固定输出：PostProcessRenderer 的 RenderPass 清除、写入、转换到 Present 或 ShaderReadOnly；
不把 RenderPass 已改变的输出 layout 再当作图内未改变的状态。未来更多后处理节点再扩展图的输出边界。
PostProcessRenderer 不拥有 Scene、Window、FrameScheduler；它缓存各 slot 的 HDR View 绑定，View 变化时创建新 descriptor，不覆盖在途 set。
record 时 FrameSlot 保留输出 target、Pipeline、RenderPass、Sampler、layout 及 Binding；Scene 同时保留 HDR target/pass。
resize 先准备 HDR 与 SDR 两套候选，再成对替换；GPU 帧保留旧代，普通 resize 不等待整个 device。
get_render_target/get_offscreen_color_view 始终暴露最终 SDR，HDR 不泄漏到 app/editor 的取图接口。
两种模式只在最终输出 owner 不同；所有 present 输出的 RenderPass 衔接 external→color dependency 和 acquire 等待阶段。
指数 tone mapping 输出线性 SDR；sRGB 附件自动编码，UNORM 输出由 Shader 编码。编辑器纹理沿用窗口编码，避免重复编码或遗漏编码。
fullscreen 正高度 viewport 保留图像上下方向，场景负高度 viewport 仍负责世界坐标转换。
ImageInfo 的 mip_levels/array_layers 会真实进入 Vulkan 创建参数，默认均为 1；这不等于已经实现 Texture 自动生成 mip 或数组采样 View。

## Swapchain 与关闭

交换链重建：等待所有 graphics slot 和 present queue → 释放 runtime/ImGui dependent →
创建 Generation → 重建 per-image state 与 dependent。Editor 离屏 MultiTarget 不因此重建。
extent 变化只重建 target；format/image count 变化还会影响 ImGui backend。
初始 RenderPass 使用实际选定的 surface format，runtime 不兼容格式目前明确终止。

Generation 的 shared ownership 只解决寿命，不保证 WSI 可继续 acquire：
传入 oldSwapchain 调用创建后，无论成功失败旧 core 都退休；调用前移走 active，只由局部 owner 保证旧句柄活到调用结束。
创建或图像枚举失败时没有 active，新候选自动销毁，后续以空 oldSwapchain 重试。无 active 的 acquire 返回 OutOfDate，不调用驱动。
SceneRenderer 用 optional 重建前配置表示暂停呈现；只在首次进入时等待并释放 dependent，失败不恢复旧目标。
begin_frame 在 100 ms 重试间隔内直接返回 false，不 reset fence、不调用 overlay、不录制或提交；旧 scene、资产和 MultiTarget 仍保留。
成功后相对最初配置计算 compatibility，再重建 per-image state／dependent 并清除 pending。显式 recreate 可立即重试。
present 后失败仍正常结束已提交的 FrameSlot；连续两次 acquire OutOfDate 则不开始 FrameSlot。
Queue 使用 Vulkan-Hpp 指针重载返回 Result，避免增强重载在 OutOfDate 时抛异常绕过恢复。
surface/device 丢失、初始创建失败和 runtime 不兼容格式仍明确终止；完整设备／surface 重建见[路线图](../engine-roadmap.md)。

关闭先解绑捕获 Editor/ImGuiContext 的 callback，结束后台工作并等待必要 GPU 完成，再释放：
ImGui dependent → Registry/SceneRenderer → ResourceManager → Swapchain/Device/Context → Window。
Device 必须比 Buffer、Image、Mesh、Texture、completion token 活得更久；shutdown 允许 Device idle。

## Viewport 和拾取边界

ViewPanel 采样 UI、维护 resize debounce 和一次性请求；ViewportLayout 计算逻辑尺寸、物理尺寸、display/visible rect。
实际纹理像素映射采用左上闭、右下开，排除工具栏、留白和 1x 裁切；debounce 中不使用尚未发布的尺寸。
上限取设备 maxImageDimension2D 与 editor 4096 软上限较小值，等比约束。
camera_controller 只做纯数学，不依赖 ImGui。

Mesh 在 GPU 创建前验证顶点并计算只读 local BoundingBox，不保留整份 CPU geometry。
CPU pick 反投影 near/far 射线，变换到局部后测包围盒，方向不再次归一化，保证非均匀缩放下距离参数可比较。
尺寸不符/隐藏丢弃请求，普通 miss 清空 Selection。F 聚焦由 Editor 按事件取最新 world bounds 后调整相机，
不改实体或 Scene Camera。当前没有 GPU readback 或三角形级选中轮廓。
