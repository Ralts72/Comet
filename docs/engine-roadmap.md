# Comet 引擎路线图

更新：2026-09-07。目标是能完成小型 3D 项目的编辑器型引擎，先打通数据和编辑闭环，再扩展渲染与运行时能力。
本文只维护阶段、待办和设计约束，不累计每次迁移的完成日志。

## 当前阶段与下一步

| 阶段 | 状态 | 后续重点 |
| --- | --- | --- |
| 0 基线收束 | 基线已完成 | 持续测试与生命周期回归 |
| 1 Scene/ECS 与渲染提交 | MVP 已完成 | 系统化更新留到阶段 6 |
| 2 序列化与编辑器闭环 | MVP 已完成 | Schema、迁移与项目格式见阶段 7 |
| 3 资产数据库与导入 | 主链路可用，仍有收尾 | Mesh 导入 UI、Artifact 状态、任务背压 |
| 4 视口与交互 | 4A/4B 主链路完成，4C 进行中 | 扩展撤销范围、旋转／缩放工具 |
| 5 渲染升级 | 未开始整体迁移 | 通用材质、PipelineKey、多 pass、线程边界 |
| 6 游戏运行时 | 规划 | 输入、System、脚本、物理、音频 |
| 7 内容生产与发布 | 规划 | 项目设置、格式迁移、打包 |

以当前工作分支的功能与验收为准，不再按 feat/auto 提交编号逐个迁移；旧分支仅作为算法、测试及设计参考。
编辑命令历史、帧准备后提取、通用线段绘制、选中包围盒及平移 Gizmo 已接通，后续顺序：

1. **完善内容编辑入口**：实体／组件增删、名称及层级已支持撤销，接下来实现 duplicate 和资产拖拽等闭环。
   保持修改、world transform 更新、提取与绘制的时序一致；结构修改不能简单套属性快照。
2. **按需通知事件**：编辑命令入口稳定后，再接真实一对多通知；不预建全局 EventBus，详见阶段 4。

另有两项应独立安排：阶段 3 的 Mesh 导入 UI/状态，以及 WSI 失败后的无呈现重试。
前者是工作流缺口，后者是可恢复性缺口，不用一个大重构捆绑完成。

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

待办：

- Project 面板支持 Mesh 导入/重导入，展示 Artifact 缺失、过期、就绪和失败状态。
- 大批量刷新增加任务合并、完成队列/主线程发布预算和背压；当前 Worker 数固定，但待处理任务不是有界的。
  同一 Handle 的旧 revision 可丢弃，不应长期占满解码与上传资源。
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
- CommandHistory 有界历史、UUID 定位及 PropertyEditTransaction；Inspector 注册属性拖动只记录一次，取消恢复。
  实体名称使用同一个 String 属性描述／事务，文本输入结束提交一次，长名称不再被固定缓冲区截断。
  Inspector 添加／移除可选组件经 SceneCommands 进入同一历史；UUID 定位，完整值快照恢复，Play 不开放结构编辑。
  Hierarchy 创建／删除子树／重设父级提交带 generation 的请求，Editor 结束手势后执行；子树恢复保持 UUID，重建内部 EntityId。
  删除前校验描述符覆盖，恢复前校验身份／父级；失败回滚本次创建，历史游标不前进。
  菜单／快捷键请求由 Editor 在 UI 准备后处理；New/Open 成功及 Edit/Play 切换清空历史，Play 修改不记录。
- Engine 在 Renderer::prepare_frame 完成 UI 准备之后读取活动 Scene 并提取，随后 render_frame；不新增快照 provider 回调。
- LineDrawList 接收单帧线段/包围盒；执行器使用场景 pass、相机和 MSAA，正常深度测试且不写深度。
  CPU 请求不依赖 Vulkan/ImGui；slot 独立 vertex buffer 安全复用，扩容失败跳过调试批次并延后重试。
- Edit 选中 Mesh 的局部包围盒八角点经过 world transform 后连十二条边；普通帧在 UI 命令完成后提交，
  点击帧在拾取结果更新 Selection 后提交，不画旧选择；Scene/Mesh/Material 不保存 selected 状态。
- 平移 Gizmo 使用世界轴、透视／正交投影和逻辑屏幕命中；父变换逆矩阵将世界位移转成本地 translation。
  与 Inspector 共用事务类型和历史，但不共用活动事务；释放提交一次，Escape／失焦／上下文变化取消。
  操作箭头由 ViewPanel 绘制为 UI 覆盖层，拖动预览先于当前帧 Scene 提取，渲染层不认识 Gizmo。

剩余：

- Gizmo 后续增加旋转／缩放、本地轴和吸附；持续验证当前帧快照一致性。
  多 pass outline 留到阶段 5，不与包围盒反馈混淆。
- 资产修改需独立定义文件事务，不与场景历史混用；大型子树操作后评估历史的内存字节上限，不只限制命令数。
- 搜索、复制粘贴、duplicate、拖拽资产、Prefab MVP。
- Project 缩略图、搜索和资产创建；与阶段 3 导入入口共用事务服务。
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

当前固定 cube pipeline、两张 Texture、u_Texture0/1 和逐帧属性解析都是 MVP 约束，不能只把 array 换成 vector。

1. SceneResolver 只解析 Mesh/Material，不知道材质属性名、纹理数量或 binding。
2. 建立 material/layout revision、手工 MaterialLayout 与渲染侧 MaterialRuntimeCache。
   材质资产保存 layout/template 和参数 Handle；缓存解析资源并生成 PreparedMaterial。
3. 按 Frame / Material / Object 分层：FrameSet 按 slot；MaterialSet 按 revision 创建不可变版本并跨 slot 复用；
   model matrix/object ID 可继续用 push constant，只有 per-frame backing 参数单独维护 slot state。
4. Render Queue 按 pipeline/material 排序，至少验证两种布局及纹理、标量、向量参数。
5. 再引入 SPIR-V reflection 生成 ShaderInterface（set/binding/type/count/stage/push constants）。
   显示名、默认值、颜色/法线语义和 Inspector 范围仍由 Material metadata 提供；不与 C++ 反射混淆。
6. Material Inspector 按布局生成控件，变化时精确失效缓存；当前不引入 bindless。

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

- 受版本控制的项目 manifest/ProjectSettings、启动场景、Build Settings 和模板，去掉发布对源码目录的依赖。
- 运行配置保持人工友好的 YAML。编辑器成为规范写入入口、Schema 稳定后，将 .scene/.mat/.meta/ProjectSettings
  整体迁移为确定性 JSON，配套版本迁移与工具；不因 fastgltf 间接带入 simdjson 就局部替换格式。
  JSON 需 Comet 自己的直接依赖/读写边界，不暴露 fastgltf 私有依赖。
- .comet/cache 继续存二进制派生产物与索引；Shipping Manifest 只含运行时身份、依赖和打包位置，
  不带松散 .meta、源资产与 editor importer 配置。
- 另存为、自动保存、崩溃恢复和日志目录规范；项目设置面板、打包播放器、CI 构建测试打包。
- 开发期 Scene Schema 不承诺兼容；冻结版本前明确迁移规则，不默默读入不兼容数据。

验收：示例可从编辑器打包成独立程序，不依赖仓库源路径；新贡献者能按 README 初始化与验证。
