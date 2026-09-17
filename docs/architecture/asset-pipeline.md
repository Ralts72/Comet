# 资产管线

本文描述当前实现；未落地的扩展统一放在[路线图](../engine-roadmap.md)。

## 目录和身份

项目路径相对项目根目录；仓库自带示例的项目根为 `demo/`，外部项目不依赖此目录名。

- `assets/`：项目源资产和相邻 `.meta`，进入版本控制。
- `.comet/cache/`：可重建导入产物；`.comet/editor/imgui.ini`：本机编辑器布局。两者不提交。
- `project.json`：项目版本、名称和启动场景；Project 只读取和校验描述，ProjectPaths 统一目录及资产路径边界。
  编辑器从传入的项目目录／描述文件启动，无参数才打开仓库 `demo/` 示例。项目无须复制引擎／编辑器自带的 config、字体、Shader。

仓库的 `editor/resources/` 是编辑器私有资源，不在项目根目录内，也不进入 AssetDatabase 或生成 `.meta`。

当前 .meta v3 使用 JSON，保存 version/guid/type；Texture 另有 importer.color_space（srgb/linear）和 flip_y。
未知字段、缺失字段或不匹配的类型/设置会被拒绝。新 Texture 默认为 srgb、不翻转。
Handle 可随源文件和 .meta 一起移动，但同一 Handle 不可改变 AssetType；类型转换需要新身份。

## 按问题找入口

以下路径均相对 `engine/src/`。

| 入口 | 负责 | 不负责 |
| --- | --- | --- |
| `asset/asset_manager.h` / AssetManager | 协调加载、重载、导入与发布 | 自己解析 glTF、持有 Device |
| `asset/database.h` / AssetDatabase | 扫描、身份、revision、依赖索引 | GPU 对象缓存 |
| `asset/registry.h` / AssetRegistry | 唯一 Handle → Runtime 对象缓存 | 路径、文件解析 |
| `asset/import/import_service.h` / ImportService | 检查输入、准备 Mesh Artifact | GPU 创建 |
| `asset/import/asset_task_queue.h` / AssetTaskQueue | 有界队列、请求合并、Worker 完成、预算与关闭等待 | 解析文件、创建 GPU 对象、发布资产 |
| `asset/import/*_importer.h` | 外部格式 → Comet CPU 数据 | 资产发布策略 |
| `asset/artifact/mesh_artifact.h` | 版本化 Mesh 产物读写与原子发布 | 读取源 glTF 判断过期 |
| `asset/serialization/` | .meta/.mat 的严格读写 | 渲染绑定 |
| `asset/source_operations.h` | 源文件移动／外部导入事务、依赖收集、校验和回滚 | GPU 对象、Artifact 编码 |
| `asset/source_monitor.h` | 低频观察源文件树变化 | 分配身份、启动 Importer |
| `render/resource/resource_factory.h` / RenderResourceFactory | 窄的 CPU → Runtime Mesh/Texture 创建接口 | 材质组装、身份与缓存 |
| `render/resource/resource_manager.h` / ResourceManager | 实现工厂，管理上传和 Shader/Sampler 共享资源 | AssetHandle 与 .meta |

Mesh/Texture 的 CPU DTO、Runtime 对象和工厂集中于 render/resource，但仍按类型分文件：
二者的导入、GPU 布局和产物契约不同，不为减少文件数合并为大分支工厂。
MaterialData 是可序列化的 template + Texture Handle／scalar／四分量 vector 参数；Runtime Material 保存解析后的 Texture 引用和数值。
`.mat` 的 `properties` 每项带 `type`：texture 使用 `asset`，scalar/vector 使用 `value`；vector 固定四个有限浮点数。
参数名跨类型唯一，未知字段与非法数值被拒绝；只有 Texture Handle 参与资产依赖索引。
未提供的数值由手工 MaterialLayout 的默认值补齐；Shader 布局和 GPU 参数块不写入资产文件。
Scene Serializer 和 ConfigLoader 留在各自模块，不强行纳入 AssetManager。

## 失败返回契约

`common/result.h` 的 `Result<T>` 表达一次操作的成功值或错误字符串；无返回数据的写入使用 `Result<void>`。
先检查结果，再访问 `value()` 或 `error()`；二者互斥，不能用空字符串或空数据判断成功。
它不记录日志、不包装 try/catch、不携带 Vulkan 类型，仍兼容 C++20。

- Mesh/Texture Importer、输入指纹采集、ImportService 构建、MeshArtifact 发布、.mat/.meta 读写及数据库更新统一返回该类型。
- `MeshImportData` 只是 CPU 网格和源依赖的数据包；外层 `Result<MeshImportData>` 才表示操作成败。
- 导入器及公共文件 I/O 直接返回预期失败；共享 JSON 校验的异常仍在序列化／产物出口转换。
  AssetManager 输出操作日志，AssetDatabase 聚合扫描问题，Inspector 保存字段错误，不在底层重复打印。
- `asset/serialization/json_serialization.h` 共用文件读写及失败结果转换，`common/json.h/.cpp` 提供 JSON 校验与输出；
  Material/Metadata 的 encode/decode 只维护各自字段规则。通过普通函数组合复用，不继承序列化器基类，
  不在公开序列化接口中暴露 JSON 类型，也不保存或异步调度编码／解码函数。
  `common/file_io` 读取返回 `Result<string>`，文本／二进制原子写入返回 `Result<void>`；资产层直接检查或传递结果。
  原子写入在同目录临时文件写入、flush、close 均成功后才替换目标，私有 RAII guard 在失败时尝试删除临时文件。
  原子可见性不等于断电持久性，也不保证目录权限变化后一定能清理；不构成跨文件事务。
  Scene 同样复用 JSON 工具，但不依赖资产序列化模块；simdjson 是 engine 的显式私有依赖。
  SceneSerializer 与 Project::load 同样返回公共 Result，在各自边界转换 I/O 和 JSON 数据错误；其他异常继续向生命周期边界传播。
  `.scene` v2、`.mat` v2、`.meta` v3 为编辑器生成的 JSON；`project.json` v1 同样使用 JSON，Profile 继续使用 YAML。
  Project 直接复用 Json::Context，不依赖资产序列化器；目前只读取项目描述，项目设置 UI/自动保存尚未实现。
  当前尚未发布，FORMAT_VERSION 只用于严格检测；版本不匹配直接报错，不兼容旧 YAML，不提供迁移或旧格式备份。
- Worker 候选保存结果及 Handle/revision；owner 先验 revision，再处理失败或发布成功值。
  非预期异常由任务 future 传递，在完成处理边界报告并回收对应任务，不会留下永久进行中的任务。
  AssetTaskQueue 持有任务和候选，仅在 owner 调用 process_completions 时交付当前 revision 的结果；
  AssetManager 决定 Artifact／Runtime 发布和依赖刷新。发布回调执行期间继续占槽，发布异常也会回收已消费任务。
  队列是 Manager 的内部资产执行模块，不导出为引擎公共服务；Limits／Status／CompletionBudget 的公共契约由 Manager 定义。
  当前保留资产类型、revision 和 Force 请求语义，不宣称它是无业务依赖的通用队列。
- 缓存查找仍用 optional 表示未命中；Runtime 加载入口仍返回共享对象或空值并负责诊断；
  扫描保留可包含多条问题的 AssetScanReport，GPU 创建保留 GpuResourceResult 的 Vulkan 错误码。

这不是全局禁用异常：分配失败、程序错误、生命周期和第三方异常仍需要外层隔离。
失败结果不承诺跨文件／数据库／GPU 的全局事务；下述原子发布与旧 Runtime 保留边界不变。

## 三种加载路径

首次加载共用 Handle／缓存／类型校验、候选创建后的 revision 复核及 Registry 注册。
创建期间使用 AssetRecord 副本，避免依赖加载或回调刷新数据库后继续访问失效记录。
Mesh 的 Artifact 读取、Texture 设置检查和 Material 依赖解析仍由各类型维护。
材质 Reload／Update 共用依赖更新与 Runtime 发布；Update 仍先创建候选、原子保存文件，再更新依赖并发布。
两者均在创建前保存记录快照/revision，依赖加载后先复核版本；过期候选不写文件、不更新依赖、不发布 Runtime。
这些步骤不构成跨文件／数据库／Registry 的全局事务。

```text
源 glTF/.glb + 外部 buffer
  → ImportService → MeshImporter → MeshArtifact::publish_atomic()
  → AssetManager::load_mesh() → MeshArtifact::load() → MeshData
  → RenderResourceFactory → Runtime Mesh → AssetRegistry

Texture 源文件 + TextureImportSettings
  → AssetManager::load_texture() → TextureImporter → TextureData
  → RenderResourceFactory → Runtime Texture → AssetRegistry

原生 .mat
  → AssetManager::load_material() → MaterialSerializer → MaterialData
  → 解析 Texture Handles → Runtime Material → AssetRegistry
```

- Mesh Runtime 只读已发布 Artifact，不校验源文件、不回退解析 glTF。缓存缺失/损坏需先导入。
  Project 模型拖入 Edit Viewport 时，复用 EditorAssets::load_reference 校验类型与 revision 后加载 Mesh，成功才执行场景创建命令。
  新实体材质引用暂留空，用户通过 Inspector 指定；内置基础材质接通后自动使用内置，不从项目描述读取默认值。
  此入口不触发同步源导入；后台导入未完成且无可用产物时，用户等待后重试。Undo 只撤销实体，不卸载共享资源。
  Inspector 组件引用的下拉／拖放也在 UI 后通过 EditorAssets::load_reference 校验类型与 revision 后加载，成功才提交属性命令。
  编辑器启动通过 SceneDocument::open 读取项目默认 `.scene`，与手工打开共用按引用加载和后台补导入恢复，不再有同步示例 bootstrap。
  材质纹理槽继续走共享材质文件更新。
- MeshArtifact 保存源路径/内容指纹，ImportService 据此判断重建；.bin 是辅助输入，不单独生成 Handle/.meta。
- Texture 仍直接解码源文件，TextureArtifact 后置；不要把当前链路误读为所有资产均有 Artifact。
- MeshImporter 当前只支持一个 glTF mesh，合并 triangle-list primitives；POSITION 必需，
  缺 NORMAL 生成平滑法线，缺 TEXCOORD_0 填零。node transform/material/animation/skin/morph/多 mesh 子资产尚未导入。
- fastgltf 类型不进入 Comet 公共头文件。

## 扫描、后台刷新与发布

场景打开和 Edit/Play 激活前，EditorAssets 通过 ComponentRegistry::collect_asset_references 收集、去重 Handle／期望类型，
再调用 AssetManager::ensure_loaded。描述符只发现引用，Manager 不依赖 Scene；Serializer 不参与资源加载。
ensure_loaded 复用具体 load_* 与唯一 Registry，先核验身份／类型；Mesh 只读已发布 Artifact。
load_*、重载和编辑返回 Result<shared_ptr<T>, Error>，ensure_loaded 返回 Result<void, Error>；失败不是空资源。材质依赖补充属性上下文但保留原生错误码。EditorAssets::prepare_scene 对普通加载失败计数并保留引用，DeviceLost 则返回失败，阻止场景安装。
EditorAssets::load_reference 额外保留 UI 的 revision 和清空引用语义，不再重复类型分发。

```text
Project Refresh / AssetSourceMonitor
  → AssetManager::scan() → AssetDatabase 候选快照
  → 提交变化集与单调 revision
  → 已加载 Mesh/Texture/Material：Worker 生成 CPU 候选
  → process_completions()：owner 验 revision
  → Mesh 原子发布 Artifact / 更新源依赖
  → 尝试创建 Runtime GPU 对象 → 再验票 → 替换 Registry
```

扫描区分单文件诊断与无法信任的全局快照：坏 .meta 的条目不会凭空沿用旧 AssetRecord；
目录发现不完整或同一 Handle 改变类型时拒绝整个候选快照。revision 是进程内版本，不持久化，也不是内容 hash。
删除会卸载对应 Runtime 对象及受影响依赖。已加载 Material 的修改或移动只排队读取数据，
在 process_completions 中加载纹理依赖、创建候选材质并发布；扫描和 Worker 不创建 GPU 资源。
依赖创建期间再次修改材质时，旧版本候选不会覆盖 Registry，最新请求继续通过同一队列处理。
数据库扫描仍同步读取材质依赖以建立索引；上述分离针对 Runtime 重建，并未消除主线程文件读取。

Worker 只接收路径、Handle、revision、导入设置的值拷贝，不访问数据库、Registry、ImGui 或 Vulkan。
过期候选丢弃；解码/GPU 创建失败不替换旧 Runtime 对象。Mesh Artifact 与 Runtime 发布是两个边界：
Artifact 已成功发布后若 GPU 创建失败，旧 Runtime Mesh 仍保留，磁盘产物可以已更新。
普通创建失败允许继续使用旧对象；DeviceLost 只保留所有权以便正常清理，不表示旧 GPU 对象仍可继续使用。
完成处理仅捕获 Worker future 异常，owner 发布异常向应用传播；无论发布是否成功，作用域清理都会释放已消费的任务槽位。
process_completions 返回 Result<vector<AssetHandle>, Error>：成功值中 Mesh 指 Artifact，Texture/Material 指 Runtime；缓存复用、普通失败或过期任务不算发布。直接 Mesh/Texture GPU 创建遇到 DeviceLost 返回带原生码的错误，停止本批剩余发布并回收当前槽位；已发布产物不回滚，失败结果不携带成功 Handle 列表。EditorAssets::update 与应用 on_update 显式向上传递该错误。材质依赖加载同样返回原生错误；前台引用赋值、Inspector 编辑、候选准备和 demo 必需资产加载已接通该协议。标准库或非预期工厂异常不在此捕获。
EditorAssets 保留扫描、发布及显式纹理重导入的变更 Handle。活动场景安装或编辑历史变化时收集一次引用集合；后台发布不再重新遍历组件。
on_update 在获取渲染帧前按变更 Handle 及依赖闭包恢复缓存的引用。尚未解析的引用在明确扫描/发布事件后重试，以覆盖尚未建立依赖索引的坏材质；无新事件时不持续重试。
恢复按条数和时间软预算处理，默认 2 项/2 ms，未消费项留待后续更新；单次 GPU 创建不能被抢占。场景切换移除旧引用待办，初次候选准备仍是完整同步操作。引用恢复不改写组件或制造撤销记录。

EditorAssets 在成功提交扫描快照后收集 Mesh Handles，下一次 update 通过 `import_mesh_async(IfNeeded)`
提交后台检查／导入，不依赖选择或 UI 按钮。有效 Artifact 复用且不重写；缺失、损坏或过期时重建。
扫描先合并待导入请求，在 EditorAssets::update 中提交后台任务；默认场景即使缺少 Artifact 也能先打开，成功发布后再恢复引用。
Project 右键 Reimport 走 Force 模式；同 Handle + revision 请求合并，自动检查期间的强制重建意图不会丢失。
未加载模型只发布 Artifact 和源依赖，不分配 GPU；已加载模型继续安全替换 Runtime，失败保留旧对象。
扫描事件后会检查项目内所有已索引 Mesh，也覆盖尚未成功导入、未登记外部 buffer 依赖的模型；
无事件帧不遍历或检查产物，失败不会每帧自动重试。大项目的检查范围和发布预算仍需后续优化。

后台请求有两层数量限制：TaskScheduler 默认最多等待 128 个任务（不含正在执行的 Worker），
AssetManager 默认最多 8 个在途任务、128 个未派发请求，可通过构造参数调整。
同 Handle/revision 合并；同 Handle 的排队项替换为最新 revision，保留原排队位置。
Force 请求在接收时直接升级未派发的缓存检查，或为已提交的缓存检查接收一个后继任务；
若后继没有排队容量，当场返回 false，不先记意图再在完成处理时尝试排队。
同 Handle 的在途任务未回收前不派发后继，但其他 Handle 可以前进。派发前与发布前都验证 revision。
全局队列满时 try_submit 返回空，资产请求留在本地等待 process_completions 再派发，不在 owner 线程执行或等待容量。
本地请求队列满时返回 false。EditorAssets 保留自动 Mesh/显式 Force 待办；AssetManager 保留扫描触发的驻留资源刷新 Handle/revision，process_completions 释放容量后再提交。相同资产合并，删除/过期请求丢弃，Force 不被自动缓存检查覆盖。
待办只保存身份/版本/模式，不持有解码数据，也不绕过 Worker 队列限制；内容失败不走容量重试。直接调用底层 import_mesh_async 的消费者仍需处理 false。
get_async_status 仅供 owner 查询已提交未回收／未派发数量，不是 Worker 实时运行数。
每个在途槽持有 future 和独立 ImportResult；Worker 只写自己的结果，owner 在 future 就绪后才读取。
不再使用 Mesh／Texture 完成队列或完成 mutex，AsyncState 由 AssetManager 独占。
process_completions 默认共用最多 2 项、2 ms 的消费预算，可传 CompletionBudget 调整。
成功、失败、过期和缓存复用均计数；数量为零或时间非正暂停消费，但仍可派发等待任务。
正时间预算至少允许一个就绪结果前进；时间只在开启下一个结果前检查，不抢占单次文件替换、GPU 创建或依赖刷新。
预算外的就绪结果继续占在途槽，发布／丢弃后才回收；递归 process_completions 被拒绝。
当前不限制单任务字节数和时长；同步扫描、显式加载／导入不受完成预算约束；增量引用恢复使用单独的同类型软预算，不保证整帧上限。

依赖索引分两类：

- AssetHandle 依赖：例如 Material → Texture。
- Importer 源路径依赖：例如 Mesh → 外部 buffer；由成功导入或 Artifact 加载恢复。

数据库依赖查询返回借用 span，修改数据库后不可保留或继续遍历。
Texture 后台刷新和显式重导入共用 `reload_loaded_material_dependents()`：
先复制直接依赖 Handle，再重载已加载 Material；重载会重建依赖索引，因此不能直接迭代原始 span。
不主动加载尚未使用的材质；失败的材质保留旧 Runtime 对象与旧 Texture 引用。

## 编辑与移动

- 外部导入：Window 在 GLFW drop 回调中复制路径和逻辑坐标 → UI 后 Project 命中目标目录 → EditorAssets → AssetManager → AssetSourceOperations。
  只接收 PNG/JPEG 和 glTF/GLB；后者收集相对 buffer／图片，在 `.comet/cache/file-import/<批次>/` 准备完整副本并调用现有 Importer 校验。
  校验不创建 GPU 对象或发布 Artifact；同卷硬链接逐文件无覆盖发布，再扫描候选数据库生成新 `.meta`，成功才提交数据库快照。
  失败补偿回滚本批文件和生成的 sidecar；成功后 EditorAssets 复用扫描变化集排队后台导入，并同步 Monitor 基线。
  这不是整批文件的 OS 原子事务：进程崩溃／回滚自身失败可能留下文件，需诊断和后续恢复；跨卷或不支持硬链接的文件系统会明确失败。
  当前文件复制／校验同步执行；批量异步准备、取消、进度及崩溃恢复留待扩展。不复制外部身份，不自动创建实体，也不进入 Scene 历史。
- Material：Inspector 值变化 → 带 revision 的 AssetEdit 请求 → Editor 复核并调用 update_material → 构建候选 → 原子保存 .mat → 更新依赖 → 替换 Registry。
  Inspector 与渲染器共用 MaterialLayout::find_builtin 的只读描述，按布局显示纹理槽及标量／向量／颜色参数。
  缺省数值只显示默认值，不立即写回；必需纹理未补齐时保留面板草稿，完整后随一次实际变化自动发布。
  未完成草稿在加载其他资产或该资产新 revision 时丢弃；未知属性阻止发布，不自动删除用户字段。
  模板切换和资产撤销尚未接通；拖动中每次真实变化都提交，未做写入合并。
- Texture：设置变化 → AssetEdit 请求 → Editor 复核并调用 reimport_texture → 记录快照/revision → 解码/GPU 候选 → 复核 revision → 保存 .meta → 发布 Texture → 刷新已加载材质。
  创建期间输入变化则拒绝候选，不保存过期导入设置，也不替换旧 Runtime。
- 控件按变化事件提交，不逐帧保存；失败恢复旧控件值。加载/字段错误显示在 Inspector，更新日志只进入 Log。
- 编辑器的资产下拉控件和拖放载荷读取集中在 `editor/src/assets/asset_reference`；公共读取只验证载荷格式并复制数据，
  槽位类型、资产 revision、文档 generation 和提交时机仍由各接收方按业务校验。
- 移动：Project 提交 Handle 和项目相对目标 → 校验边界/扩展名/身份 → 成对移动 source/.meta →
  候选数据库扫描 → 可信则提交，否则补偿回滚。两个文件不能获得单次 OS 原子 rename；
  回滚自身失败必须报告具体诊断，不声称成功。普通 Mesh 移动不等于重写 glTF 外部 URI。
- 成功后保留 Selection，并向 Monitor 确认精确变动路径，避免再次识别自身写入。
- Project 只产生刷新／移动请求，由 Editor 在下一次 on_update 执行并回传扫描结果；Inspector 编辑、文件操作和引用加载同样不在获取 GPU 帧后执行。
  Inspector 自己以 Handle/revision 判断是否重新加载字段；无关扫描不清空缓存，选中资产变化后下一次显示时重读。
  失败加载也记录尝试过的 revision，避免每帧重试；可手动 Retry Load，或在新 revision 到来后自动重试。

## 生命周期与文件写入

AssetManager 持有数据库，借用 Registry、RenderResourceFactory 和 TaskScheduler；必须先于这些依赖销毁，
析构先取消未派发请求，再等待已提交任务并丢弃候选，不在析构中发布 GPU 对象。
Worker 闭包不持有 AsyncState，只与对应在途槽共享单个结果，future 完成建立结果读取的同步关系。
TaskScheduler 是通用固定 Worker 池，不认识资产；析构仍执行完已接收的任务，wait_idle 不代表资产层排队工作也已排空。
Registry 保存 Runtime 的共享引用；替换条目不影响仍持有旧对象的 Material 或在途帧。
GPU ready/retention 规则见[渲染所有权](rendering-ownership.md)，资产 revision 不代替 GPU completion。

Scene/.mat/.meta 的文本保存和 MeshArtifact 二进制发布共用临时文件替换机制，不直接截断正式文件。
缓存可以删除并重新导入，但不是身份或源文件的唯一副本；删除 editor 本地状态会丢失布局，不影响项目内容。
Sampler filter/wrap、mipmap 和格式迁移等未实现能力只在路线图维护。
