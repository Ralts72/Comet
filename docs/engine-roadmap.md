# Comet 引擎路线图

更新：2026-09-12。目标是能完成小型 3D 项目的编辑器型引擎，先打通数据和编辑闭环，再扩展渲染与运行时能力。
本文只维护阶段、待办和设计约束，不累计每次迁移的完成日志。

## 当前阶段与下一步

| 阶段 | 状态 | 后续重点 |
| --- | --- | --- |
| 0 基线收束 | 基线已完成 | 持续测试与生命周期回归 |
| 1 Scene/ECS 与渲染提交 | MVP 已完成 | 系统化更新留到阶段 6 |
| 2 序列化与编辑器闭环 | MVP 已完成 | Schema、迁移与项目格式见阶段 7 |
| 3 资产数据库与导入 | 主链路、任务背压与发布预算已接通，仍有扩展 | 增量引用恢复、字节预算与更多导入格式 |
| 4 视口与交互 | 4A/4B 主链路完成，4C 进行中 | 内容编辑与撤销扩展 |
| 5 渲染升级 | 材质分层、Inspector、SPIR-V 接口、PipelineKey 与 CPU 编译入口已接通 | specialization、热更新、多 pass、线程边界 |
| 6 游戏运行时 | 规划 | 输入、System、脚本、物理、音频 |
| 7 内容生产与发布 | 项目打开最小入口已落地，其余规划 | 项目设置 UI、格式迁移、打包 |

以当前 main 的功能与验收为准，不再按 feat/auto 提交编号逐个迁移；旧分支仅作为算法、测试及设计参考。
下一步推进阶段 5 的 specialization 值、PipelineKey 和反射一致性，再接后台发布和热更新；阶段 3 的导入扩展及阶段 4 的内容编辑待办继续保留。
编辑命令与一次性属性事务已有共同执行边界；一对多通知在真实消费者出现后引入，不预建全局 EventBus。

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

当前基线：资产身份、扫描、导入／加载、后台发布及场景引用恢复已接通；
实际行为与失败边界见[资产管线](architecture/asset-pipeline.md)。

待办：

- 当前按事件重查整个场景；大场景按需求增加增量引用索引和受影响范围，不引入每帧轮询。
  任意运行时代码新写入的 Handle 尚不自动建立加载需求，结合阶段 6 Runtime 资源使用契约补齐。
- 大批量工作流后续按实际测量补总解码字节预算和接收失败后的恢复策略。当前任务数量有界，同 Handle 只保留一个最新后继，
  完成候选在发布／丢弃前仍占在途槽，失败／过期也计入消费预算；2 ms 是非抢占软预算，不保证整帧耗时。
  已执行的任务不强行中断。队列满会拒绝，不能等同于自动保证最终导入成功。
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

当前基线：单视口相机与布局、拾取／聚焦、选中包围盒、平移／旋转／本地缩放 Gizmo，
组件与层级编辑、引用选择／拖放、外部资产导入，以及统一场景撤销历史。
操作说明见 [README](../README.md#编辑器使用)；帧时序、事务和资源寿命见
[资源所有权](architecture/rendering-ownership.md)，文件操作边界见[资产管线](architecture/asset-pipeline.md)。

剩余：

- 持续验证 Gizmo 的当前帧快照一致性；缩放保持本地 TRS，不引入会产生剪切的世界非均匀缩放。
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

- 继续以 Command History 承接新的编辑入口。现有菜单／结构／资产／模式请求统一结束活动编辑；离散属性赋值复用事务 apply。
  新命令沿用唯一执行者、显式结果和事务边界，不另建平行的修改路径。
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

当前已分离场景资源解析、pass 编排和材质绘制：MaterialRenderer 使用手工 MaterialLayout 驱动 descriptor 与参数打包，
MaterialRuntimeCache 按版本复用快照。FrameSet 按 slot，MaterialSet 按不可变版本；队列支持 unlit_texture_blend/unlit_color 两种 Pipeline。
这仍是固定内置模板，不等于通用项目 Shader；对象缓存已按 Shader 内容与实际渲染配置构建 PipelineKey。

- 接通引擎内置基础材质，供新模型未指定材质时自动使用；项目描述不配置 default_material。
  基础材质不依赖 demo 的纹理或材质文件，内置资源有稳定身份／解析入口和明确生命周期，不在编辑器内写死临时 Handle。
  自动默认只针对未指定材质；显式引用丢失仍保留身份并报告错误，不用默认材质掩盖坏引用。
- Shader 源分为引擎内置（engine/shaders）与项目自定义（项目 assets/shaders）；前者由引擎维护，后者随项目版本控制。
  两者最终复用程序描述、编译／反射接口和渲染消费者；区分来源与身份，不各造一套 Shader/Pipeline 系统。
  项目 Shader 的产物写入项目 .comet/cache，内置 Shader 使用引擎构建／安装产物；项目不得通过同名文件隐式覆盖内置资源。
  项目只引用内置公开契约，不包含引擎源码绝对路径；私有渲染 pass 的 Shader 不必作为用户可选材质资产暴露。

1. 已接通：SceneResolver 只解析 Mesh/Material，不知道材质属性名、纹理数量或 binding。
2. 已接通：Material revision、不可变手工 MaterialLayout 与渲染侧 MaterialRuntimeCache，生成 PreparedMaterial。
   材质资产保存 template、纹理 Handle、标量／四分量向量；仅纹理进入资产依赖索引。
3. 已接通 Frame / Material / Object 分层：FrameSet 按 slot；MaterialSet 按版本创建并跨 slot 复用；
   model matrix 使用 push constant，实际使用的旧版本由 FrameSlot 保活。
4. 已接通按 pipeline/material 排序，验证两种布局及纹理、标量、向量参数，包含跨 slot 的 GPU 像素读回。
5. 已接通 SPIRV-Reflect 生成入口级 ShaderInterface（set/binding/type/count/stage/block members/push constants），
   Pipeline 创建前检查通用布局覆盖，MaterialLayout 校验参数块大小、偏移、类型和纹理协议；当前仍同步反射。
   ShaderInterface 公开 Comet 值类型，Vulkan 布局对照收敛于 ShaderLayout 实现，不向材质及未来编辑器消费者传播。
   显示名、默认值、颜色/法线语义和 Inspector 范围仍由 Material metadata 提供；不与 C++ 反射混淆。
   后续再扩展自动布局、复杂参数、顶点输入／stage 间接口和完整外部字节码校验；当前拒绝 runtime descriptor array。
6. 已接通内置 Material Inspector：按共享布局显示纹理／数值／颜色，真实变化才提交，失败恢复，缺槽可逐步修复。
   后续扩展反射布局、模板切换和资产撤销；当前不引入 bindless。

目标编辑流程：项目 Shader 源码及程序描述进入资产管线，描述组合 vertex/fragment 等阶段与入口；
编译与反射产出可用程序和参数布局，材质按稳定资产引用选择程序／模板，Inspector 按布局显示纹理槽及其他参数。
反射只负责类型和 binding，名称、默认值、用途与编辑范围由材质 metadata 补充，不把任意单个 GLSL 文件当完整渲染方案。
切换程序时保留兼容参数，对缺失或类型变化给出默认值／诊断；编译失败不替换当前有效版本。

Shader 源码、CPU 编译结果和 Vulkan 对象分层；build-time/editor 编译共用 stage、entry、defines/variants、target 和依赖契约。
已接通 tools/shader 的 CPU 编译入口与构建 CLI，固定 glslang，复用 Comet::ShaderStage；公开 API 不含第三方类型。
生产与测试构建使用同一入口，保留 INCLUDE_DIRECTORY/DEPENDENCIES 并生成 depfile；编译失败保留旧产物。
Result 拥有字节码、诊断和输入快照（包括缺失的搜索候选）；成功后复核输入，但不代替发布时的 revision／输入校验。
当前仅支持 GLSL vertex/fragment/compute、Vulkan 1.0/1.3 目标；未做持久编译缓存、优化器、HLSL、超时或沙箱。
depfile 仅跟踪存在的依赖，新增遮蔽文件不保证触发增量构建；未来监听须消费缺失候选，不能仅观察成功 include。
Editor-only 热加载按 debounce → Worker 编译/reflection → revision 验票 → owner 帧边界切换。
接口兼容时换 Pipeline；接口变化时同时重建 Layout 并失效材质缓存。失败保留旧版本并输出文件/行号诊断；
成功也不能提前释放在途帧引用的 Shader/Pipeline/Layout。Shipping 只消费预编译打包数据，不要求松散 .spv。

### Shader 编译产物与发布

- 当前内置路径：`GLSL → .spv → 生成的 .h 数组 → 编译链接进引擎二进制`。
  .spv 是 SPIR-V 字节码，生成的 .h 只是同一字节码的 C++ 数组表示，不是反射结果或 Shader 接口声明。
  运行时同一份内存字节码用于 ShaderInterface 反射和 Vulkan Shader Module 创建，不分别读取两份产物。
- 后续项目路径：`项目源码／程序描述 → 编译产物缓存 → 运行时加载字节码`。
  当前 Vulkan 后端的核心产物是 SPIR-V，可先保存为 .spv；后续按需求打包入口、编译选项和版本等元数据，
  不提前强制新容器格式。项目 Shader 不生成 C++ 头文件、不链接进引擎，修改后只重编 Shader 并更新渲染资源。
  取得字节码后复用内置 Shader 的反射、布局校验和 GPU 创建逻辑；此项目加载／热重载链路尚未接通。
- 编辑期缓存位于项目 .comet/cache，可重新生成；发布时将需要的编译产物作为运行资源交付，不能依赖开发机缓存。
  字节码可以是独立 .spv、资源包内容或二进制内嵌数据。若选择运行时读取独立 .spv，发布包必须包含它们；
  资源包和内嵌方式不要求松散 .spv，但同样必须携带字节码。
- 当前内嵌方式下，发布编辑器／运行程序不需要附带内置 GLSL、生成的 .h 或中间 .spv。
  若提供内置源码查看、修改或重新编译功能，再明确提供对应源码及必要编译依赖，不混淆源码发行与运行资源发行。
  保留仓库中的学习 Shader 源文件；发布裁剪不等于删除开发源码。
- 头文件嵌入只解决打包和加载，不是保密机制。SPIR-V 仍可能被提取、反汇编或反编译；
  按需求剥离调试信息可减少名称等信息暴露，但不能保证 Shader 逻辑不可分析。

验收：内置嵌入模式在不携带 Shader 源码／中间文件的发布目录中正常启动；项目产物脱离开发机缓存仍可加载，
修改项目 Shader 不触发引擎 C++ 重编译／链接；编译产物缺失有明确诊断，发布运行不隐式回退到源码编译。

### Pipeline 两级缓存

- 已接通当前 API 的 PipelineKey：Shader 完整字节码／入口、layout、vertex/topology、raster/depth/blend/dynamic state、
  静态 viewport/scissor、RenderPass 身份、attachment formats/sample count/subpass。名称仅作标签，hash 索引后完整相等比较。
  规范化无关顺序与动态 viewport/scissor 的静态值，规范化后的配置也用于实际创建；静态配置和 subpass 已接通 GPU 消费。
  PipelineManager 弱缓存，使用者和 FrameSlot 持有实际对象；下次创建或 collect_unused 清理过期键，不每帧扫描。
  ShaderManager 同名比较字节码／入口，候选构造成功才替换；后台请求 revision 验票与内容相等判断分别处理。
  尚未开放 specialization 值配置，随 Shader 编译契约后续接通键与实际消费，不把保留字段当成功能。
- 驱动 PipelineCache blob 用于跨进程加速，不代替对象 key。放在 .comet/cache/vulkan 或平台缓存，
  校验 header size/version、vendorID、deviceID、pipelineCacheUUID，以及 envelope 长度/校验和。
  损坏或不兼容回退空 cache，不影响启动。
- 结构化 key 已完成；接下来编译契约、热加载，随后 cache load/atomic save；编译批次后节流或关机保存，不每帧写磁盘。
  Pipeline 创建/合并/保存由同一 owner 串行访问；后台 ShaderCompiler 不直接操作 Vulkan cache。
- 测试 key 等价性、兼容性和损坏输入；cold/warm 性能只做测量，不要求固定加速比例。
- 接入 Shader 发布时，MaterialRenderer 的 GPU 材质缓存必须同时跟踪 PipelineState 版本，不能只比较 PreparedMaterial。
  当前 key 复制字节码保证完整判等；若实际测量出现开销，再共享不可变代码，不能退化为 hash-only。

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

- 已提前补齐最小项目入口：`project.json` 保存版本、名称和可选启动场景；编辑器接受项目目录／描述文件路径。
  项目 roots、资产索引、缓存、布局及 SceneDocument 均绑定同一项目；相对场景路径基于 assets，拒绝越界 Open/Save。
  无参数打开仓库 `demo/` 内的独立示例项目，显式无效项目不回退示例；空启动场景创建空文档，不硬编码示例资源。
- 编辑器内增加 File → Open Project，与现有 Open Scene 分开；选择目录或 project.json，并提供最近项目列表。
  切换前处理未保存场景和活动属性／Gizmo 编辑，Play 模式先退出；取消或新项目校验失败时保持当前项目不变。
  第一版可通过重启编辑器进程打开新项目，避免直接交换活动 AssetManager；若支持原地切换，须先排空旧任务和在途帧，
  再释放旧场景／选择／历史／资产缓存与监视器，保存旧布局并加载新布局，禁止旧项目结果发布到新项目。
  验收：无需命令行即可选项目；取消／失败不丢修改；同 Handle 的两个项目不串用资源或缓存。
- 后续扩展项目设置 UI、记录上次文档、Build Settings 和项目模板，去掉发布对源码目录的依赖。
  项目创建时生成 project.json；项目设置修改并校验成功后自动原子保存，不单独增加 Save Project 按钮。
  Save Scene 仅保存场景，不连带重写项目描述；只有启动场景等项目设置变化才保存项目，编辑器本地状态仍放 .comet/。
  让 app 读取项目场景，替换当前独立代码示例；编辑器／引擎自带 Profile、字体和 Shader 与项目内容保持分离。
- 已提前接入确定性 JSON：编辑器生成的 .scene v2、.mat v2、.meta v3 使用 JSON，扩展名与身份引用不变。
  .scene 按 children 嵌套保存子实体，保留 UUID，并在根节点和每组兄弟节点内稳定排序；不兼容旧 parent 字段。
  engine 显式依赖 simdjson，Scene/Material/Metadata 共用 JSON 读写工具。
  当前尚未发布，FORMAT_VERSION 只做检测报错，不提供旧格式兼容、迁移工具或备份。
  project.json v1 也使用 JSON，目录入口只查找 project.json；运行 Profile 和编辑器快捷键配置继续使用 YAML。
  ImGui ini 和二进制缓存不改格式。
  后续冻结 Schema 时补齐长期版本迁移策略，不提前统一所有文件。
- .comet/cache 继续存二进制派生产物与索引；Shipping Manifest 只含运行时身份、依赖和打包位置，
  不带松散 .meta、源资产与 editor importer 配置。
- 另存为、自动保存、崩溃恢复和日志目录规范；项目设置面板、打包播放器、CI 构建测试打包。
- 开发期 Scene Schema 不兼容旧版本；不匹配直接报错。首次发布并冻结格式时再制定后续版本迁移规则。

验收：示例可从编辑器打包成独立程序，不依赖仓库源路径；新贡献者能按 README 初始化与验证。
