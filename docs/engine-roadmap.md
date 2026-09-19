# Comet 引擎路线图

更新：2026-09-20。目标是能完成小型 3D 项目的编辑器型引擎，先打通数据和编辑闭环，再扩展渲染与运行时能力。
本文只维护阶段、待办和设计约束，不累计每次迁移的完成日志。

## 当前阶段与下一步

| 阶段 | 状态 | 后续重点 |
| --- | --- | --- |
| 0 基线收束 | 基线已完成 | 持续测试与生命周期回归 |
| 1 Scene/ECS 与渲染提交 | MVP 已完成 | 系统化更新留到阶段 6 |
| 2 序列化与编辑器闭环 | MVP 已完成 | Schema、迁移与项目格式见阶段 7 |
| 3 资产数据库与导入 | 主链路、任务背压与发布预算已接通，仍有扩展 | 增量引用恢复、字节预算与更多导入格式 |
| 4 视口与交互 | 4A/4B 主链路完成，4C 材质创建与模板选择已接通 | 内容编辑与资产撤销扩展 |
| 5 渲染升级 | PBR/base-color、阴影、材质反射、全局 IBL、Bloom、内置 Shader 热发布与渲染诊断已接通 | 阶段性架构审查、代表场景测量、项目 Shader 资产化 |
| 6 游戏运行时 | 规划 | 输入、System、脚本、物理、音频 |
| 7 内容生产与发布 | 项目打开最小入口已落地，其余规划 | 项目设置 UI、格式迁移、打包 |

以当前 main 的功能与验收为准，继续逐项对照 feat/auto2 的实现及原始提交，而不是机械 cherry-pick。
每项先注明对应旧提交、当前覆盖、需要调整及仍未覆盖的范围，再适配 main 的 Result、目录边界和生命周期；
临时代码说明留在仓库内供学习，不进入提交。旧分支的已实现行为不能只因 main 有同名功能就判为完整覆盖。
材质布局重建、呈现／场景边界和完整目标事务已收敛；辅助线 Shader 热更新按实际需求暂缓，旧 026 驱动 PipelineCache、旧 027 WSI 恢复、旧 028 有序 RenderGraph 与旧 029 HDR／SDR 双 pass 已核对适配；旧 030 类型化光源与有界 forward 光照、旧 031 单方向光阴影、旧 032 PBR 已按当前所有权和 Result 协议适配，并接通可选 base-color 纹理。
阶段 3 的导入扩展及阶段 4 的内容编辑待办继续保留。
编辑命令与一次性属性事务已有共同执行边界；一对多通知在真实消费者出现后引入，不预建全局 EventBus。

近期顺序按可用性调整，不机械沿旧分支继续叠加效果：

1. **已接通：材质编辑闭环**（阶段 4C／5）：Project 创建材质、Inspector 选择已有模板、按属性布局编辑并指定给物体。
   先使用现有 pbr／unlit_color 与共享 metadata，不等待项目 Shader 资产化，也不为每个用户材质注册 C++ 类型。
2. **已接通：环境光照**（阶段 3／5）：背景与照明独立控制，后台准备 irradiance／GGX prefilter／BRDF LUT，
   整组缓存和 GPU 发布复用现有预算／版本链路；PBR 的背光非金属和金属均可获得环境贡献。
3. **已接通：Bloom**（阶段 5）：HDR 高亮提取、半分辨率横纵模糊与显示前合成；曝光／泛光作为场景内容接入 Inspector 实时编辑、撤销和保存，app/editor 统一消费场景快照，不再由引擎 YAML 决定外观。Bloom 不补偿缺失照明。
4. **已接通：有界渲染诊断**（阶段 5）：旧 `034 / f6dd1ac` 适配为 Renderer 所有的诊断服务，提供 CPU/GPU 耗时、低频显存预算与按需分配报告；查询封装在 graphics，完成证据与保活复用既有帧生命周期。
5. **下一步：阶段性架构审查**（阶段 5）：核对旧 `035 / 253d5d1` 的平台生命周期、面板状态与关机边界在 main 的覆盖，不重复迁回已修正的实现；随后对照 `9fcb93c` 补齐可复现的代表场景性能测量，再衔接阶段 6。
6. **后续独立里程碑：项目自定义 Shader**（阶段 3／5）：复用材质编辑入口，接通程序资产、metadata、动态布局与发布所有权。
   依赖材质编辑闭环和程序资产协议，不把它作为 PBR／环境照明的前置；出现真实自定义着色需求时可独立提前。

与 feat/auto2 结合：本轮复用已迁移的 `9a7b2e3` 共享布局面板和 `40dfe50` 候选发布思路，补齐该分支未提供的材质创建／模板选择。
旧 PBR 后的 `033 / 63b2394` 已适配为独立 BloomPass 与 OutputPass 合成，沿用 HDR 提取／模糊算法和像素验收思路，
接入当前 RenderGraph、HDR/SDR 输出与在途资源所有权，不迁回旧 PostProcessRenderer 的命名与职责。
旧 034 诊断已适配当前 Result 和所有权，后续继续逐项核对架构审查与测量，不能用新增规划替代旧分支验收，也不能用带 validation 的小测试耗时代表真实项目性能。

WSI 暂时失败的无呈现重试及 SurfaceLost 重建已接通；设备丢失恢复与跨呈现队列迁移仍需单独设计。

### 架构收敛原则

- 当前实现与失败边界以架构文档为准，不在路线图重复已完成的逐文件迁移记录。
- 优先减少外露协议、统一状态推进入口，不以新增类或拆文件数量作为收益。
- 预期业务失败保留 Result 与错误类别；LOG_FATAL 仅用于内部不变量，不代替需要清理的退出。
- 不跨模块机械清理异常，也不新增通用错误传播宏；第三方边界按实际实现处理。
- Transform 写入契约与快照读取安排在阶段 6；项目 Shader 所有权调整随阶段 5 的真实消费者推进。

## 基本边界

- Scene 与持久化数据只保存组件、Entity UUID 和 AssetHandle，不保存路径、GPU handle 或运行时 shared_ptr。
- AssetHandle 是非零 64 位持久资产身份；EntityId 是进程内实体标识，EntityUuid 用于持久定位，二者不混用。
- AssetRegistry 是唯一 Handle → Runtime Asset 缓存。RenderResources 不建立第二份资产缓存。
  CPU MeshData／TextureData／MaterialData 位于 asset/data；材质运行时定义／准备／绘制聚合到 render/material，辅助线聚合到 render/debug。
  SceneRenderer::RenderState 保存完整兼容资源，render 执行多 pass；不为目录整理增加抽象基类。
- Engine 组合通用能力，app/editor 组合项目工作流；不把 demo bootstrap 或 ImGui 专用类放入 engine。
- 以实际职责、可测试契约和生命周期拆分，不为减少成员数量套结构体，不为对齐设计图增加转发 façade。
- 保持 C++20；通用纯逻辑先有测试，Vulkan 行为通过集成测试和 validation 检查。
- Shader 只保留实际使用的生产实现与有明确覆盖目标的测试资源；不为学习用途长期维护已被替代的模板。
- 所有格式规则以根目录 .clang-format 为准，不在路线图或 AGENTS 重复一套排版细则。

当前实现以代码和[资源所有权](architecture/rendering-ownership.md)为准；
下文的新类型名是目标职责，不意味着立即新增同名文件或类。

## 阶段 3：补全资产工作流

当前基线：资产身份、扫描、导入／加载、后台发布及场景引用恢复已接通；
实际行为与失败边界以 AssetManager、ImportService 及对应测试为准。

待办：

- AssetTaskQueue 当前只服务 AssetManager，作为私有资产执行模块保留；revision 复核、Force 合并与发布预算是资产语义，
  不提前包装为通用任务框架。第二个独立消费者出现时再评估调度机制与资产策略分离。
  验收仍覆盖过期结果拒绝、同 Handle 最新请求合并、发布期间占槽与异常后的槽位回收。
  扫描触发的 Material 刷新已复用同一队列：后台读取数据，owner 完成处理时创建依赖并校验版本发布，不在扫描阶段同步创建资源。

- Runtime Mesh／Texture／Material 首次加载、重载及依赖解析已统一返回保留错误类别的 Result，
  EditorAssets、候选准备、Inspector 请求和 app 必需资产加载消费同一协议，不增加全局错误旁路。
  回归覆盖普通失败保留资源/文件、DeviceLost 拒绝场景安装与错误码追踪；仍需完成真实桌面交互验收。
  首次加载已复用缓存／类型／revision 校验与注册流程；材质重载和更新仅共享依赖更新及 Runtime 发布，
  不合并文件读写策略，不将多步发布描述为全局事务。

- 当前缓存活动场景引用，按变更 Handle 及依赖闭包分批恢复；场景安装／编辑历史变化时仍全量收集引用，后续按实际规模细化字段变更通知。
  任意运行时代码新写入的 Handle 尚不自动建立加载需求，结合阶段 6 Runtime 资源使用契约补齐。
- 容量拒绝恢复已接通：编辑器 Mesh 待办与驻留资产刷新保留身份/版本并自动重试，内容失败仍等待新事件。环境准备已有预估 CPU 字节预算；后续向普通纹理和 Mesh 扩展，不宣称当前已限制进程总内存。当前任务数量有界，同 Handle 只保留一个最新后继，
  完成候选在发布／丢弃前仍占在途槽，失败／过期也计入消费预算；2 ms 是非抢占软预算，不保证整帧耗时。
  已执行的任务不强行中断。队列满会拒绝，不能等同于自动保证最终导入成功。
  当前每次扫描事件会检查所有已索引 Mesh 的缓存；后续结合失败依赖追踪缩小检查范围，不引入每帧产物扫描。
  外部文件复制／校验当前同步执行；大文件工作流后续补异步准备、进度、取消和发布预算。
- TextureArtifact 留到实际扩展纹理导入时一起实现原子发布与 Artifact-only 加载；不先复制 Mesh 的所有中间层。
- Texture 的 wrap/filter、mipmap、压缩必须接通 Sampler、Image 与上传消费者，不能只增加不生效的配置字段。
- glTF 多 mesh 子资产、node transform、material、animation、skin、morph target 需明确导入契约后逐项扩展。
- 补全递归依赖失效、更多资产类型；资产与 Shader 的原生文件监听按下节统一接入，
  不把手工编辑 .meta 的失败保留作为当前产品路径。
- 给 Device 提供反映实际启用 features/extensions/queues/limits/formats 的不可变能力集合，不再让使用方拼接来源。
- 非关键 streaming 资源先建立占位、重试、淘汰，再扩展严格预算策略；关键 RenderTarget 保持明确失败。
- Shader 编译产物、缓存键与发布 Manifest 在 metadata/导入协议稳定后设计，Shipping 不带源编译器。

验收：移动不破坏引用；失败不发布半初始化对象；旧任务不能覆盖新结果；大批量操作不长期卡主线程。
Mesh 缓存可删除重建但不替代源资产；Runtime 加载不能隐式回退 glTF 解析。

### 统一文件监听与防抖（阶段 3／5 共用，待实现）

作为独立验收项，先贯通现有资产监视与内置 Shader，再用于项目 Shader 的自动监视。
当前 500 ms 轮询和发现变化后固定延迟 200 ms 的实现保持为过渡状态，不等同于下述目标。

- **职责与所有权**：平台层只报告文件／目录可能变化，不直接导入、编译或操作 Scene／GPU。
  资产和 Shader 共用底层监听及变更合并能力，各自按依赖关系决定重建范围；不创建全局 EventBus。
  监听注册、解除由所属项目／编辑器服务管理，连接可自动释放；切换项目、销毁服务后排队事件按 generation 丢弃。
- **原生通知优先**：macOS、Windows 分别接入平台后端并验收，平台细节留在私有实现；其他平台暂用显式兜底。
  后端不支持、注册失败或所在文件系统通知不可靠时才启用可配置的后台轮询，并报告当前降级状态。
  正常空闲路径不周期读取源码、不扫描目录；主线程只消费有界事件／完成队列，目录查询与内容复核在后台。
- **按目录监听**：覆盖源码父目录、递归目录变化和缺失 include 的搜索候选，避免仅绑定旧文件对象，
  漏掉编辑器“临时文件写完后 rename 替换”的保存方式。新增目录及时注册，目录级通知转换为局部重查。
  事件可能重复、合并、乱序，不能把一次 Remove 立即视为资产永久删除；按合并后的磁盘状态与现有身份规则决策。
  排除 .comet/cache、构建目录等自产物；编辑器自身写入通过已确认内容去重，不用全局忽略时窗吞掉外部修改。
- **尾沿防抖**：以受影响资产／Shader 程序批次为合并键，而不是全局定时器；共享 include 可使多个程序分别变脏。
  使用单调时钟记录最后一次相关事件，`due = last_event + quiet_period`；新事件到来就延后该键的截止时间。
  初始 quiet_period 建议 200 ms，接入 editor-dev YAML 并校验范围，按实测调整，不硬编码为行业标准。
  例如事件发生于 0、80、150 ms，截止时间依次变为 200、280、350 ms；无后续事件才在 350 ms 后排队。
  不 sleep 主线程；持续写入只保留一个 dirty 项，不强制在仍不稳定时编译，也不拖延其他资产的更新。
- **通知只是线索**：静默时长不证明文件写完。到期后复核内容、依赖与路径解析结果；内容未变不推进实际编译／导入。
  短暂缺失、占用、分段写入采用有界退避和日志去重，失败保留旧有效版本；到达重试上限等待新事件或显式 Refresh。
  新事件立即使相关在途候选过期；实际重建请求在防抖后合并提交，仍最多一个在途任务和一个最新后继。
  队列满保留 dirty 状态并稍后重试；发布前继续复核 revision／输入快照，不能用文件事件代替一致性检查。
- **漏事件恢复**：事件队列溢出、监听根目录失效、休眠恢复或后端重连时，标记受影响 root 需重新核对。
  初始扫描先注册监听并缓存扫描期间事件，再统一核对，避免扫描和注册之间漏改动。
  重扫按 root 合并并放到后台，不为每条溢出消息扫整个项目；启动校准和手动 Refresh 继续保留。
  原生后端可用时，不另保留每 500 ms 全量扫描作为常态“双保险”。

验收采用可控时钟和事件注入，并增加 macOS／Windows 的真实文件操作冒烟测试：

- 空闲时无源码读取／目录扫描／重复编译；单次保存、重复事件、连续保存仅重建最终稳定内容。
- 两个无关资产独立防抖；共享 include 只更新依赖者，缺失 include 创建后自动恢复。
- 原地写、原子替换、重命名、删除后重建、目录搬移、分段写入均最终收敛，不丢资产身份或发布半批结果。
- 在途修改丢弃旧结果；队列满、事件溢出、恢复重扫最终收敛；项目切换后无失效回调、跨项目事件或重复发布。
- 统计原生事件数、合并批次数、重扫次数和实际编译／导入次数，确认改进的是空闲 I/O 与重复工作，而非只换调用方式。

参考依据与边界：Unreal 的自动重导入提供监视目录、过滤条件及 Import Threshold Time，支持“检测后延迟处理”，
但文档没有规定所有引擎必须采用同一种尾沿算法。见 [Unreal 自动重导入](https://dev.epicgames.com/documentation/en-us/unreal-engine/reimporting-assets-automatically-in-unreal-engine)。
Unity 6 文档列有 Windows 的 Directory Monitoring，并在导入期间输入变化时重新刷新；不能概括成所有平台都不扫描。
见 [Unity 偏好设置](https://docs.unity3d.com/cn/6000.0/Manual/Preferences.html)和
[资源数据库刷新](https://docs.unity3d.com/6000.0/Documentation/Manual/AssetDatabaseRefreshing.html)。
以上共用监听、按依赖批次尾沿防抖与 200 ms 初值是 Comet 的设计选择，不是照搬这些引擎的实现或默认值。

## 阶段 4：完成编辑交互

已补齐场景未保存保护：文档保存点使用历史状态 ID，New/Open/窗口关闭提供保存、丢弃、取消；Play/Stop 保留 Edit 历史。请求执行移到 on_update、获取渲染帧之前；同步文件操作仍可能阻塞主线程，后台 I/O 准备与取消另行推进。
活动场景引用已缓存，资产变更按 Handle/依赖与未解析集合增量恢复，并采用独立软预算；更细的组件变更通知和总字节预算仍待后续需求。

当前基线：单视口相机与布局、拾取／聚焦、选中包围盒、平移／旋转／本地缩放 Gizmo，
组件与层级编辑、引用选择／拖放、外部资产导入，以及统一场景撤销历史。
操作说明见 [README](../README.md#编辑器使用)；帧时序、事务和资源寿命见
[资源所有权](architecture/rendering-ownership.md)，文件操作边界以 SceneDocument 和 EditorAssets 的实现为准。

剩余：

- 持续验证 Gizmo 的当前帧快照一致性；缩放保持本地 TRS，不引入会产生剪切的世界非均匀缩放。
  多 pass outline 留到阶段 5，不与包围盒反馈混淆。
- 搜索、跨场景复制粘贴、Prefab MVP；资产修改需独立定义文件事务，不与场景历史混用。
- 按大场景实际使用测量撤销快照内存；当前仅限制历史条数，巨大子树的内存预算后续按需求完善。
- Project 材质资产创建与 Inspector 模板选择已接通，细节见阶段 5“材质编辑闭环”；
  缩略图、搜索和其他资产创建后续推进，与阶段 3 导入入口共用事务服务。
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
  场景环境／背景色与后处理已共用类型化 SceneTarget 的预览、提交、取消；新增同类设置不再扩充事务的类型分支。
  历史只记录内容目标及修改前后值，不记录 GPU 对象、Pass 生命周期或整个引擎快照；普通编辑与 Undo/Redo 走同一场景提取链路。
  一次手势只记录一次，提交捕获校验／归一化后的实际值，不重复恢复再重放；结构编辑仍使用有明确逆操作的命令。
  新命令沿用唯一执行者、显式结果和事务边界，不另建平行的修改路径。
  可撤销的场景修改进入历史；保存、刷新、重导入等服务操作不因入口统一就强行加入 Undo/Redo。
  持久化的 Bloom 等内容开关同样可撤销；面板可见性、语言、相机导航及未来仅影响编辑器的临时效果预览不进入场景历史。
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

当前内置模板收敛为 unlit_color 和 pbr，不等于通用项目 Shader。
已接通材质版本快照、Frame/Material/Object 分层、共享布局 Inspector、SPIR-V 反射校验、PipelineKey 与 CPU 编译／构建 CLI；
实现与失败边界集中在[资源所有权](architecture/rendering-ownership.md#材质shader-与-pipeline)。

#### 材质编辑闭环（已接通，资产撤销后续扩展）

目标流程：Project 新建材质 → 选择 Shader 模板 → Inspector 显示材质属性 → 配置数值／纹理 → 指定给物体 → 保存重开。

- **资产与模板分开**：用户材质是普通 `.mat` 项目资产，保存模板引用、参数与纹理 Handle；模板定义着色逻辑和属性契约。
  引擎可内置公开 Shader 模板及独立的默认材质，但不能把 demo 材质当引擎必需资源，也不为每个材质写 C++ 注册代码。
- **Project 创建入口**：选择目标目录、文件名与已有模板，生成 `.mat`、稳定 `.meta` 身份并选中新资产。
  复用现有资产服务、路径约束与 Result；拒绝重名覆盖，写入失败不留下可见的半成品，重开后身份及引用不变。
- **Inspector 模板选择**：先提供 pbr／unlit_color 下拉选择，候选来自公开模板定义，不由面板复制硬编码列表。
  对已有材质切换时只保留属性 ID、类型和语义兼容的值；新增属性使用模板默认值，不兼容参数的丢弃需明确提示。
  材质身份与场景引用保持不变；新依赖、布局和绑定准备失败时保留原材质，不能先保存新模板却继续显示旧参数。
- **参数面板复用**：继续由共享布局与 metadata 生成控件，只展示 Material 域参数，不暴露 Frame 的相机／灯光或 Object 矩阵。
  反射提供类型、binding、offset；metadata 提供稳定属性 ID、默认值、范围、颜色等编辑语义。
  本轮只覆盖现有标量、向量／颜色和纹理能力，不承诺任意 uniform、结构体、数组都能自动编辑。
- **提交边界**：创建、模板切换和参数修改走同一资产编辑入口；先完成失败可恢复的保存／运行时发布。
  资产 Undo/Redo 在资产文件事务上独立扩展，不混入场景历史，也不把当前自动保存称为可撤销编辑。

当前实现：目录／空白处 New Material 与 Template 下拉框共用已发布布局；纯函数负责默认值和兼容参数迁移。
创建在缓存暂存 `.mat`／`.meta` 后以不覆盖的硬链接发布，候选数据库扫描成功才替换索引；普通失败回滚，不承诺崩溃原子性。
编辑通过 AssetManager 准备只读材质候选，MaterialRenderer 准备完整 CPU／GPU 绑定；保存成功后在同一帧边界发布，
期间不插入 Shader 发布或 renderer 重建。未提交的候选直接释放，依赖／GPU 准备或文件保存失败均不切换原材质。

验收：从空项目创建纯色／贴图 PBR 并用于物体，无需手改 JSON；切换模板后参数、文件和画面一致；
重开、资产移动、缺失纹理、重名／写入／GPU 准备失败与在途帧生命周期均有回归，app/editor 使用同一材质数据。
自动回归覆盖创建路径约束／扫描回滚、UI 确认取消与失败恢复、候选过期／保存失败，以及模板切换前后的 GPU 像素和在途帧。
未注入真实驱动 OOM；该路径依赖既有 Buffer／Descriptor 创建 Result，不能把布局拒绝称为真实显存耗尽覆盖。

#### 默认资源与程序资产

- 接通引擎内置基础材质，供新模型未指定材质时自动使用；项目描述不配置 default_material。
  基础材质不依赖 demo 的纹理或材质文件，内置资源有稳定身份／解析入口和明确生命周期，不在编辑器内写死临时 Handle。
  自动默认只针对未指定材质；显式引用丢失仍保留身份并报告错误，不用默认材质掩盖坏引用。
- Shader 源分为引擎内置（engine/shaders）与项目自定义（项目 assets/shaders）；前者由引擎维护，后者随项目版本控制。
  两者最终复用程序描述、编译／反射接口和渲染消费者；区分来源与身份，不各造一套 Shader/Pipeline 系统。
  项目 Shader 的产物写入项目 .comet/cache，内置 Shader 使用引擎构建／安装产物；项目不得通过同名文件隐式覆盖内置资源。
  项目只引用内置公开契约，不包含引擎源码绝对路径；私有渲染 pass 的 Shader 不必作为用户可选材质资产暴露。

Shader 基础能力与后续独立验收项：

1. 可失败创建与消费者迁移：反射／布局／Pipeline 校验、GPU 候选创建、结果处理与失败回滚完整接通，见下节。
2. 内置材质 Shader 后台编译与发布已完成：请求 revision、输入快照复核、整批 GPU 候选切换、失败保留旧版本、在途帧寿命。
   开发编辑器按顶点/片元配对登记材质程序；单个在途任务与最新后继合并，不阻塞等待调度容量。
   ShaderReload 接收 1..16 个具名 CPU 请求，不持有渲染器、GPU 对象或发布回调；编译成功不等于 GPU 发布成功。
   Vulkan 主机／设备内存不足时由 Editor 请求重新交付同一 CPU 候选，依次等待 1、2、4 秒，最多重试三次；复核输入和 revision 后重试 GPU 发布，不重新编译。耗尽后等待新请求。
   接口错误不重试，DeviceLost 继续退出；重试成功前旧程序与绑定保持不变。
   Renderer 检查发布帧边界，SceneRenderer 发布材质程序并暂存成功字节码供目标重建，旧 Pipeline 由在途帧保活。无生产消费者的 ShaderManager 已移除。
   热发布已报告管线准备、候选复制、材质 CPU／GPU 准备耗时；根据真实规模再决定增量／跨帧准备，不提前改写事务。
   已登记属性可重建 MaterialSet 的 offset／块大小／binding；Frame／Object 与基础顶点输入、Vertex→Fragment 仍须匹配。
   已补固定资源契约（递归 block／push 成员、矩阵／数组形状、采样图片类型）、兼容更新的材质绑定复用、
   相同 Pipeline 发布幂等性与活动帧入口检查；驻留材质候选整批成功后切换，并向 Inspector 交付布局快照。
   新属性、复杂参数和项目 Shader 资产未接通，不能把有限布局重绑定说成任意接口动态生成。
   监视仍是每 500 ms 内容复核、200 ms 防抖，不是原生文件事件；GPU 创建仍可能造成主线程尖峰。
   原生监听、尾沿防抖与漏事件恢复统一按阶段 3 的“统一文件监听与防抖”专项推进，不在 Shader 内另建后端。
3. 项目 Shader／程序资产与布局生成：补复杂参数、顶点输入／stage 间接口、外部字节码校验。
   同步完成下节“资源服务与程序版本所有权”的迁移，不把程序状态继续叠加到 SceneRenderer。
   复用上面的模板选择控件，将候选扩展为内置公开程序与项目程序，支持新增 metadata 属性驱动面板；
   当前字符串 template 引用届时定义版本迁移，转为明确来源与稳定程序身份，不按文件路径或显示名定位。
   资产撤销与文件事务一起完善；不把已有模板选择推迟到本项，也不先引入 bindless。
   基础接口校验已落地：按入口反射 user I/O、忽略 built-in，检查顶点 attribute／binding 和片元输入的来源。
   当前采用精确 32 位标量／向量格式契约，不支持的数组、矩阵、结构体、64 位与 component 打包 I/O 明确拒绝。
   后续按消费者扩展 normalized／packed 顶点格式转换、复杂 I/O、插值／附件输出及设备能力校验；
   不把这一步的保守限制说成 Vulkan 完整兼容规则，也不把反射视为完整 SPIR-V validator。

近期旧实现对照顺序：

- 023 `5ae0775`：兼容材质热更与绑定复用已覆盖；保留 main 的 Result、CPU/GPU 分离，不恢复 ShaderManager 发布快照。
- 024 `40dfe50`：已接已登记属性的布局重绑定、驻留材质整批重建和 Inspector 同步；
  使用 Result 和布局对象身份，不恢复布局 revision 计数或 ShaderManager 快照，不包含任意新属性或项目 Shader 资产。
- 025 `c186e75`：只保留架构整理：具名 CPU 编译服务、局部 GPU 候选和在途帧所有权，
  删除无生产消费者的 ShaderManager 及后台重复反射。Debug 热重载暂缓，保留辅助线绘制和统一构建路径；
  有实际辅助线着色效果开发需求时再评估，不作为下一项的前置条件。
- 026 `6d9f365`：已适配设备／版本校验、损坏拒绝、原子保存、ImGui 借用和跨进程恢复；使用实际项目目录与 Result，不迁回旧异常捕获，不替代 PipelineKey 对象缓存。候选驱动拒绝／OOM 尚无故障注入，缓存总预算和淘汰仍待实际需求。
- 027 `88cdd4a`：主线已由 Presentation 管恢复，补齐独立 WSI 故障注入回归及持续 INCOMPLETE 的有界枚举；保留 1／2／4 秒预算和 SurfaceLost 扩展，不恢复旧 SceneRenderer 编排或固定间隔无限重试。人工 ImGui、真实平台 SurfaceLost 及设备恢复仍不在覆盖内。
- 028 `fd1d5f3`：已适配有序资源声明、纯 CPU 同步计划、当前离屏目标及真实 GPU producer/consumer。声明统一在 compile 返回 Result，录制失败保留 GraphicsError，复用 Barrier2／FrameSlot；同步校验覆盖子资源、区间、跨提交、MSAA 与 resize。外部 upload／WSI 等待保持显式；尚无 DAG 重排、瞬态分配、多队列、内存别名跟踪或 RenderThread。
- 029 `624a143`：已适配 RGBA16F 场景与共享 fullscreen 输出，保留 Result、短回调和完整 RenderState 所有权；中间／输出目标成对替换，提前检查 HDR 格式与采样数。启动配置支持 sdr/hdr/auto，HDR 优先 RGBA16F + 扩展线性 sRGB，不支持时回退 SDR；编辑器固定 SDR。GPU 像素验证覆盖 RGBA/BGRA、sRGB/UNORM、浮点 HDR、高亮、曝光、方向、MSAA、resize 和在途资源保活。尚无显示器亮度校准、HDR10/PQ、自动曝光、中间格式降级或第二目标 OOM 故障注入。
- 030 `30dce0f`：已适配方向／点／聚光、32 灯上限、lit_color 和帧 UBO；枚举共用 Inspector／撤销／JSON，姿态保留主线的层级去缩放语义。热发布与重建保留各组成功字节码，不迁回 ShaderManager、旧 YAML 或异常。CPU、UI 和 GPU 像素／版本寿命测试覆盖主链路；人工视觉与远端平台验收另行进行。
- 031 `531c7b6`：适配为 `passes/ShadowPass`，深度图与 FrameSet 按槽位独立，复用 RenderGraph 深度写入／采样依赖与上传等待合并；创建和录制返回 Result。纯深度管线按真实子通道颜色附件数创建。单方向光、1024²、3×3 PCF，覆盖开关、删除、移动、目标重建和在途寿命；不迁入旧异常与命名。级联、稳定化、透明裁切、点／聚光阴影及跨平台视觉验收后续处理。
- 032 `be721fe`：接入 pbr、160 字节相机 Frame UBO 与共用光源采样；保留当前接收面深度梯度阴影算法、Result 与具名完整程序发布，不恢复 ShaderManager 或共享顶点消费者隐式绑定。后续补齐可选 base-color 纹理与白色默认绑定，参数和纹理编辑复用 Inspector。demo 立方体继续使用原 PBR 资产身份，引用已有 sRGB 纹理，移除不再使用的 demo.mat；不迁移其双纹理 blend 语义。法线／金属粗糙度贴图、IBL、透明和 glTF 材质导入仍未覆盖。
- 033 `63b2394`：适配为 BloomPass 的半分辨率高亮提取、九采样横纵模糊与 OutputPass 的线性 HDR 合成；参数现归属场景，支持 Inspector、撤销与持久化。关闭时不执行额外 pass，首次关闭不创建中间目标；开启后保留目标供复用。复用完整目标替换和帧保活，覆盖 SDR/HDR 像素、奇数／极小尺寸、极值、MSAA、开关、resize 与同步校验。未引入旧异常、soft-knee、多级金字塔或自动曝光；第二中间目标分配失败仍无真实 OOM 注入。
- 原生文件监听按阶段 3 专项安排，旧分支同样采用轮询，不作为最终方案迁回。

specialization 已贯通类型化值、默认值规范化、反射校验、PipelineKey 和 GPU 创建。
当前仅支持 bool/int32/uint32/float32 的固定接口变体；所有依赖 specialization 的数组长度暂不接受，
包含局部数组及派生表达式。改变数组形状需经编译期 defines 生成新字节码再反射；不把每帧材质参数变成 Pipeline 变体。

目标编辑流程：项目 Shader 源码及程序描述进入资产管线，描述组合 vertex/fragment 等阶段与入口；
编译与反射产出可用程序和参数布局，材质按稳定资产引用选择程序／模板，Inspector 按布局显示纹理槽及其他参数。
反射只负责类型和 binding，名称、默认值、用途与编辑范围由材质 metadata 补充，不把任意单个 GLSL 文件当完整渲染方案。
切换程序时保留兼容参数，对缺失或类型变化给出默认值／诊断；编译失败不替换当前有效版本。

Shader 源码、CPU 结果与 Vulkan 对象分层；后续编辑器复用现有编译库，不另起编译实现，也不让 Shipping 链接 glslang。
后台发布同时复核请求 revision 与输入快照；监听需包含缺失的 include 候选，不能只观察成功包含的文件。
持久编译缓存、优化器、HLSL、超时／沙箱和交叉编译 host tools 按实际需求安排，不把当前编译器视为不可信源码安全边界。
Editor-only 热加载已按 debounce → Worker 编译 → revision／输入验票 → owner 反射校验／创建／绘制前切换接通。
已登记材质属性的 Layout 与参数映射随 Pipeline 一起更新。失败保留旧版本并输出诊断；
成功也不能提前释放在途帧引用的 Pipeline/Layout/材质绑定。ShaderModule 只需活到 Pipeline 创建结束。
Shipping 只消费预编译打包数据，不要求松散 .spv。

### 资源服务与程序版本所有权（随项目 Shader 接入）

目标不是减少 RenderResources 的字段数量，而是按资源身份、程序版本、设备资源和帧生命周期划分职责。
当前 SceneRenderer::m_material_shaders 是为重建保留成功字节码的过渡实现；短期保留，
不只为搬走一个 optional 创建新 Manager，也不恢复仅按名称缓存 ShaderModule 的 ShaderManager。
本项随项目 Shader／程序资产分步落地，不阻塞旧 026 的驱动 PipelineCache，不恢复辅助线热重载。

#### 职责与依赖

| 角色 | 应负责 | 不应负责 |
| --- | --- | --- |
| AssetDatabase／AssetRegistry／AssetManager | 项目程序的稳定身份、依赖、导入产物与加载；沿用既有职责分工 | 按窗口／RenderPass 选择 Pipeline，提前宣布 GPU 版本发布成功 |
| RenderResources | 实现 RenderResourceFactory，创建 Mesh／Texture，组织上传与 Sampler 复用 | 项目文件监视、程序当前版本、材质语义、帧编排 |
| 渲染层程序状态 | 持有当前渲染域已发布的不可变程序版本，为消费者提供版本快照 | 文件轮询、源码编译、窗口尺寸、命令录制 |
| MaterialRenderer | 从程序快照、材质数据和目标兼容性准备 Pipeline／参数／绑定，执行绘制 | 作为唯一程序版本仓库，在目标重建时丢失程序身份 |
| SceneRenderer | 目标／pass 兼容资源的完整安装、场景录制与消费者重建；整帧和呈现归 Renderer／Presentation | 长期保存阶段字节码或实现源码热重载策略 |
| PipelineManager／FrameSlot | 前者按内容与状态弱缓存 Pipeline；后者保留实际提交的资源 | 前者充当资产库或持久格式；后者持有无关的全部程序历史 |

程序状态由长期存活的 Renderer 或同级渲染运行时 owner 装配，生命周期长于具体 SceneRenderer／MaterialRenderer 的目标重建。
具体类名在实施时确定；优先使用一个入口清晰的 render 程序模块，不按每种 Shader 再建 Manager 或大量单字段文件。
AssetRegistry 继续作为稳定资产 Handle 的统一解析入口；渲染域的发布状态按程序身份关联，不能再创建平行身份表。
编辑器通过明确的候选发布命令交付 CPU 数据，不把编译回调注入 RenderResources 或 SceneRenderer。

#### 程序身份、候选与版本

- 程序描述包含稳定身份、阶段组合、入口、编译选项、目标环境及依赖；显示名和文件路径不作为版本身份。
  内置公开程序与项目程序使用同一消费协议，但来源／命名空间明确，项目不得按同名文件覆盖内置程序。
- CPU 编译产物保存自有字节码、接口信息和内容标识；反射提供物理布局，属性名称／默认值／编辑语义由 metadata 提供。
  不持有 Device、RenderPass、ShaderModule 或 Pipeline；已有解析结果可复用，但外部产物仍须经过可信边界校验。
- 区分“最新请求 revision”“可用编译产物”和“当前渲染域已发布版本”。编译成功或 Artifact 原子落盘，
  不代表任何渲染消费者已经切换；GPU 失败时可以保留编译候选，但不能改变 active 版本指针。
- 发布版本为不可变快照，包含阶段字节码及匹配的接口／材质布局信息；调用方持共享快照，不修改历史版本。
  同内容请求幂等处理；内容一致性以完整内容或可靠校验确认，不能只依赖显示名或短 hash。
  本期只定义单渲染域的发布一致性；未来多设备／多个独立渲染域分别确认成功，不把一个全局 active 指针当成所有域已就绪。

#### 发布与重建时序

```text
资产导入／开发编译 → 自有 CPU 候选
  → owner 复核项目 generation、请求 revision、输入快照
  → 收集受影响的已注册绘制消费者
  → 各消费者准备 Pipeline、布局、驻留材质绑定候选
  → 帧边界再次确认请求有效
  → 一次提交 active 程序快照及全部受影响消费者状态
  → 再通知 Inspector 等观察者

目标重建 → 获取当前 active 程序快照 → 针对新 RenderPass 创建绘制资源
         （不重新编译，不切换程序版本，不默认退回内嵌程序）
```

候选准备使用 Result，失败由 RAII 释放候选；任一受影响的活动消费者失败，旧程序与旧绘制资源一起保留。
提交阶段只做预先准备好的状态交换，不在中途进行可能失败的 GPU 创建、容器扩容或 UI 回调。
多帧准备若与目标重建交错，需复核目标 generation 并重建失效候选；不能把旧 RenderPass 的 Pipeline 安装到新目标。
旧提交继续由 FrameSlot 保留实际 Pipeline／Layout／材质绑定，完成后释放，不引入通用退休队列或发布前 GPU 全局等待。
ShaderModule 只用于 Pipeline 创建，不因程序资产存在就长期缓存所有 module。
设备丢失沿现有退出清理策略处理；上述候选事务不承诺已退休交换链可回滚，WSI 恢复仍为独立专项。

#### 分步迁移与验收

1. **程序描述与不可变 CPU 版本**：随项目 Shader 资产接入，内置材质与项目程序统一描述／加载入口；
   测试稳定身份、依赖、编译失败、产物损坏和过期结果拒绝。禁止让导入线程创建 GPU 对象。
2. **渲染域发布与目标生命周期分离**：将 m_material_shaders 移出 SceneRenderer，移除旧字段与旁路接口，
   MaterialRenderer 只消费版本快照；保持已有材质整批候选语义，不复制一套材质准备缓存。
   验证热更新后 resize／MSAA／RenderPass 重建继续使用成功版本，失败不出现“新程序配旧绑定”。
3. **真实多消费者与关闭边界**：在第二个 View／pass 实际接入时扩展注册与事务范围，不预建空泛订阅框架。
   测试一个消费者准备失败时全部保留旧版、目标重建期间候选失效、同版本重复发布不重建、旧帧完成后回收；
   项目关闭／切换使旧 generation 的任务失效，先停止交付再回收 GPU 消费者，Device 最后释放。

每步同时迁移生产消费者与测试；只保留独立验收所需的文档，不以拆文件数量或增加 Manager 数量作为架构改进指标。

### 可失败创建 API 与消费者迁移（热更新前置）

当前已完成的边界：

- CPU 反射、布局、specialization、PipelineKey 校验和公共文件 I/O 返回 Result，不发布部分结果。
- Shader／Pipeline、Descriptor、Sampler、RenderPass／target、Comet 侧 ImGui 初始化返回完整 GPU 候选，失败由 RAII 回收。
- 资产 GPU 发布、调试缓冲扩容与离屏 resize 区分普通失败和 DeviceLost；前者保持原有降级，后者交给应用退出清理。
- WSI 创建／重建、acquire、present 已返回显式结果；退休交换链不重新发布。
- Queue／CommandContext／UploadBatch 和帧提交只在成功后登记 completion、serial 与资源保活；关闭等待失败不阻断析构。
- Scene／Project 解析、项目资产路径与应用工厂返回公共 Result；Open／Save／Play 消费失败结果，保留已有状态。
  JSON 数据校验直接返回 Result；生命周期钩子也返回 Result，入口不再捕获异常。未迁移路径仍可能异常终止，不承诺所有接口 noexcept 或所有故障都能有序退出。

现行职责、所有权和测试边界统一见[渲染资源所有权](architecture/rendering-ownership.md)，此处只保留后续验收项：

1. **命令录制／同步对象创建／运行期等待**：按实际消费者继续检查异常边界；每步同时迁移接口、生产调用方及测试，
   不保留可被业务绕回使用的旧入口，也不承诺所有函数 noexcept。
2. **ImGui 第三方后端失败（暂缓）**：先明确局部资源接管／释放和中断策略，再接错误回调；
   Init 的 bool 不覆盖全部 Vulkan 失败，不能直接抛异常跳过局部资源释放，不以修改第三方源码掩盖边界。
3. **WSI 扩展恢复**：无呈现退避、dependent 重试和 SurfaceLost 重建已接通；
   后续按平台验证设备丢失／呈现队列不兼容时的重新初始化，不将当前退出清理描述为设备恢复。
4. **原生数据边界**：PipelineConfig、viewport/scissor 按真实消费者整理；不复制全部 Vulkan 类型或预建多后端框架。
   ImGui Vulkan 适配仍允许在私有实现中使用原生接口。

公共 Result 位于无资产／Vulkan 依赖的 common 层；GraphicsError 与 GpuResourceResult 位于 graphics/result.h。
后者保留原生错误码，诊断字符串按需生成，不为统一外观改变提交失败路径的分配行为。
CPU 编译工具不依赖 GPU 模块；编译诊断、业务错误和原生结果保留各自的信息量。
公共创建／校验结果必须被消费，不能仅将 throw 移到 helper 或宏中，也不以全局禁用异常代替接口设计。
第三方异常在适配边界转换；不可承诺恢复的内存耗尽、析构保护和内部不变量错误另行处理。

失败策略由 owner 决定：启动所需资源创建失败应返回启动失败并正常清理；运行中候选失败保留旧版本，
无旧版本则明确跳过／报告，不伪装成功；设备丢失与资源不足保留区别，不隐式无限重试。
缓存只插入成功候选，失败不破坏已有项；旧 Shader／Layout／Pipeline／材质版本仍由在途帧持有。

验收：坏字节码、布局／specialization 不匹配和可控 GPU 创建失败均返回可诊断结果；
原对象与缓存保持有效，失败候选无泄漏；首次创建失败不发布半初始化对象；启动清理、运行中替换、帧保活测试通过。
每个子项通过调用点审查确认所有消费者检查结果，避免新旧错误协议长期并存；后续按当前架构推进，不逐提交照搬旧分支。

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

当前对象缓存已按完整内容／状态判等，弱引用不延长 GPU 对象寿命；驱动缓存已接恢复与关闭原子保存。当前边界及后续重点：

- 驱动 PipelineCache blob 用于跨进程加速，不代替对象 key。放在 .comet/cache/vulkan 或平台缓存，
  校验 header size/version、vendorID、deviceID、pipelineCacheUUID，以及 envelope 长度/校验和。
  损坏或不兼容回退空 cache，不影响启动。
- 当前关闭自动保存，可由 owner 显式保存；编译批次后节流按实际需要接入，不每帧写磁盘。
  Pipeline 创建/合并/保存由同一 owner 串行访问；后台 ShaderCompiler 不直接操作 Vulkan cache。
- 测试 key 等价性、兼容性和损坏输入；cold/warm 性能只做测量，不要求固定加速比例。
- 接入 Shader 发布时，MaterialRenderer 的 GPU 材质缓存必须同时跟踪 PipelineState 版本，不能只比较 PreparedMaterial。
  当前 key 复制字节码保证完整判等；若实际测量出现开销，再共享不可变代码，不能退化为 hash-only。

### GPU 资源、同步与 WSI

现有 Device、FrameScheduler、UploadManager 继续演进，不为目标名称再包一层。

- Device 管 logical device、queues、allocator、能力和设备缓存，不拥有所有资产、FrameSlot 或业务 target。
  设备候选按实际 RGBA16F 场景格式检查混合、采样与 MSAA；与目标创建复用 graphics 层校验，最终输出另查单采样能力。
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
  启动输出偏好与重建固定组合分开传递，固定组合不可用不回退为另一种编码。
- **WSI 无呈现恢复**：传入非空 oldSwapchain 调用创建后，无论成功失败，旧交换链都已退休。
  当前 Presentation 已区分 no-present／dependent／surface 恢复，暂时错误有界退避，失败不从退休对象 acquire。
  创建失败后的重试使用空 oldSwapchain；dependent 重建失败复用成功新代，SurfaceLost 重建并校验新 surface。
  旧资源仍须等待 graphics/present completion，再按 framebuffer → view → swapchain 顺序释放。
  规则来源：[Khronos](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html)。

### RenderGraph 与多 pass

- 当前有序图已用于 app/editor 的方向光阴影、HDR 场景、可选 Bloom 与显示输出 pass；详细录制与失败契约见[渲染所有权](architecture/rendering-ownership.md#有序-rendergraph)。
  当前顺序为：阴影 → 场景颜色（清屏、天空背景、材质网格、辅助线、按需 MSAA resolve）
  → 可选 Bloom（高亮提取、横向模糊、纵向模糊）→ OutputPass（合成、曝光、显示映射／编码）→ 编辑器 UI → 呈现。
  SkyboxPass 等绘制类不一定是独立图节点；当前天空背景和辅助线都在 scene 节点内。图按声明顺序执行并编译资源屏障，
  不自动重排所有业务阶段。后续多 pass 优先复用现有图，不把当前单 queue 实现描述成自动多队列调度器。
- Pass 声明读写 usage/subresource；imported/exported 资源明确边界状态，tracker 编译 Barrier2。
  Image 不保存单一全局 current_layout；状态属于录制/编译上下文，持久资源在提交边界交接 handoff state。
- 处理 layout 变化、RAW/WAR/WAW 和 ownership，兼容 read-after-read 不机械加全 barrier。
  跨 queue family 要成对 release/acquire + semaphore；未知输入状态或绕过 tracker 的操作必须明确声明或拒绝。
- Synchronization 2 / Timeline 已启用；API version 为 1.3 不代表所有可选 feature 自动启用。
- Dynamic Rendering 在真实多 pass/attachment 需求下评估，不为 API 更换重写阶段 4。
  检查显式 feature、ImGui/MSAA/resize、调试工具和目标 GPU；可按 pass 保留传统 RenderPass。
- 有界 Forward Lighting、三类 LightComponent、单方向光阴影、PBR/base-color、材质编辑、环境光照／IBL、Bloom 与有界渲染诊断已接通；下一步核对阶段性审查与代表场景测量。
  纹理受光复用材质准备、资产引用与 Shader 发布链路；仅保留 unlit_color 与 pbr 模板，旧双纹理混合和 Lambert Shader 已清理。
  地面材质改为 ground.mat，沿用原资产 ID，使用纯色非金属 PBR；不承诺与 Lambert 像素等价。
  PBR 使用线性 base_color 乘采样纹理，不继承旧 blend 参数；无纹理使用白色默认绑定。
  先完成小型 forward 场景，不一次构建完整 deferred renderer。
- 环境照明按下节独立验收；天空盒、IBL 和 bloom 是不同职责，不把环境贴图套到普通 Mesh 材质上，
  也不与显示器 HDR 开关混为一谈。
- 显示输出已支持启动时选择 SDR / 扩展线性 HDR，默认 SDR；后续按真实需求增加显示器 headroom／白点校准、
  HDR10/PQ、跨屏及系统模式切换后的安全重建，再处理编辑器 HDR 视口与 UI 亮度合成。不是下一项光源迁移的前置条件。
  内部线性 HDR 场景目标与显示器 HDR 输出分开：当前 SDR 也使用浮点场景目标；OutputPass 是最终显示步骤，不是可选 HDR 特效。
  保持线性光照／合成、动态范围和一次正确的输出编码；曝光 1 是中性倍率，不是关闭显示映射。
  不为增加开关提前建立 LDR 双管线；独立全屏输出有成本，后续仅依据实际 GPU 测量评估融合或轻量路径，不能丢失色彩转换职责。
- 已接通低频 GPU memory budget、手动 allocation dump、CPU 循环分段和场景图 CPU/GPU 计时；范围与延迟见[渲染诊断](architecture/rendering-ownership.md#渲染诊断)。代表场景测量须关闭 validation，区分等待、CPU 录制和 GPU，不凭 FPS 直接决定引入 RenderThread。
- 后处理外观参数属于项目内容，当前按场景保存；引擎提供算法与校验，编辑器提供内容编辑。跨场景预设、相机覆盖、局部区域和画质降级按真实需求扩展，不提前引入全局配置与场景之间的多级覆盖。
  纯色背景也已归属场景环境，支持保存、预览和撤销；HDR 输出、MSAA 等仍是启动策略，不与场景外观编辑混为一谈。

#### 对象／材质多 pass（后续按需扩展）

当前同一 Mesh 已可参与阴影与主材质绘制，但阴影使用固定程序，材质主绘制还没有用户定义的 pass 列表。
Scene 保存外观数据，不持有 Pass 实例／执行序列；MeshRenderer 引用网格、材质及未来必要的效果选择数据，
渲染管线统一安排阶段。先绘制这一阶段的相关物体，再消费阶段输出，而不是每个物体独立执行完整屏幕后处理链。
材质的多次绘制不必对应多个 Vulkan RenderPass／图节点；不增加附件依赖时可留在同一节点，
需要中间纹理或跨阶段读写时再声明图节点，特殊的物体内绘制顺序由阶段契约明确。

按独立验收项推进，不作为本轮背景色／撤销改造的前置条件：

1. **材质阶段契约**：结合项目 Shader／程序资产，按实际需求增加主绘制、阴影、深度、遮罩等阶段变体。
   明确阶段输入、附件与渲染状态；顶点变形／透明裁切等在相关阶段保持一致，不直接把任意 Shader 当作通用 pass。
2. **对象限定效果**：先以一个真实的对象描边／遮罩效果验收对象选择、深度遮挡、目标尺寸与合成位置。
   物体输出颜色／遮罩，图编排模糊／合成；共享算法与资源契约，不把现有全屏 BloomPass 实例直接挂到 MeshRenderer。
3. **自定义效果链**：有实际组合需求后再扩展，明确 HDR 线性阶段还是显示映射之后执行，并声明读写资源、顺序与失败策略。
   覆盖 resize、在途保活、关闭后无多余绘制，以及场景内容的保存／撤销；不提前开放任意 Pass 拖拽列表。

### 环境照明与天空盒

当前已接天空盒和全局 IBL；关闭环境或缺失资源时保留直接光基线，不依赖曝光或 bloom 补偿缺失照明。

1. **场景环境设置（已接通）**：单一场景级配置保存环境引用、背景／照明独立开关及强度与共享 Y 旋转。
   旧场景缺少 lighting 字段时默认关闭，新 demo 显式开启；不复用背景强度控制照明。
   持久化、Inspector 和 app/editor 提取消费一并接通；不把环境配置复制到每个材质，不要求先创建通用 System。
   缺省关闭保持旧场景外观，缺失的显式环境引用需诊断；不在 PBR 中加入无法配置的常量 ambient 或最低亮度。
2. **环境资产与背景（基础链路已接通）**：独立 Environment 类型接 2:1 Radiance HDR、RGBA16F cubemap／背景 mip 上传与采样。
   背景单面最高 2048²；源图与 mip 都保留线性 HDR 能量，普通背景 mip 不作为粗糙度预滤波结果。
   SkyboxPass 只绘制背景，随相机旋转而非平移，不覆盖几何；复用现有 HDR 目标、RenderGraph 和帧资源保活。
   首次需求与驻留重载共用 ImportService：环境 Artifact 按输入内容和算法版本校验，后台读取／转换／原子写缓存，owner 校验资产 revision 后发布 GPU 对象。
   队列按预估环境工作集预留 CPU 字节，完成候选在发布／丢弃前仍占预算；排队后输入增长超预算时拒绝，不突破预约。
   场景级环境引用可缺失并回退纯色，app 必需 Mesh／Material 失败仍阻止启动，DeviceLost 不降级。
   GPU 创建、普通纹理首次加载和外部文件复制仍同步；上传分片、更多类型的字节预算与取消按实测推进。
   IBL 已在 v2 产物协议中扩展 irradiance／prefilter／LUT，整组校验、整代发布，不另建并行导入链路。EXR、六面图片及更高背景分辨率按实际内容需求扩展。
3. **PBR 环境光照（已接通）**：16² 漫反射、128² GGX 镜面预滤波和 128² BRDF LUT，按金属度／粗糙度计算环境贡献。
   单散射 split-sum 与直接光使用相同 GGX/Smith 约定。预计算在后台导入／准备阶段完成，不放在每帧关键路径。
   IBL 可在隐藏天空盒时继续照明，天空盒可显示而关闭 IBL；这不包含局部反射探针、动态 GI 或光线追踪。

验收：关闭环境时保留直接光基线；非零均匀环境下背光非金属获得漫反射、金属获得粗糙度相关镜面贡献；
背景与照明开关、强度／旋转、保存重开、SDR／HDR 色彩路径和资源替换／失败保留均通过回归；与 Bloom 独立控制和验收。

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
- app 与 editor 已共用项目与启动场景数据；app 对仓库 demo 的固定 UUID 旋转仅是演示行为，
  外部项目和 editor Play 不执行它。通用 System／脚本接入后统一 app／Play 行为注册、场景替换和生命周期，
  移除 app 内的演示 UUID 绑定；本轮不为示例旋转添加专用组件或空壳 System。
- EditorMode 只含 Edit/Play；RuntimeState（Running/Paused）与之正交，支持暂停/单步，不增加 EditorMode::Paused。
- 当前 Engine::run 管循环、私有 tick 推进单帧；on_update 后准备帧，帧就绪才执行 on_frame_ready，然后提取当前场景并渲染。
  两个函数仅在 run 调用期间借用；编辑器请求执行与后台维护在 on_update，UI/请求收集/即时属性与视口更新在 on_frame_ready。无通用阶段注册表或预留 Late 钩子。
- 后续按真实消费者加入固定步长、暂停／单步与 System 调度，应用生命周期钩子不兼任通用调度协议。
  阶段和依赖以物理、动画、相机等实际需求为依据，不新增空壳 System 或通用 EventBus。
  验收包含更新顺序、场景替换、异常清理、回调寿命，以及模拟暂停时编辑器与资产维护继续运行。
- Scene 已维护 ID／UUID／父子索引，按本地 TRS 与父级版本增量重算矩阵，单个查询只检查祖先链。
  为兼容可变引用写入，场景提取前仍需 O(N) 值检查；未来 TransformSystem 收口修改入口后再改为 dirty 集合遍历，
  不能仅在 get_component 时标脏。
  先定义组件修改何时生效，明确即时世界矩阵查询与同步后只读快照的区别，再收敛 getter 内的更新副作用。
  写入契约应同时覆盖 Inspector／Gizmo／Undo／加载／运行逻辑；长期持有引用的兼容方案不能未经迁移直接删除。
  验收：当帧修改可见、父级变化影响后代、场景替换无旧缓存、同步后批量读取不重复分配或更新；
  仅将当前算法移入 TransformSystem 不算完成该项。
  持续用直接 TRS 组合校验缓存结果；结合实际场景规模衡量值检查、索引和矩阵重算的成本。
- 任务并行必须声明组件读写集合和 phase/barrier，不任意并发执行脚本回调。
- 物理（候选 Jolt/Bullet）、音频（候选 miniaudio）、动画/AI 与 Runtime UI；引入依赖前按实际 demo 需要评估。
- 脚本、Inspector、Serializer、Undo/Redo 共用稳定 ComponentDescriptor/PropertyDescriptor。
  类型/字段 ID 不用 typeid 名称或裸 offset；使用类型化访问器，并区分 editable/serializable/transient 等属性。
- C++26 反射仅在所有目标工具链、标准库与依赖验证通过后评估；只替换 descriptor 生成后端，不重写业务消费者。

验收：角色移动、碰撞、声音的小 demo 可运行，Play/Edit 隔离稳定；固定输入/时间步与单线程回退可重复。

## 阶段 7：项目格式与发布

- 已提前补齐最小项目入口：`project.json` 保存版本、名称和可选启动场景；app／editor 接受项目目录／描述文件路径。
  项目 roots、资产索引、缓存、布局及 SceneDocument 均绑定同一项目；相对场景路径基于 assets，拒绝越界 Open/Save。
  无参数打开仓库 `demo/` 内的独立示例项目，显式无效项目不回退示例；空启动场景创建空文档，不硬编码示例资源。
  app 同样读取启动场景并按组件引用加载资产，启动时同步确保 Mesh Artifact；指定场景／必需资源损坏时报错退出，
  editor 保留缺失引用供修复。app 尚未去掉开发期导入器与源码目录依赖，不等同于 Shipping Manifest 加载。
- 编辑器内增加 File → Open Project，与现有 Open Scene 分开；选择目录或 project.json，并提供最近项目列表。
  切换前处理未保存场景和活动属性／Gizmo 编辑，Play 模式先退出；取消或新项目校验失败时保持当前项目不变。
  第一版可通过重启编辑器进程打开新项目，避免直接交换活动 AssetManager；若支持原地切换，须先排空旧任务和在途帧，
  再释放旧场景／选择／历史／资产缓存与监视器，保存旧布局并加载新布局，禁止旧项目结果发布到新项目。
  验收：无需命令行即可选项目；取消／失败不丢修改；同 Handle 的两个项目不串用资源或缓存。
- 后续扩展项目设置 UI、记录上次文档、Build Settings 和项目模板，去掉发布对源码目录的依赖。
  项目创建时生成 project.json；项目设置修改并校验成功后自动原子保存，不单独增加 Save Project 按钮。
  Save Scene 仅保存场景，不连带重写项目描述；只有启动场景等项目设置变化才保存项目，编辑器本地状态仍放 .comet/。
  编辑器／引擎自带 Profile、字体和 Shader 与项目内容保持分离。
- **设置归属与渐进迁移**：背景、环境照明、Bloom／曝光等外观属于场景内容；跨场景预设／相机覆盖按实际需求增加。
  MSAA／各向异性过滤属于项目画质默认值与运行时画质策略；输出模式／VSync／窗口属于应用默认策略、用户偏好和设备协商，
  编辑器窗口与预览偏好独立。不把所有 render 配置机械搬入 `.scene`，也不因 JSON/YAML 格式决定归属。
  项目设置 UI 接入时可先提供“重启生效”，再逐项实现安全的实时切换；HDR headroom／白点随输出校准完善，不当作场景曝光。
  在途帧调度、底层格式与设备限制、诊断／验证等仍由引擎运行或开发配置管理；本轮不迁移全部启动配置。
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
