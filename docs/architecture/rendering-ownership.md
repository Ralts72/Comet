# 渲染资源所有权

描述当前 owner、调用边界和销毁规则；未来 MaterialLayout/RenderGraph/RenderThread 设计见[路线图](../engine-roadmap.md)。

## 先看哪个类

| 入口 | 职责 |
| --- | --- |
| `core/engine.h` | 组合 Window、Scene、TaskScheduler、AssetRegistry 和 Renderer，驱动主循环 |
| `render/renderer.h` | 渲染子系统组合根，编排帧、RenderView、overlay 与拾取 |
| `render/scene/scene_extractor.h` | Scene → 不含 GPU 对象的 RenderScene 快照 |
| `render/scene/scene_resolver.h` | Handle/Camera → RenderSubmission |
| `render/scene/scene_renderer.h` | Target、Pipeline、材质 descriptor 与命令录制 |
| `render/frame_scheduler.h` | FrameSlot 复用、image 关联、完成序号与 retention |
| `render/resource/resource_manager.h` | 设备资源工厂、上传及 Shader/Sampler 共享资源 |
| `graphics/` | Vulkan 对象与显式同步后端 |
| `editor/src/imgui_context.h` | 编辑器 UI 最终呈现和私有纹理绑定，不属于 engine |

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
    └── SceneRenderer
        ├── RenderPass / PipelineManager / Pipeline
        ├── FrameScheduler → FrameSlot[N] / SwapchainImageState[M]
        ├── ViewProjectBuffer[N]
        ├── RenderTarget：runtime SwapchainTarget 或 editor MultiTarget
        └── MaterialDescriptorState[material][slot]

Editor
├── AssetManager（借用 Engine 的服务）
├── EditorState / SceneDocument / EditorSceneSession / SelectionService
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
  → 按请求 CPU pick → scene pass
  → overlay render（录制已生成的 ImGui 数据）
  → submit / present
```

完整数据链为 `Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
SceneRenderer 不读 EditorMode/ImGui。SceneResolver 当前仍有固定两纹理材质规则，阶段 5 才解除，不把它描述成已通用化。

只有 prepare_frame 成功才提取并提交；overlay prepare 可以修改或替换 Scene，Engine 在其返回后重新读取 owner。
Renderer 不接收 Scene getter/provider，仍只消费 owned RenderScene；不持有可变 Scene 或 EnTT 引用。
编辑命令完成后提取，因此组件修改、Undo/Redo 和当前帧拾取使用同一份场景快照。

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
slot 循环索引不是永久 completion 身份；材质 descriptor 缓存回收使用单调 frame serial。
当前 UBO 与材质 descriptor 按 slot 更新，只有对应 fence 完成后才允许 CPU 改写。
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

## Viewport 和拾取边界

ViewPanel 采样 UI、维护 resize debounce 和一次性请求；ViewportLayout 计算逻辑尺寸、物理尺寸、display/visible rect。
实际纹理像素映射采用左上闭、右下开，排除工具栏、留白和 1x 裁切；debounce 中不使用尚未发布的尺寸。
上限取设备 maxImageDimension2D 与 editor 4096 软上限较小值，等比约束。
camera_controller 只做纯数学，不依赖 ImGui。

Mesh 在 GPU 创建前验证顶点并计算只读 local BoundingBox，不保留整份 CPU geometry。
CPU pick 反投影 near/far 射线，变换到局部后测包围盒，方向不再次归一化，保证非均匀缩放下距离参数可比较。
尺寸不符/隐藏丢弃请求，普通 miss 清空 Selection。F 聚焦由 Editor 按事件取最新 world bounds 后调整相机，
不改实体或 Scene Camera。当前没有 GPU readback、选中线框或 Gizmo。
