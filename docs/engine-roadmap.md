# Comet 引擎路线图

更新：2026-09-12。目标是能完成小型 3D 项目的编辑器型引擎，先打通数据和编辑闭环，再扩展渲染与运行时能力。
本文只维护阶段、待办和设计约束，不累计每次迁移的完成日志。

## 当前阶段与下一步

| 阶段 | 状态 | 后续重点 |
| --- | --- | --- |
| 0 基线收束 | 基线已完成 | 持续测试与生命周期回归 |
| 1 Scene/ECS 与渲染提交 | MVP 已完成 | 系统化更新留到阶段 6 |
| 2 序列化与编辑器闭环 | MVP 已完成 | Schema、迁移与项目格式见阶段 7 |
| 3 资产数据库与导入 | 主链路可用，仍有收尾 | 增量引用恢复、任务背压与发布预算 |
| 4 视口与交互 | 4A/4B 主链路完成，4C 进行中 | 缩放工具、内容编辑与撤销扩展 |
| 5 渲染升级 | 未开始整体迁移 | 通用材质、PipelineKey、多 pass、线程边界 |
| 6 游戏运行时 | 规划 | 输入、System、脚本、物理、音频 |
| 7 内容生产与发布 | 项目打开最小入口已落地，其余规划 | 项目设置 UI、格式迁移、打包 |

以当前 main 的功能与验收为准，不再按 feat/auto 提交编号逐个迁移；旧分支仅作为算法、测试及设计参考。
编辑命令历史、帧准备后提取、通用线段绘制、选中包围盒及平移 Gizmo 已接通，后续顺序：

1. **补全内容编辑入口**：组件增删、实体创建／删除、改父级及子树复制已接入撤销；
   Mesh 自动后台导入、右键重导入、拖入场景、Inspector 引用拖放与外部文件拖入 Project 已接通；打开场景的资源准备与补导入后的恢复已补齐。
   变换 Gizmo 的平移／旋转、World／Local 与相对步长吸附已接通，下一步补缩放工具。
   保持编辑、world transform 更新、提取与绘制时序一致。
2. **按需通知事件**：编辑命令入口稳定后，再接真实一对多通知；不预建全局 EventBus，详见阶段 4。

WSI 失败后的无呈现重试仍应独立安排，不与资产编辑工作流捆绑重构。

## 基本边界

- Scene 与持久化数据只保存组件、Entity UUID 和 AssetHandle，不保存路径、GPU handle 或运行时 shared_ptr。
- AssetHandle 是非零 64 位持久资产身份；EntityId 是进程内实体标识，EntityUuid 用于持久定位，二者不混用。
- AssetRegistry 是唯一 Handle → Runtime Asset 缓存。ResourceManager 不建立第二份资产缓存。
- Engine 组合通用能力，app/editor 组合项目工作流；不把 demo bootstrap 或 ImGui 专用类放入 engine。
- 以实际职责、可测试契约和生命周期拆分，不为减少成员数量套结构体，不为对齐设计图增加转发 façade。
- 保持 C++20；通用纯逻辑先有测试，Vulkan 行为通过集成测试和 validation 检查。
- Shader 学习源码保留，但不与生产编译列表、现行 descriptor 协议混淆。
- 所有格式规则以根目录 .clang-format 为准，不在路线图或 AGENTS 重复一套排版细则。

当前实现以[资产管线](architecture/asset-pipeline.md)和[资源所有权](architecture/rendering-ownership.md)为准；
下文的新类型名是目标职责，不意味着立即新增同名文件或类。

## 阶段 3：补全资产工作流

已具备：事务式扫描、稳定 Handle、revision、资产/源文件两类依赖、Texture/Material 加载、Mesh 显式导入与原子 Artifact、
Mesh/Texture 后台 CPU 刷新、Owner Thread 验票发布、源监视、成对移动和错误诊断。
编辑器已支持扫描事件后的 Mesh 自动后台导入及 Project 右键 Reimport；未加载模型生成产物时不创建 GPU 对象。
打开场景及切换 Edit/Play 时按组件元数据准备资源；缺失资源不清空引用、不阻止打开文档。
编辑器从运行时项目路径读取 `project.yaml`，扫描该项目 assets 并复用 Open 读取配置的启动场景；缺失或损坏回退空场景，不覆盖文件。
成功扫描、后台发布及显式纹理修复后合并重查当前场景，Mesh 加载不回退源文件；无请求帧不遍历引用。

待办：

- 当前按事件重查整个场景；大场景按需求增加增量引用索引和受影响范围，不引入每帧轮询。
  任意运行时代码新写入的 Handle 尚不自动建立加载需求，结合阶段 6 Runtime 资源使用契约补齐。
- 大批量刷新增加任务合并、完成队列/主线程发布预算和背压；当前 Worker 数固定，但待处理任务不是有界的。
  同一 Handle 的旧 revision 可丢弃，不应长期占满解码与上传资源。
  当前每次扫描事件会检查所有已索引 Mesh 的缓存；后续结合失败依赖追踪缩小检查范围，不引入每帧产物扫描。
  外部文件复制／校验当前同步执行；大文件工作流后续补异步准备、进度、取消和发布预算。
- TextureArtifact 留到实际扩展纹理导入时一起实现原子发布与 Artifact-only 加载；不先复制 Mesh 的所有中间层。
- Texture 的 wrap/filter、mipmap、压缩必须接通 Sampler、Image 与上传消费者，不能只增加不生效的配置字段。
- glTF 多 mesh 子资产、node transform、material、animation、skin、morph target 需明确导入契约后逐项扩展。
- 补全递归依赖失效、更多资产类型和原生文件事件后端；不把手工编辑 .meta 的失败保留作为当前产品路径。
- 给 Device 提供反映实际启用 features/extensions/queues/limits/formats 的不可变能力集合，不再让使用方拼接来源。
- 非关键 streaming 资源先建立占位、重试、淘汰，再扩展严格预算策略；关键 RenderTarget 保持明确失败。
- Shader 编译产物、缓存键与发布 Manifest 在 metadata/导入协议稳定后设计，Shipping 不带源编译器。

验收：移动不破坏引用；失败不发布半初始化对象；旧任务不能覆盖新结果；大批量操作不长期卡主线程。
Mesh 缓存可删除重建但不替代源资产；Runtime 加载不能隐式回退 glTF 解析。

## 阶段 4：完成编辑交互

已具备：

- 单 Viewport，Edit 独立相机、Play 场景主相机；2D/3D 真正切换投影。
- HiDPI、Free/16:9/固定分辨率、Fit/1x、等比尺寸上限与 resize debounce。
- 完整 MultiTarget 候选替换、FrameSlot retention、ImGui slot 安全更新。
- 屏幕到实际纹理像素映射；环绕/平移/缩放、CPU 包围盒拾取、F 聚焦。
- Swapchain core/dependent 共享所有权与 compatibility diff；重建分别等待 graphics 与 present 使用完成。
- 面板显隐以 EditorPanel 为唯一来源；Project 目录树按扫描快照重建，属性控件显式返回手势状态。
- Inspector 支持可撤销的 Camera／Mesh Renderer 增删；完整组件值快照保留未暴露字段和资产 Handle。
  组件结构修改在属性遍历完成及事务提交后执行；Name／Transform 不开放增删，Play 禁用组件结构编辑。
- Hierarchy 创建／删除子树／改父级通过带文档代次的请求在 UI 后执行，并接入同一历史。
  空白处／Scene 创建根实体，实体右键创建子实体；创建及父子关系合为一条命令，默认本地 Transform。
  子树恢复 UUID、名称、完整组件和父子关系；未知或无恢复协议的组件阻止删除，失败不移动历史。
  不恢复旧 EntityId、选择或展开状态；改父级沿用保留本地 Transform 的语义，Play 禁用结构操作。
- Hierarchy 右键 Duplicate 复用子树快照，为副本分配新 UUID 并重映射内部父级，外部父级和 AssetHandle 保持不变。
  单次 Undo/Redo 覆盖整棵副本；未来自定义组件中的实体引用需另行定义重映射协议，不假定隐藏引用会自动更新。
- CommandHistory 有界历史、UUID 定位及 PropertyEditTransaction；Inspector 注册属性拖动只记录一次，取消恢复。
  实体名称使用同一个 String 属性描述／事务，文本输入结束提交一次，长名称不再被固定缓冲区截断。
  菜单／快捷键请求由 Editor 在 UI 准备后处理；New/Open 成功及 Edit/Play 切换清空历史，Play 修改不记录。
- 编辑器离散快捷键由 `editor-dev.yaml` 配置；绑定匹配与菜单提示共用 EditorShortcuts，保留模式／焦点／输入保护。
  设置界面和用户级覆盖后续接入同一绑定数据，不另建事件总线，也不与阶段 6 游戏 Input 混为一体。
- Mesh/Material 引用与材质纹理槽共用按类型过滤的路径下拉选择；底层保存 Handle，组件引用沿用属性编辑事务。
  Edit 支持 Project 拖入引用框，校验类型、revision 和文档 generation；拖动不改变当前选择。
  组件赋值请求在 UI 后重新校验目标并加载资源，Mesh 只消费已发布 Artifact；失败保留原引用，成功纳入场景历史。
  材质纹理拖放复用资产文件更新与失败回退，不进入场景历史；Play 仅保留下拉引用调试。搜索和资产文件撤销仍留待后续。
- Finder／系统文件管理器可拖入 PNG/JPEG、glTF/GLB，按 Project 落点复制到 assets 根目录或子目录，空目录也可作为目标。
  glTF 保持相对 buffer／图片路径；完整副本校验后无覆盖发布，再扫描生成新身份并复用后台导入，不移动原文件或创建实体。
  重名／缺依赖／复制失败拒绝整批；发布／扫描失败补偿回滚，错误进入 Log。文件操作与场景历史分离。
- Project 模型拖入 Edit Viewport 后，在相机关注平面创建根实体；Transform 与 Mesh Renderer 合为一条撤销命令。
  拖拽载荷以 Handle、revision 和文档 generation 校验身份；放置只加载已有 Artifact，首次导入未完成时不创建实体。
  新实体材质引用暂留空，由 Inspector 手动指定；不解析 glTF 材质，不提供放置预览或表面吸附。
- Engine 在 Renderer::prepare_frame 完成 UI 准备之后读取活动 Scene 并提取，随后 render_frame；不新增快照 provider 回调。
- LineDrawList 接收单帧线段/包围盒；执行器使用场景 pass、相机和 MSAA，正常深度测试且不写深度。
  CPU 请求不依赖 Vulkan/ImGui；slot 独立 vertex buffer 安全复用，扩容失败跳过调试批次并延后重试。
- Edit 选中 Mesh 的局部包围盒八角点经过 world transform 后连十二条边；普通帧在 UI 命令完成后提交，
  点击帧在拾取结果更新 Selection 后提交，不画旧选择；Scene/Mesh/Material 不保存 selected 状态。
- TransformGizmo 支持平移／旋转、世界／本地轴、透视／正交投影和逻辑屏幕命中；父变换逆矩阵将世界位移转成本地 translation。
  Tool 菜单提供 Mode、Space、Snap 与距离／角度步长；吸附相对拖动起点，不修改项目或场景配置。
  旋转使用圆环命中、连续角度累计与矩阵合成；World 非均匀父级明确拒绝，Local 保留真实仿射基。
  与 Inspector 共用事务类型和历史，但不共用活动事务；释放提交一次，Escape／失焦／上下文变化取消。
  操作箭头由 ViewPanel 绘制为 UI 覆盖层，拖动预览先于当前帧 Scene 提取，渲染层不认识 Gizmo。

剩余：

- Gizmo 后续增加缩放；持续验证当前帧快照一致性。
  多 pass outline 留到阶段 5，不与包围盒反馈混淆。
- 搜索、跨场景复制粘贴、Prefab MVP；资产修改需独立定义文件事务，不与场景历史混用。
- 按大场景实际使用测量撤销快照内存；当前仅限制历史条数，巨大子树的内存预算后续按需求完善。
- Project 缩略图、搜索和资产创建；与阶段 3 导入入口共用事务服务。
- 外部导入后续按需支持整目录、重名交互、含 `..` 的依赖布局和更多格式；现在明确拒绝，不隐式改名或覆盖。
  文件批次当前依赖同卷硬链接与进程内补偿回滚，不保证整批崩溃原子性；后续补恢复记录及跨卷发布策略。
  原生 Finder 拖放手工验收与 Windows 平台体验持续补充；自动测试覆盖回调所有权、落点和事务，不等于操作系统端到端验收。
- Runtime Camera 的投影设置应通过场景组件/Inspector 表达，不让 Edit 的 2D/3D 开关影响 Play。
- Runtime 输入单独路由；有真实需求才增加 Eject/Debug Camera 或多 Viewport。
  多个同时可见视口必须各自拥有 Camera、目标尺寸、FrameSlot 目标和提交；隐藏时跳过场景渲染。
- CPU Pick 保持按点击事件线性测试包围盒。大型场景先测量，再评估可供拾取与视锥裁剪共享的空间索引；
  需同时计入实体、Transform 和 Mesh 变更的维护成本。加速不等于提高拾取精度。
- 三角形级精度按需求评估；GPU ID/readback 和多 pass outline 留到阶段 5。
  真正引入 readback 时需在对应 fence/timeline 完成后 invalidate/read，不预建无人使用的系统。

验收：坐标、拾取、Gizmo 和高亮在 DPI/resize/裁切后保持一致；一次编辑手势对应一次撤销，保存后可恢复场景。

### 命令与通知编排

- 先结合 Command History 收敛菜单、快捷键、Inspector/Gizmo 的编辑入口，明确唯一执行者、返回结果和事务边界。
  可撤销的场景修改进入历史；保存、刷新、重导入等服务操作不因入口统一就强行加入 Undo/Redo。
- 命令入口稳定后，仅对资产发布、选中对象变化、活动场景切换等真实一对多通知引入类型化事件。
  事件表达已提交的事实，不代替有返回值的命令或查询；不把整个编辑事务改成订阅者之间的隐式调用链。
- 通知通道由所属 Editor/服务持有，不预建全局 EventBus。明确派发阶段、订阅连接的 RAII 解除和重入规则；
  跨线程结果回到 owner 线程处理，载荷使用稳定 ID/revision 或自有数据，不保存组件裸指针。
- Overlay prepare/render、交换链 release/rebuild 保留有序生命周期钩子，不混入业务事件。
  场景 getter/replacer 属于访问与所有权协议，属性编辑函数属于策略回调，不能为减少回调数量硬改成事件。
- 与场景快照时点一起验证：修改在预定阶段提交，Transform 更新后再提取；失败不广播成功通知，
  面板销毁或 Scene 更换后无失效订阅，同一次提交不重复执行或重复记录历史。

## 阶段 5：渲染架构升级

### 材质与 Shader

当前固定 unlit_texture_blend pipeline、两张 Texture、u_Texture0/1 和逐帧属性解析都是 MVP 约束，不能只把 array 换成 vector。

- 接通引擎内置基础材质，供新模型未指定材质时自动使用；项目描述不配置 default_material。
  基础材质不依赖 demo 的纹理或材质文件，内置资源有稳定身份／解析入口和明确生命周期，不在编辑器内写死临时 Handle。
  自动默认只针对未指定材质；显式引用丢失仍保留身份并报告错误，不用默认材质掩盖坏引用。
- Shader 源分为引擎内置（engine/shaders）与项目自定义（项目 assets/shaders）；前者随引擎只读提供，后者随项目版本控制。
  两者最终复用程序描述、编译／反射接口和渲染消费者；区分来源与身份，不各造一套 Shader/Pipeline 系统。
  项目 Shader 的产物写入项目 .comet/cache，内置 Shader 使用引擎构建／安装产物；项目不得通过同名文件隐式覆盖内置资源。
  项目只引用内置公开契约，不包含引擎源码绝对路径；私有渲染 pass 的 Shader 不必作为用户可选材质资产暴露。

1. SceneResolver 只解析 Mesh/Material，不知道材质属性名、纹理数量或 binding。
2. 建立 material/layout revision、手工 MaterialLayout 与渲染侧 MaterialRuntimeCache。
   材质资产保存 layout/template 和参数 Handle；缓存解析资源并生成 PreparedMaterial。
3. 按 Frame / Material / Object 分层：FrameSet 按 slot；MaterialSet 按 revision 创建不可变版本并跨 slot 复用；
   model matrix/object ID 可继续用 push constant，只有 per-frame backing 参数单独维护 slot state。
4. Render Queue 按 pipeline/material 排序，至少验证两种布局及纹理、标量、向量参数。
5. 再引入 SPIR-V reflection 生成 ShaderInterface（set/binding/type/count/stage/push constants）。
   显示名、默认值、颜色/法线语义和 Inspector 范围仍由 Material metadata 提供；不与 C++ 反射混淆。
6. Material Inspector 按布局生成控件，变化时精确失效缓存；当前不引入 bindless。

目标编辑流程：项目 Shader 源码及程序描述进入资产管线，描述组合 vertex/fragment 等阶段与入口；
编译与反射产出可用程序和参数布局，材质按稳定资产引用选择程序／模板，Inspector 按布局显示纹理槽及其他参数。
反射只负责类型和 binding，名称、默认值、用途与编辑范围由材质 metadata 补充，不把任意单个 GLSL 文件当完整渲染方案。
切换程序时保留兼容参数，对缺失或类型变化给出默认值／诊断；编译失败不替换当前有效版本。

Shader 源码、CPU 编译结果和 Vulkan 对象分层；build-time/editor 编译共用 stage、entry、defines/variants、target 和依赖契约。
Editor-only 热加载按 debounce → Worker 编译/reflection → revision 验票 → owner 帧边界切换。
接口兼容时换 Pipeline；接口变化时同时重建 Layout 并失效材质缓存。失败保留旧版本并输出文件/行号诊断；
成功也不能提前释放在途帧引用的 Shader/Pipeline/Layout。Shipping 只消费预编译打包数据，不要求松散 .spv。

### Pipeline 两级缓存

- Engine PipelineKey 包含 shader 身份/revision/entry/specialization、layout、vertex/topology、raster/depth/blend/dynamic state、
  attachment formats/sample count/subpass。名称仅作标签，hash 索引后必须完整相等比较；旧 key 对象按 last use 释放。
- 驱动 PipelineCache blob 用于跨进程加速，不代替对象 key。放在 .comet/cache/vulkan 或平台缓存，
  校验 header size/version、vendorID、deviceID、pipelineCacheUUID，以及 envelope 长度/校验和。
  损坏或不兼容回退空 cache，不影响启动。
- 先做结构化 key，再接热加载，最后加 cache load/atomic save；编译批次后节流或关机保存，不每帧写磁盘。
  Pipeline 创建/合并/保存由同一 owner 串行访问；后台 ShaderCompiler 不直接操作 Vulkan cache。
- 测试 key 等价性、兼容性和损坏输入；cold/warm 性能只做测量，不要求固定加速比例。

### GPU 资源、同步与 WSI

现有 Device、FrameScheduler、UploadManager 继续演进，不为目标名称再包一层。

- Device 管 logical device、queues、allocator、能力和设备缓存，不拥有所有资产、FrameSlot 或业务 target。
- FrameSlot 管复用时点：等待 completion → 释放 retained owners → reset command/descriptor/transient arenas。
  CommandPool 按 slot + recording thread + queue family 隔离；长期材质和 swapchain image 状态不放入 slot。
- Descriptor 分 frame arena、persistent arena 和 editor-owned ImGui pool；分页 pool、object ring/dynamic UBO/SSBO
  以真实规模与测量为依据，offset 遵守设备 alignment，不能覆盖 in-flight storage。
- UploadManager 显式 batch、staging 子分配、timeline completion、ready wait 的边界保留。
  后续合并跨资产批量、细化 ring 回收；独立 transfer queue 必须补齐 ownership release/acquire 与 semaphore。
- 单帧资源用 fence + RetainedResources；只有跨 submission/queue 的真实需求才引入通用退休队列。
  依据实际 last-use completion，不使用“延迟 N 帧”或循环 slot index 猜完成；不允许混入任意业务 callback。
- waitIdle 可用于 shutdown、设备恢复与平台回退，不应成为常规资产替换机制；graphics fence 不代表 present 已完成。
- 运行时 format/sample-dependent RenderPass/Pipeline 需与 target 形成兼容、可替换的 generation；
  当前不兼容格式仍明确终止，不能继续绑定旧 Pipeline。
- **WSI 无呈现恢复**：传入非空 oldSwapchain 调用创建后，无论成功失败，旧交换链都已退休。
  当前最小安全策略是新建失败明确终止；只有创建调用前的零尺寸延期可以恢复旧 dependent。
  后续设计 no-present/retry/surface-lost 状态，禁止从退休对象 acquire，禁止把它再次作为非退休 oldSwapchain。
  旧资源仍须等待 graphics/present completion，再按 framebuffer → view → swapchain 顺序释放。
  规则来源：[Khronos](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html)。

### RenderGraph 与多 pass

- Pass 声明读写 usage/subresource；imported/exported 资源明确边界状态，tracker 编译 Barrier2。
  Image 不保存单一全局 current_layout；状态属于录制/编译上下文，持久资源在提交边界交接 handoff state。
- 处理 layout 变化、RAW/WAR/WAW 和 ownership，兼容 read-after-read 不机械加全 barrier。
  跨 queue family 要成对 release/acquire + semaphore；未知输入状态或绕过 tracker 的操作必须明确声明或拒绝。
- Synchronization 2 / Timeline 已启用；API version 为 1.3 不代表所有可选 feature 自动启用。
- Dynamic Rendering 在真实多 pass/attachment 需求下评估，不为 API 更换重写阶段 4。
  检查显式 feature、ImGui/MSAA/resize、调试工具和目标 GPU；可按 pass 保留传统 RenderPass。
- Forward Lighting → LightComponent（方向/点/聚光）→ shadow → PBR → tone mapping/gamma/bloom。
  先完成小型 forward 场景，不一次构建完整 deferred renderer。
- 低频采样 GPU memory budget，详细 allocation dump 手动触发；补足 CPU/GPU frame-time 诊断。

### 线程演进

当前 Main 拥有 Scene、UI 和 GPU 可变状态；Worker 只产出 CPU 数据。独立 RenderThread 不是阶段 4 前置任务。

| Owner | 职责 | 边界 |
| --- | --- | --- |
| Main/Update | Scene、脚本、Editor command、ImGui 构建 | 不并发写 GPU cache |
| Worker | I/O、解码、编译、纯 CPU 工作 | 不访问 Scene/ImGui，不提交 Queue |
| RenderThread（未来） | GPU cache、录制、submit/present、退休 | 不读取可变 EnTT/面板 |

- Main 侧 RenderSystem 编排稳定时点 extraction，生成 owned RenderFramePacket（serial、views、资源 revision、UI draw data）。
  不把借用 ImDrawData 指针直接排队；需要深拷贝或先保留 UI 主线程执行。
- 队列容量不超过允许的 frames-in-flight，拥塞用显式等待/淘汰过期 editor packet 等策略处理。
- GPU 创建、替换、销毁在 owner 安全边界；队列外部同步、缓存与 completion owner 都必须明确。
- 先 profile，再并行 culling/sorting/preparation 或 secondary command recording；每 worker 独立 CommandPool。
- 关机先停止生产并 drain/cancel jobs，join 渲染线程，等待必要 GPU 完成，销毁资源，最后 Window/Logger。
  单 Worker/单线程基线始终可供确定性测试。

验收：多布局材质与多 pass 正确；Shader 更新失败不破坏旧帧；队列有界；没有并发访问可变 Scene/ImGui/Vulkan owner；
validation、同步测试和生命周期回归通过。

## 阶段 6：游戏运行时

- Input（键鼠/手柄）、Fixed Update 与普通 Update、Native Script 生命周期和字段暴露。
- EditorMode 只含 Edit/Play；RuntimeState（Running/Paused）与之正交，支持暂停/单步，不增加 EditorMode::Paused。
- TransformSystem 收口 Inspector/Gizmo/脚本修改，以 dirty 集合增量更新受影响子树。
  当前全量计算保留作正确性对照；实体/UUID 索引一起评估，不只在 getter 加缓存而漏失效。
- 任务并行必须声明组件读写集合和 phase/barrier，不任意并发执行脚本回调。
- 物理（候选 Jolt/Bullet）、音频（候选 miniaudio）、动画/AI 与 Runtime UI；引入依赖前按实际 demo 需要评估。
- 脚本、Inspector、Serializer、Undo/Redo 共用稳定 ComponentDescriptor/PropertyDescriptor。
  类型/字段 ID 不用 typeid 名称或裸 offset；使用类型化访问器，并区分 editable/serializable/transient 等属性。
- C++26 反射仅在所有目标工具链、标准库与依赖验证通过后评估；只替换 descriptor 生成后端，不重写业务消费者。

验收：角色移动、碰撞、声音的小 demo 可运行，Play/Edit 隔离稳定；固定输入/时间步与单线程回退可重复。

## 阶段 7：项目格式与发布

- 已提前补齐最小项目入口：`project.yaml` 保存版本、名称和可选启动场景；编辑器接受项目目录／描述文件路径。
  项目 roots、资产索引、缓存、布局及 SceneDocument 均绑定同一项目；相对场景路径基于 assets，拒绝越界 Open/Save。
  无参数打开仓库 `demo/` 内的独立示例项目，显式无效项目不回退示例；空启动场景创建空文档，不硬编码示例资源。
- 编辑器内增加 File → Open Project，与现有 Open Scene 分开；选择目录或 project.yaml，并提供最近项目列表。
  切换前处理未保存场景和活动属性／Gizmo 编辑，Play 模式先退出；取消或新项目校验失败时保持当前项目不变。
  第一版可通过重启编辑器进程打开新项目，避免直接交换活动 AssetManager；若支持原地切换，须先排空旧任务和在途帧，
  再释放旧场景／选择／历史／资产缓存与监视器，保存旧布局并加载新布局，禁止旧项目结果发布到新项目。
  验收：无需命令行即可选项目；取消／失败不丢修改；同 Handle 的两个项目不串用资源或缓存。
- 后续扩展项目设置 UI、记录上次文档、Build Settings 和项目模板，去掉发布对源码目录的依赖。
  让 app 读取项目场景，替换当前独立代码示例；编辑器／引擎自带 Profile、字体和 Shader 与项目内容保持分离。
- 运行配置保持人工友好的 YAML。编辑器成为规范写入入口、Schema 稳定后，将 .scene/.mat/.meta 与项目描述
  整体迁移为确定性 JSON，配套版本迁移与工具；不因 fastgltf 间接带入 simdjson 就局部替换格式。
  JSON 需 Comet 自己的直接依赖/读写边界，不暴露 fastgltf 私有依赖。
- .comet/cache 继续存二进制派生产物与索引；Shipping Manifest 只含运行时身份、依赖和打包位置，
  不带松散 .meta、源资产与 editor importer 配置。
- 另存为、自动保存、崩溃恢复和日志目录规范；项目设置面板、打包播放器、CI 构建测试打包。
- 开发期 Scene Schema 不承诺兼容；冻结版本前明确迁移规则，不默默读入不兼容数据。

验收：示例可从编辑器打包成独立程序，不依赖仓库源路径；新贡献者能按 README 初始化与验证。
