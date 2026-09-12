# 渲染资源所有权

描述当前 owner、调用边界和销毁规则；未来 Shader 热更新/RenderGraph/RenderThread 设计见[路线图](../engine-roadmap.md)。

## 先看哪个类

| 入口 | 职责 |
| --- | --- |
| `core/engine.h` | 组合 Window、Scene、TaskScheduler、AssetRegistry 和 Renderer，驱动主循环 |
| `render/renderer.h` | 渲染子系统组合根，编排帧、RenderView、overlay 与拾取 |
| `render/scene/scene_extractor.h` | Scene → 不含 GPU 对象的 RenderScene 快照 |
| `render/scene/scene_resolver.h` | Handle/Camera → RenderSubmission |
| `render/scene/scene_renderer.h` | Target、pass、帧与呈现生命周期编排 |
| `render/material_renderer.h` | Frame/Material descriptor、材质 Pipeline、队列排序与 Mesh 绘制 |
| `render/material_runtime.h` | 手工 MaterialLayout、PreparedMaterial 快照与版本缓存 |
| `graphics/pipeline/shader_interface.h` | 入口级 SPIR-V 反射结果与绑定覆盖校验；仅拥有 CPU 值 |
| `render/frame_scheduler.h` | FrameSlot 复用、image 关联、完成序号与 retention |
| `render/line_draw_list.h` | 通用 CPU 线段列表；`render/debug/debug_renderer.h` 是当前 GPU 消费者 |
| `render/resource/resource_manager.h` | 设备资源工厂、上传及 Shader/Sampler 共享资源 |
| `graphics/` | Vulkan 对象与显式同步后端 |
| `editor/src/viewport/viewport.h` | 组合 ViewPanel/Gizmo，连接编辑器相机、选择反馈与 Renderer |
| `editor/src/ui/imgui_context.h` | 编辑器 UI 最终呈现和私有纹理绑定，不属于 engine |

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
        ├── RenderPass / PipelineManager
        ├── FrameScheduler → FrameSlot[N] / SwapchainImageState[M]
        ├── DebugRenderer → 线段 Pipeline / VertexBuffer[slot]
        ├── RenderTarget：runtime SwapchainTarget 或 editor MultiTarget
        └── MaterialRenderer
            ├── PipelineState → MaterialLayout / descriptor layouts / Pipeline
            ├── FrameResources[slot] → 相机 UBO / FrameSet / pool
            ├── MaterialRuntimeCache → PreparedMaterial → Texture / 参数字节
            └── MaterialResources[material version] → PreparedMaterial / PipelineState / Sampler / 参数 UBO / pool / MaterialSet

Editor
├── EditorAssets → AssetManager（借用 Engine 的服务）
├── EditorState / SceneDocument / EditorSceneSession / SelectionService
├── CommandHistory ← Inspector / TransformGizmo 各自的属性事务
├── Viewport → ViewPanel / TransformGizmo（借用状态、选择、Renderer、Registry 和 ImGuiContext）
└── ImGuiContext
    ├── RenderPass / SwapchainTarget / DescriptorPool
    └── TextureBinding[slot] → ImageView / Sampler / ImGui descriptor
```

- 引用表示必需且不可重绑定的借用；指针用于可空、可换 owner 或 moved-from 状态。
  unique_ptr 独占，shared_ptr 延长共享寿命；原生 Vulkan/GLFW handle 仍遵守各自协议。
- Renderer 是组合根，不是所有 GPU 对象的直接 owner；Device 也不反向拥有业务服务。
- app/editor 的 AssetManager 先于 Engine 销毁；后台任务先结束，GPU 使用完成后再释放 Registry 和渲染资源。

## 应用启动与失败清理

Application 的实现集中在 runtime.cpp，对外只提供完整的 run(Config) 生命周期：
创建 Diagnostics／Engine → on_init → 引擎更新循环 → end。start/main_loop 不再作为可独立调用的接口。
初始化和更新失败共用一个捕获边界；on_init 一旦开始，就会尝试一次 on_shutdown，应用必须能关闭部分初始化的成员。
end 是内部操作，提前消费关闭标记，保证关闭钩子自身抛错后不会再次调用。
钩子失败时保留 Engine／Diagnostics，由应用析构先释放派生类剩余资源、再释放基类 owner；
原始初始化／更新错误继续向上传递，清理错误单独报告。关闭失败的实例不能重新运行。

ImGuiContext 的原生 Context 由带私有 ContextDeleter 的 unique_ptr 拥有；
它最后声明，因此构造失败时最先析构，先关闭借用 GPU 资源的后端，再析构 pool／target。
不需要在构造函数中 catch 后 cleanup/rethrow。正常析构仍先等待 GPU、解除纹理注册，再销毁后端及资源。
只关闭实际存在的后端，覆盖 swapchain 重建中旧后端已经关闭的状态。
等待 GPU 时的 catch 保留：它保护 noexcept 清理边界，不等同于设备丢失恢复。

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
Engine：事件 → Application 更新
  → Renderer::prepare_frame
      回收完成的 upload → 等待 slot / acquire / 开始录制
      → overlay prepare（UI、编辑命令、相机输入、最新 RenderView）
  → SceneExtractor（读取此时的活动 Scene，更新 world transform）
  → Renderer::render_frame
  → SceneResolver（使用实际 Target 尺寸）
  → 按请求 CPU pick → scene pass（场景物体 → DebugRenderer）
  → overlay render（录制已生成的 ImGui 数据）
  → submit / present
```

完整数据链为 `Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
SceneRenderer 不读 EditorMode/ImGui。SceneResolver 只解析 Camera、Mesh 和 Material 引用，不检查模板、属性名或纹理数量。
MaterialRenderer 使用 MaterialRuntimeCache，按 Material 对象身份/revision 与不可变 MaterialLayout 对象身份生成 PreparedMaterial。
内置布局由 MaterialLayout::find_builtin 共享，不由各个 Renderer 重复构造；Inspector 读取同一份默认值、槽名和编辑语义。
布局只含 CPU 描述，不含 ImGui 控件或 GPU owner；显示名、颜色语义、编辑范围是人为元数据，不由 Shader 反射自动推断。
失败也缓存，在持续使用期间不逐帧重复诊断；源或布局变化后重试。未使用的 CPU 缓存按渲染周期回收。
PreparedMaterial 持有当时的 Texture 引用与按布局打包的参数。set 0 是按 slot 更新的相机 FrameSet；
set 1 是按材质版本创建、发布后不改写的 MaterialSet；model matrix 仍使用 push constant。
MaterialResources 保留 PreparedMaterial、Pipeline/layout、Sampler、参数 buffer 和 descriptor pool，
实际使用它的 FrameSlot 再保留该整体与 Mesh、FrameResources，直到 GPU 完成；CPU 缓存回收不代表 GPU 完成。
GPU 候选创建失败时继续使用旧 MaterialResources，同一候选延后 60 个 frame serial 重试；新候选可立即尝试。
不支持的模板或 CPU 准备失败仍跳过绘制，不承诺任何失败都沿用旧材质。
当前队列按模板名和材质 Handle 排序，支持 unlit_texture_blend 与 unlit_color；完整 PipelineKey 尚未接通。

Shader 先从指定入口的 SPIR-V 生成自有 ShaderInterface，再创建设备 shader module；反射库和输入字节码不被结果借用。
DescriptorSetLayout 保存原始 binding 描述；ShaderLayout 检查 descriptor 类型/数量/stage 与 push constant 覆盖范围。
PipelineManager 在名称缓存查询前执行校验，MaterialRenderer 额外用 MaterialLayout 核对材质 set 的参数块与纹理协议。
ShaderInterface 只公开 Comet 的 Format、DescriptorType、ShaderStage 和自有范围值，不依赖 Vulkan 头文件。
反射库类型在 shader_interface.cpp 内显式转换；Vulkan 类型对照留在 ShaderLayout::validate 的实现中，材质层只消费 Comet 描述。
当前同步反射，不自动生成 MaterialLayout，不新增热更新线程或事件；预检只检查字节码头与指令长度，不是完整 SPIR-V validator。

只有 prepare_frame 成功才提取并提交；overlay prepare 可以修改或替换 Scene，Engine 在其返回后重新读取 owner。
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

Viewport 拥有 ViewPanel 和 TransformGizmo，借用 EditorState、Selection、Renderer、AssetRegistry 和 ImGuiContext；
每次更新显式接收当前 Scene，不另存活动场景指针。Editor 负责挂接和解除帧回调、场景重绑以及跨面板命令。
TransformGizmo 是编辑器侧的投影、命中与平移／旋转／缩放事务，不是渲染资源。它与 Inspector 各自持有 PropertyEditTransaction，
共享同一个 CommandHistory；拖动用 UUID 定位，按模式预览 translation、rotation 或 scale，释放提交一次，取消恢复。
ViewPanel 优先将普通左键交给 Gizmo，未命中才请求场景拾取；拖动时占有 ImGui active ID，阻止快捷键和相机导航。
UI 回调完成命令／相机更新后，ViewPanel::draw_gizmo 将最新句柄追加到本帧窗口 draw list，随后 ImGui::Render。
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

Mesh/Texture 静态工厂先创建完整 GPU owner，再通过 UploadBatch 提交 copy/barrier，保存 ready completion 后返回，
不进行 CPU wait。SceneRenderer 按 VertexInput/FragmentShader 汇总实际资源的 timeline wait。
UploadManager pending batch 保留 staging page、CommandContext 与目标 owner，完成后才回收；
staging 增长失败只 abort 自己尚未提交的 batch，不影响其他事务。

VMA memory budget 只在扩展确实启用后使用；估算值不当作硬上限。
强失败创建与 GpuResourceResult 可恢复创建都不发布空句柄成功对象。
资产发布层决定失败保留旧对象，低层工厂只报告错误，不认识 AssetHandle。

ResourceState/ImageState 描述 stage/access/layout/subresource/queue owner，不保存在 Image 的单一 current_layout 中。
Barrier2 描述访问依赖，timeline 描述完成；跨 queue family 需配对 release/acquire 和 semaphore，不能只改 index。

## Swapchain 与关闭

交换链重建：等待所有 graphics slot 和 present queue → 释放 runtime/ImGui dependent →
创建 Generation → 重建 per-image state 与 dependent。Editor 离屏 MultiTarget 不因此重建。
extent 变化只重建 target；format/image count 变化还会影响 ImGui backend。
初始 RenderPass 使用实际选定的 surface format，runtime 不兼容格式目前明确终止。

Generation 的 shared ownership 只解决寿命，不保证 WSI 可继续 acquire：
传入 oldSwapchain 调用创建后，无论成功失败旧 core 都退休。本阶段失败明确终止；
只有调用创建前的零尺寸延期才允许恢复旧 dependent；创建失败后的无呈现恢复见[路线图](../engine-roadmap.md)。

关闭先解绑捕获 Editor/ImGuiContext 的 callback，结束后台工作并等待必要 GPU 完成，再释放：
ImGui dependent → Registry/SceneRenderer → ResourceManager → Swapchain/Device/Context → Window。
Device 必须比 Buffer、Image、Mesh、Texture、completion token 活得更久；shutdown 允许 Device idle。

GLFW 由 Window 实现管理：首个窗口初始化，最后一个窗口释放后终止，创建／销毁在主线程执行。
原生窗口由 unique_ptr 与私有 deleter 持有，构造过程中取得窗口后发生异常也会释放。
Engine 不直接初始化 GLFW；普通调用方使用 `request_close()` / `is_minimized()`。
`Window::get()` 仅借出句柄给 Vulkan Surface、ImGui 后端和底层测试，不转移所有权；
调用者不得自行销毁句柄或在 Comet 窗口存活时终止 GLFW。独立的非 Comet 窗口生命周期暂不纳入管理。
GLFW 使用共享库，避免 Engine 动态库与 ImGui／测试各自静态链接一份全局状态；PRIVATE 链接仅控制接口传播，不代替这一运行时约束。

## Viewport 和拾取边界

ViewPanel 采样 UI、维护 resize debounce 和一次性请求；ViewportLayout 计算逻辑尺寸、物理尺寸、display/visible rect。
实际纹理像素映射采用左上闭、右下开，排除工具栏、留白和 1x 裁切；debounce 中不使用尚未发布的尺寸。
上限取设备 maxImageDimension2D 与 editor 4096 软上限较小值，等比约束。
camera_controller 只做纯数学，不依赖 ImGui。

Mesh 在 GPU 创建前验证顶点并计算只读 local BoundingBox，不保留整份 CPU geometry。
CPU pick 反投影 near/far 射线，变换到局部后测包围盒，方向不再次归一化，保证非均匀缩放下距离参数可比较。
尺寸不符/隐藏丢弃请求，普通 miss 清空 Selection。F 聚焦由 Editor 按事件取最新 world bounds 后调整相机，
不改实体或 Scene Camera。当前没有 GPU readback 或三角形级选中轮廓。
