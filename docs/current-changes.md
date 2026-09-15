# 本轮架构优化与当前改动说明

日期：2026-09-15。

## 1. 范围与阅读方式

本文记录本轮相对基线提交 `d9d9441`（`refactor: separate presentation and scene resource lifecycles`）的全部源码、测试和文档改动，而不只是最后一次抽取公共逻辑的修改。提交后该基线仍保持不变，不以之后的 HEAD 重新解释本文。

当前工作区共涉及 119 个文件：109 个已跟踪文件的修改和 10 个新增文件，尚未提交。以下描述以本次复核的实现为准；不包含构建产物，也不修改 AGENTS.md。精确路径清单见第 13 节，数量不代表整体目标已完成。

本轮目标是：减少重复扫描和重复业务流程，明确资源所有权与失败边界，让恢复逻辑只有一个推进入口，同时避免提前引入通用任务框架或空壳 System。

| 领域 | 本轮实际变化 |
| --- | --- |
| Scene | 常驻身份与父子索引；TRS 值检查和父版本驱动的矩阵缓存 |
| Runtime | run 管循环、tick 显式排序 update/frame_ready/提取/绘制，贯通生命周期错误 |
| Asset | 提取私有任务队列；合并首次加载校验；补齐候选版本复核 |
| Graphics / Render | Surface 保活；分阶段 WSI 恢复；统一重建请求和 Result 传递 |
| Editor | 统一候选安装；UI 请求与执行分离；资产规则归 EditorAssets；ImGui 重建可重复执行 |
| Tests / Docs | 补充行为回归，迁移接口消费者，将剩余工作放回路线图 |

## 2. Scene：身份查找与层级维护

### 2.1 从临时扫描改为常驻索引

此前按 ID、UUID 查找实体、按父实体查找孩子都需要扫描组件集合；更新世界矩阵时还会临时重建身份和父子关系表。

现在 `Scene` 私有维护三份索引：

- `m_entities_by_id`：EntityId 到 EnTT 实体句柄。
- `m_entities_by_uuid`：EntityUuid 到 EnTT 实体句柄。
- `m_children_by_parent`：父 EntityId 到直接孩子句柄列表。

ID 和 UUID 查找变为哈希表平均 O(1) 查找。孩子查询只访问对应列表，随后按 ID 排序。根实体查询仍需扫描，但不再嵌套执行全场景身份查找；返回实体列表也按 ID 排序，避免哈希遍历顺序泄漏到调用方。

这些索引只属于运行时 Scene，不写入场景文件，没有修改 UUID 或场景序列化格式。

保留三份索引的原因不同：ID 服务父节点、矩阵和拾取定位，UUID 服务持久身份与撤销恢复，孩子索引避免层级面板和子树操作反复扫描全场景。它们不是三种重复身份；独立 EntityId 能否以后由引擎实体句柄替代，需要连同失效检测和消费者一起评估，本轮未迁移。

### 2.2 统一维护索引一致性

- 创建实体：先建立组件和两份身份索引，全部成功后才推进下一个 ID；异常时撤销已插入索引并销毁实体。
- 设置父实体：先完成新父节点孩子列表的可能分配，再修改关系并移除旧父索引；保留跨场景及环检测。
- 清除父实体：同时清除关系和旧父节点孩子记录。
- 删除实体：先收集完整子树，再解除子树根与外部父节点的关系，最后逆序删除实体及对应索引、矩阵缓存。

删除前完成遍历所需分配，避免收集过程中失败却已经删除半棵树；对子树内部不再逐个反复维护即将整体删除的兄弟列表。

该实现依赖现有约束：身份和层级由 Scene 接口维护，不能绕过这些接口直接破坏其内部关系。

换父节点时，已有孩子列表直接追加，新列表先完整构造再插入索引。实体创建涉及多个组件及身份索引，改由 ScopeExit 在未提交时撤销索引并销毁句柄，成功后 release；不再捕获后重抛。编辑器恢复实体树和新增组件快照也采用同一作用域清理机制。

## 3. Transform：少算矩阵，但仍兼容可变引用

### 3.1 最终采用的缓存方式

新增的是 Scene 私有 `TransformState`，不是对外新增一个业务组件。它记录上次本地 Transform、是否存在该组件、父 ID、父版本以及自身计算版本。

每次检查节点时，比较本地 translation、rotation、scale、组件存在性、父 ID 和父版本。只有其中之一变化或尚未初始化，才重新计算矩阵并增加自身版本。

```text
修改本地 TRS / 换父节点 / 父版本变化
  -> 节点检查发现变化
  -> 重算 world 与 camera 矩阵
  -> 自身版本递增
  -> 后代检查父版本时继续传播
```

`update_world_transforms()` 使用父节点先于孩子的迭代遍历，复用常驻父子索引，不再递归构造整套临时层级。返回值由 `void` 改为实际重算节点数，便于验证未变化节点没有重复计算。

### 3.2 两种读取路径

- 场景提取前：继续调用 `update_world_transforms()`，检查全部节点，只重算变化节点及受影响后代。
- 单实体即时查询：`get_world_matrix()` 只收集该实体祖先链，按根到叶检查和更新，不再触发全场景同步。

因此当前是 **O(N) 值检查 + 变化范围内矩阵重算**，不是仅遍历 dirty 集合的完整增量调度。单实体查询也仍有 O(层级深度) 检查和临时数组分配。

没有采用“获取可变组件引用时标脏”的方案：调用方可能长期保存 `TransformComponent&`，以后直接修改而不再调用 getter。最终的值比较能识别这种写入；删除再添加 Transform 也会重新同步。

### 3.3 相机矩阵与公共计算复用

普通世界矩阵继续包含完整父子 TRS。相机矩阵的朝向由父子旋转组合，不继承自身或祖先缩放；其位置取普通世界矩阵的位置，因此父缩放对孩子偏移的影响仍然保留。

计算本地矩阵时先组合一次不含缩放的 TR，再用已有 `Math::scale` 得到完整 TRS，避免为普通矩阵和相机矩阵重复组合平移、旋转。没有新增 Math 抽象或 TransformSystem。

Inspector 和编辑器资产属性比较同时改用 const 组件读取；真正写入仍走原有可变接口。这是只读语义修正，不是依赖 getter 标脏的机制。

## 4. Runtime：单帧推进和当帧数据链路

### 4.1 主循环与单帧职责

最终实现取消了中间方案中的 Prepare、Simulation、Late 阶段和回调注册表。Late 没有实际消费者，Application 阶段虚函数再包装到 Engine 注册表也重复表达了扩展入口。

Engine::run 同步运行主循环，在调用期间借用 update 和 frame_ready，不保存回调注册表。私有 tick 负责单帧；Application 的 on_update 处理运行逻辑，on_frame_ready 处理帧就绪后的编辑。编辑器在 on_update 中处理 Shader 重载、资产完成、模式请求和 FPS，在 on_frame_ready 显式执行 ImGui begin、UI 绘制、请求处理、视口更新、ImGui end 和反馈提交。

frame_ready/on_frame_ready 已补为 Result<void, Error>，给交互阶段的资源错误提供直接返回链路。该阶段发生失败时，Engine 先执行关闭准备，再保留原始错误返回；不提取/绘制、不重用已取得帧，并拒绝再次运行。新增故障注入覆盖原始错误码、没有 overlay 绘制、拒绝新任务/帧/再次运行。资产的可预期资源失败已接入此返回链路；未预期标准库异常不在此保证内。

Inspector 的材质更新/纹理重导入移除持久业务回调，沿用面板请求模式：绘制产生 AssetEdit（Handle、revision、类型专属前后值），Editor::process_editor_requests 转交 EditorAssets::apply_edit 复核版本后执行，complete_asset_edit 在失败且面板仍显示同一版本时恢复草稿。请求消费后清空，不在绘制控件中保存文件或创建 GPU 资源；既有实体属性编辑流程不变。UI 测试在绘制结束后显式消费请求并确认单次交付。前台 AssetManager 错误返回现已接通，Editor 在确认 Inspector 草稿结果后将设备错误向上传递。

资产编辑请求定义移到 editor/src/assets/asset_edit.h，不再由 InspectorPanel 定义业务协议。EditorAssets 的 apply_edit 替代原有 update_material/reimport_texture 两个公开入口，集中执行版本检查、类型分派、写入成功后的源监视确认及纹理引用刷新。Editor 不再读取数据库版本或拆解请求变体；没有新增管理类、通用分发框架或异常包装。新增回归验证已成功提交后重放旧请求不会改文件、资源或版本，以及删除资产后旧请求不会重建文件；既有纹理重导入和设备错误测试改走此入口。

该职责收敛完成后，dev-debug 全目标构建及 CTest 通过：677 个 GoogleTest 用例中 676 个通过、1 个原有采样器用例跳过，shader_build_contract 通过。桌面交互仍待单独验收，不以这些测试代替实际操作验证。

Project 面板也移除 RefreshCallback/MoveAssetCallback：render 只生成刷新或移动请求，Editor 在统一请求阶段调用 EditorAssets，再由 update_scan_report/complete_move 更新目录树与操作提示。移动请求保留拖动时的资产版本，交付前拒绝过期手势；同名重命名不发起移动，失败保留输入，成功在下一次绘制关闭弹窗。没有引入通用命令总线或新增管理类。原有 Project UI 回归在绘制结束后消费请求；新增两个用例明确暂停消费，验证刷新和重命名不会在绘制阶段扫描数据库或移动文件。

Project 请求收敛后全目标构建及 CTest 通过：682 个 GoogleTest 用例中 681 个通过、1 个原有采样器用例跳过，shader_build_contract 通过。

资产源文件移动的失败处理也集中到一个 rollback：元数据、源文件、新建空目录依次恢复/清理；创建目录、移动源文件、移动元数据和扫描失败共用此路径，不再分别维护清理分支。ScopeExit 负责非预期退出时的清理，普通失败显式执行回滚并将文件回滚错误加入报告；成功才替换候选数据库并解除清理。删除 move 中扫描捕获。身份冲突回归增加“移除冲突后立即重试”，验证回滚不妨碍后续移动。README 已复核，本步不改变资产移动使用方式。

文件导入也删除整段执行外层的 try/catch，路径解析、目录检查/创建、复制和硬链接发布使用 error_code 返回业务失败。暂存与发布文件共用 cleanup，成功保留发布文件但清理暂存，失败逆序清理本次发布文件、扫描产生的对应元数据及新建目录；ScopeExit 保留提前退出清理，普通失败收集清理诊断。记录路径先于文件发布，发布失败撤销记录，避免把未创建的目标当成本次文件删除。新增暂存路径被文件占用后保留占用文件并允许修复重试的测试；扫描失败回滚测试增加随后完整导入成功。source_operations.cpp 已无自有 try/catch/throw，不承诺第三方或标准库不会抛异常。README 已复核，导入使用方式未变。

文件导入迁移后全目标构建及 CTest 通过：683 个 GoogleTest 用例中 682 个通过、1 个原有采样器用例跳过，shader_build_contract 通过。

AssetTaskQueue 的排队/派发删除两段异常捕获。排队先准备索引位置，再写队列，成功后才更新 pending 版本；新增索引的提前退出由 ScopeExit 撤销，已有版本不提前覆盖。派发先创建结果并调用 try_submit，接受后利用已预留容量登记完整 future/结果，不再插入空 future 占位再修补。普通背压保留原请求，未预期的分配/任务复制失败不再伪装成普通拒绝；Worker 的业务失败与非预期异常边界见下段。背压回归增加连续三次拒绝的检查，验证无在途占位、请求保留且最终只交付最新版本。README 已复核，提交与重试契约未变。

随后核对所有生产 Worker：Mesh/Texture/Material 导入失败已有候选 Result，Shader 失败已有编译诊断。完成消费删除 future 外层捕获，但保留 get，不用 wait 静默忽略异常；异常中断的候选不会发布。资产 ScopeExit 继续移除已消费槽位，Shader 在 get 前取走并清空 pending。标准库等未预期异常不再被转换成可恢复业务失败，顶层仍不提供兜底；不承诺这些异常有序退出。队列同时删除仅用于旧异常日志的 AssetType 字段及 schedule 参数，类型仍由候选变体负责，未新增完成标志、错误容器或服务。

最后两处零容量 invalid_argument 改为 LOG_FATAL：TaskScheduler 的 queue_capacity 以及 AssetManager::AsyncLimits 的两项预算均是代码提供的正数前置条件，当前不来自用户配置。非法容量不自动修正、不创建半初始化对象，也不为此新增工厂；空任务/队列满/关闭后的 try_submit 仍返回空。头文件和 README 明确该约束，原异常测试改为 threadsafe 子进程失败测试。生产源码复查仅剩配置和快捷键中直接 YAML::Load 的两处捕获，按用户决定暂缓。该语法检查不等于整体架构、第三方失败退出或桌面工作流已全部验收。

Editor 的 ImGui begin/end 改由局部 ScopeExit 配对，作用域结束后再提交视口反馈，正常顺序不变，提前返回也能收尾。Application 生命周期参数化测试增加 frame_ready 失败、同时 shutdown 失败两项，使用真实 ImGui 帧的提前返回验证收尾、原始错误码保留、关闭一次及最终 Context 释放。此验证不代替真实设备丢失或锁屏中的桌面交互验收。README 已复核，作用域清理不改变使用契约。

```text
窗口事件 / 尺寸检查 / 时间更新
  -> Application::on_update（编辑器准备或应用运行逻辑）
  -> Renderer::prepare_frame（只做图形帧准备）
  -> Application::on_frame_ready（就绪后编辑，无持久注册回调）
  -> 读取当前 Scene，执行 SceneExtractor
  -> Renderer::render_frame
```

继续保留“overlay 准备后再获取当前 Scene”的顺序，因此编辑器当帧修改或替换场景不会让提取过程继续使用旧场景。

引擎循环重入返回失败 Result，不附带 Vulkan 错误码，也不改变外层循环状态。运行标记由局部 RAII 守卫在正常和异常退出时复位。无更新函数也可运行渲染循环；不存在运行中修改注册表的问题。

### 4.2 暂停呈现不等于终止应用

准备帧返回成功但值为 false 时，本轮不提取和渲染场景，并通过新增 `Window::wait_events(double)` 最多等待约 16ms，避免无呈现期间忙循环。原有阻塞等待接口保留。

这没有实现固定步长、暂停/单步、脚本生命周期或 System 调度。未来根据真实时间语义和依赖增加调度，而不是先预留通用阶段。

## 5. Asset：把任务执行与业务发布分开

### 5.1 私有 AssetTaskQueue

将 AssetManager 内原有异步状态、请求排队、在途 future、调度、完成预算和关闭等待迁移到新增 `AssetTaskQueue`。候选类型移动到 `import_candidate.h`，包括 MeshArtifactCandidate、TextureImportCandidate 和 AssetImportResult。

```text
AssetManager 决定要导入什么，并构造任务
  -> AssetTaskQueue 合并请求、排队、限制在途数量
  -> TaskScheduler 执行 Worker，生成 CPU 候选
  -> owner 线程处理完成，复核 revision
  -> AssetManager 发布 Artifact / 创建及发布 Runtime 资源
  -> 必要时刷新材质依赖
```

队列负责执行机制，Manager 继续负责具体加载、依赖和发布。没有新增另一份资产缓存；AssetRegistry 仍是唯一 Handle Runtime 缓存，也没有把 GPU 发布移到 Worker。

队列仍然理解数据库 revision、资产类型和 Force 请求，属于资产专用模块，不是通用执行器。它不使用 `COMET_API` 导出；Manager 公共配置类型仍定义在 Manager 上，头文件只前置声明实现类型。

### 5.2 保留并明确的任务规则

- 同 Handle 不并行执行多个导入，允许合并保留最新后继请求。
- revision 与强制重建语义参与去重，队列和在途数量有上限，满载可以拒绝接收。
- 调度前和完成时拒绝过期结果，完成预算同时计入失败及过期结果。
- 正时间预算允许处理首个结果，但不抢占单次发布，因此不是严格实时上限。
- 先通过 future 同步 Worker 结果，再读取候选；Worker 异常与 owner 发布异常分开处理。
- 发布期间仍占在途槽，异常退出也移除已消费 future；完成处理有重入保护。
- 析构丢弃未执行请求并等待在途 Worker；队列成员先于其所依赖的 Manager 内部资源销毁。

其中多数规则是从原 Manager 实现迁移和保留，并非本轮从零新增的能力。

完成处理的重入标记复位、已消费任务删除，以及 Engine::run 的运行标记复位，统一复用已有 ScopeExit；删除三种仅用于析构清理的局部结构。正常返回、提前失败和异常展开仍执行原清理，不改变 Worker 异常协议或发布期间占槽的约定。README 已复核，这次内部实现收敛不改变使用方式，无需追加说明。

关闭准备不再只调用 wait_idle：TaskScheduler::shutdown 先在锁内停止接收任务，再唤醒 Worker、排空已接收任务并 join。Engine 在应用清理前调用该入口，防止空闲等待结束后又接收后台工作。shutdown 可由 owner 顺序重复调用，不允许 Worker 自关或并发 join；wait_idle 继续保留为运行期间的空闲等待。
线程启动中途失败和析构也复用 shutdown；构造时以 ScopeExit 保护已启动线程，成功后 release，移除原有 catch 后重抛。新增关闭排空/拒绝提交/重复调用回归，并扩展 Engine 关闭准备测试验证拒绝后台新任务。

任务提交只保留 try_submit：生产资产/Shader 调度原本已经使用它，移除仅测试调用的抛异常 submit。空任务与队列满/关闭均返回空 optional，测试逐处断言接收成功后再操作 future；测试辅助 BlockedWorker 接收失败会报告测试失败并返回，不等待未提交的任务。Worker 的非预期异常仍由 future 保存，消费方保留 get 检查、不再外层捕获；零容量构造作为内部前置条件使用 LOG_FATAL。

ShaderReload 消费就绪任务时先移出完整 Pending 并清空成员，再消费 future，避免意外退出留下“future 已消费但 Pending 仍存在”的状态。新增回归覆盖关闭调度器后交付已接收编译结果、只交付一次，以及关闭后新请求不伪造成功。业务编译诊断保留，未预期异常不再捕获；README 已复核，无用户操作变化。

## 6. Asset：合并加载流程并修复过期发布

职责复核保留 AssetManager 作为加载与发布的组织者，不再为 Mesh/Texture/Material 各建 Manager：Database 维护身份/依赖，ImportService 处理 CPU 导入产物，AssetTaskQueue 只管理任务，ResourceFactory 创建 GPU 候选，Registry 保存已发布对象。首次加载的缓存/类型/版本规则值得共用；不同资产的 Artifact 落盘、依赖更新与 Runtime 替换并非同一事务，不硬合并。移除仅转发 register_asset/replace_asset 并重复记录失败的 publish_runtime_asset helper，调用点直接表达登记还是替换，已有失败 Result 和后台处理策略不变。README 已复核，其描述仍与实现一致。

同步导入与后台 Mesh 完成共用 `refresh_loaded_mesh`：只刷新已驻留 Mesh，创建候选后复核 revision，再替换 Registry。
Artifact 发布仍由各自入口明确执行，不把磁盘发布和 GPU 替换包装成一个事务；后台返回的 Handle 仍表示 Artifact 已发布。
同步 Mesh 导入保存记录快照，避免 GPU 创建期间数据库变化后继续使用借用记录。
显式 Texture 重导入也保存快照和 revision，在 GPU 创建后、写入 `.meta` 前拒绝过期候选；新增回归验证旧纹理和导入设置保持不变，后续后台新版本仍可正常发布。
资产可预期失败的完整返回链现已接通；多项发布不提供全局回滚，具体边界如下。

后续接通后台完成的显式错误链：AssetTaskQueue 的发布回调返回 Result<void, Error>，失败立即停止本批并通过 ScopeExit 回收当前任务。AssetManager::process_completions 返回 Result<vector<AssetHandle>, Error>，直接 Mesh/Texture GPU 创建的 DeviceLost 原样转成通用 Error，经 EditorAssets::update、Editor/app 的 on_update 返回 Engine；普通失败继续记录并保留旧 Runtime。致命失败前已落盘产物不回滚，错误结果不包含已发布 Handle 清单。
共享 Mesh 刷新函数与同步 import_mesh 也改为显式错误，删除对应 DeviceLost throw；首次加载、Texture 重导入和材质依赖现也使用同一错误协议，见 6.4。新增测试覆盖停止本批后续发布、仅回收当前槽位，以及 EditorAssets 返回原生设备错误且保留旧 Mesh。测试成功路径用局部 completed_handles 检查 Result，再断言原有发布列表，不在生产接口增加测试便利包装。

Material Reload/Update 同样保存 AssetRecord 快照与 revision，依赖纹理加载完成后先复核，再保存或发布；不再跨依赖加载借用数据库记录。
材质删除回归参数化覆盖 Load/Reload/Update 三个入口，确认创建依赖期间移除材质后不会重新写出被删除的文件、登记 Runtime 或恢复数据库记录。

### 6.1 首次加载公共流程

`asset_manager.cpp` 原有匿名命名空间内增加私有模板 `load_runtime_asset`，由 Mesh、Texture、Material 首次加载共同使用：

```text
检查 Handle
  -> 查 Runtime 缓存与类型冲突
  -> 查数据库记录并检查资产类型
  -> 保存 revision，复制 AssetRecord 快照
  -> 类型专属函数创建候选
  -> 复核该 Handle 的 revision 是否仍有效
  -> 注册到 AssetRegistry
```

各类型导入和 GPU 创建逻辑仍分别保留；缓存命中不重复构造资源。公共函数不导出为新的引擎服务。

这里不仅减少代码重复，还补齐 Texture、Material 创建完成后的版本复核。创建资源或加载依赖期间，数据库可能被回调刷新甚至删除记录；快照避免继续持有被修改的记录引用，最终 revision 校验防止旧候选被发布。

该流程不是给 AssetDatabase 增加任意多线程写入支持，而是在现有调用约束下处理创建过程中的记录变化。

### 6.2 材质只合并共同尾部

新增私有 `publish_material()`，共享“更新数据库依赖，再发布 Runtime 材质”的流程。

- reload：读取数据、构造候选，再进入共同发布尾部。
- update：校验与序列化、构造候选、原子写入文件，再进入共同发布尾部。

没有为了抽象而合并文件读取与编辑保存策略。文件、依赖数据库和 Runtime Registry 仍是多个步骤，不是全局事务；后一步失败不保证撤销前一步已成功的结果。

Mesh Artifact 与 Runtime 发布也保持这种边界：Artifact 发布成功后，GPU 创建失败不会把已落盘 Artifact 描述成未发布。

### 6.3 扫描触发的材质刷新统一进入完成队列

`apply_scan_report` 不再直接调用同步 `reload_material`。它提交包含记录快照和 revision 的材质读取任务，Worker 只读取 `MaterialData`；owner 在 `process_completions` 中加载纹理依赖、创建候选、再次校验版本，最后复用 `publish_material`。

- 复用已有队列的合并、背压、完成预算和槽位回收，不增加调度器或管理层。
- 扫描与 Worker 不创建 GPU 资源；材质修改、移动均在完成处理后才替换 Runtime。
- 数据库扫描仍读取材质依赖以建立索引，本次不宣称所有 CPU 读取都已后台化；坏材质的扫描诊断与身份快照提交分别验证。
- 依赖创建期间版本改变则丢弃旧候选，最新请求继续处理，不覆盖现有材质。
- 源文件解析失败保留旧材质并释放任务槽；修复后的新扫描可以正常发布。
- 成功发布列表包含 Material Handle，供已有场景引用重查流程消费。显式编辑保存与首次加载仍保留各自入口；材质依赖错误的 Result 迁移见 6.4。

回归测试验证各阶段的对象身份与纹理创建次数、移动后的延迟发布、过期候选拒绝及损坏后修复。

### 6.4 资产加载错误贯穿实际消费者

Mesh/Texture/Material 的 load、reload、update/reimport 统一返回 Result<shared_ptr<T>, Error>，成功值为资源，失败携带原因；ensure_loaded 返回 Result<void, Error>。查询不存在仍由数据库/Registry 的空指针表示，没有给普通查询强加 Result。

- 首次加载先检查数据库类型，再复用缓存；创建后仍校验 revision，失败不发布候选。
- 材质依赖失败补充文件/属性上下文，保留底层 code；不再对 DeviceLost 抛 runtime_error。
- EditorAssets 的引用加载及编辑返回 Result；prepare_scene 返回缺失数或致命错误，普通缺失保留引用，设备丢失中止候选安装。
- Inspector 先确认编辑失败并恢复草稿，再交付设备错误；引用赋值、模型拖入、场景重查均由原有请求阶段处理，不增加全局错误标记。
- app 必需资源加载保留底层原始 Error，不替换成泛化错误消息。
- graphics/result 导出 is_device_lost(Error)，比较同一动态库内的 Vulkan error_category，避免各消费者重新构造分类逻辑。
- 重导入成功发布纹理后，依赖材质若发生设备错误则返回失败；之前已完成的文件/Registry 发布不回滚，仍不是全局事务。

测试直接验证 Result 失败和原生码；原有成功资源断言通过测试局部 loaded_asset 解包并报告失败，没有给生产 API 增加兼容包装。非预期工厂异常注入测试仍保留，不宣称标准库和第三方调用不会抛异常。

## 7. Surface：所有权与候选创建

Context 中的裸 Surface 成员改为共享持有 `vk::UniqueSurfaceKHR`。业务仍通过 `get_surface()` 借用原生句柄；取得 owner 的入口收为 private，仅供友元 Swapchain 使用。

每个 Swapchain Generation 保存其创建时的 Surface owner，并保证原生 swapchain 先于 Surface owner 析构。Context 切换到新 Surface 后，仍被持有的旧 Generation 可以继续保活旧 Surface，避免 Surface 提前销毁。

初始化和恢复共用私有 `create_surface_candidate()`：验证窗口、创建 RAII 候选、保留原生错误。恢复还检查现有 present queue family 是否支持候选 Surface，成功后才安装。

```text
Context 创建候选 Surface
  -> 恢复路径检查现有呈现队列兼容性
  -> 安装新的 Surface owner
  -> 新 Generation 持有该 owner
旧 Generation -> 仍持有自己的旧 Surface owner
```

SurfaceLost 恢复前释放活动 Generation，并要求上层已经等待 GPU/呈现完成、释放 dependent。共享 Surface 不代表共享 Instance：Instance、Device 仍必须活得比相关资源更久。

本轮没有实现队列不兼容时重新选择设备或呈现队列；初始创建失败仍保留已有启动期 fatal 策略。

## 8. Presentation：统一恢复状态机

### 8.1 只在帧准备推进恢复

旧接口可在 acquire、present 或外部调用时直接发起重建。现在公开调用只登记请求，实际恢复统一由下一次 `begin_frame()` 推进。

恢复阶段包括 `Ready`、`Swapchain`、`Dependents`、`Surface`：

```text
Ready
  -> 手动请求 / OutOfDate / acquire 或 present 失败
  -> Swapchain（SurfaceLost 则进入 Surface）
  -> 等待帧槽和 present queue，释放 overlay 再释放 scene
  -> 必要时重建 Surface
  -> 重建 Swapchain
  -> Dependents：重建 scene，再重建 overlay
  -> 全部成功后 Ready
```

零尺寸导致 Deferred 时保持未就绪，不能开始新帧。dependent 失败则保留已成功创建的新 Generation，下次重试 dependent，而非重复创建交换链。

兼容性比较针对上次完整安装成功的配置；只有所有 dependent 成功才更新该配置。

### 8.2 重试规则

内存不足、OutOfDate、SurfaceLost、Timeout、NotReady 等已分类暂时失败进入无呈现状态，复用 RetryBackoff，按 1、2、4 秒最多重试三次。

手动请求可以重置预算，多次请求合并到下一次准备；自动 OutOfDate 状态推进不主动重置预算。不可重试错误或预算耗尽向上传递错误。

传入 oldSwapchain 创建后，旧代退休不可回滚的规则继续保留。创建失败后再次尝试不能从退休代 acquire，也不能把它当作仍有效的旧代恢复使用。

### 8.3 提交和呈现完成顺序

提交失败直接返回错误。提交成功后，即使 present 返回失败，也先结束 FrameScheduler 的当帧状态，再处理恢复或终止，避免已提交帧仍被当成录制中的帧。

可恢复的 present 失败表示本次错误已登记恢复，并不保证画面成功显示；调用方不能把 `render_frame()` 的成功等同于用户已经看到新画面。

## 9. 错误返回和接口迁移

| 接口 | 当前契约 |
| --- | --- |
| `Presentation::begin_frame()` | `Result<bool, GraphicsError>`：true 就绪，false 延期，failure 终止当前循环 |
| `Presentation::end_frame()` | `Result<void, GraphicsError>`：保留错误类别与原生结果 |
| `Renderer::prepare_frame()` | 透传三态；延期或失败时清理本帧临时拾取/线条状态 |
| `Renderer::render_frame()` | 返回呈现链路的 Result |
| `Renderer::request_swapchain_recreation()` | 替代立即执行的 `recreate_swapchain()`，只登记请求 |
| `Engine::run(update, frame_ready)` | 显式帧编排，返回 `Result<void, Error>` |
| `Engine::tick(update)` | 私有单帧推进，不作为外部调度 API |
| `Application::run(config)` | 返回 `Result<void, Error>`，完成关闭后交由入口处理失败 |
| `Application::end()` | 私有关闭操作，返回 Result，关闭失败时保留资源 owner |
| `EditorShortcuts::load/parse()` | 返回 `Result<EditorShortcuts>`，只在成功后安装候选配置 |
| `Scene::update_world_transforms()` | 返回实际重算数量 |

### 9.1 Runtime 生命周期与错误保留

错误链路为：底层错误 → Presentation → Renderer → Engine → Application → 入口退出码。Application::run 和私有 end 均返回 Result，不再将呈现错误转为异常，也不捕获后重抛；完成清理后由入口报告失败并返回 1。

engine.cpp、runtime.cpp、entry.cpp 已无显式 try/catch/throw。Application 的 on_init/on_update/on_shutdown 直接返回 Result<void, Error>，Engine 的更新回调同样返回 Result；生命周期层不再把错误转异常，也没有新建入口 catch。关闭失败不覆盖原错误，也不重复关闭。

图形内部保留 GraphicsError，离开图形处理边界时通过导出的 as_error 转换为通用 Error；保留消息、标准 error_code 的类别和原生数值，不再依赖异常 RTTI。生命周期测试通过 Result 注入初始化、更新、DeviceLost 和关闭失败。Worker 和渲染依赖中的未预期异常仍可能直接终止进程，不能宣称第三方无异常或全部故障都能有序退出。

关闭钩子失败时保留 Engine/Diagnostics，让派生类剩余 GPU 资源先析构，再销毁基类 owner。只有清理失败而没有先前错误时，才以清理错误作为最终失败；关闭失败的实例拒绝重新运行。

### 9.2 文件导入与致命错误边界

文件导入的业务校验已改为 Result，路径越界、glTF 依赖、目标冲突、暂存与索引失败都不再显式 throw；文件系统和扫描异常的回滚边界保留。内置组件/属性编辑器注册失败属于代码不变量，改用 LOG_FATAL，不用于用户数据错误。完整剩余位置及迁移边界见 [异常处理审查](architecture/error-handling-audit.md)。

导入仍遵循“校验和收集依赖 → 暂存复制并验证 → 发布文件 → 扫描候选数据库 → 提交快照”。直接返回 Result 后仍进入统一回滚和暂存清理，不从外层绕过清理。LOG_FATAL 会 assert/terminate，不展开栈，因此没有将设备失败、文件错误或应用正常失败改成直接终止。

### 9.3 快捷键配置异常收敛

`EditorShortcuts::load/parse` 及内部绑定解析改用 Result，文件读取复用 `read_text_file`。未知按键、修饰键、错误节点类型、重复动作和冲突直接返回错误，不再通过多层 throw/catch 传递。只有 YAML::Load 的第三方异常在解析边界转换；内存分配等非预期失败不在这里吞掉。

### 9.4 配置、JSON 与底层查询

- ConfigLoader::load 两个重载返回 Result<Config>。文件读取、Profile 合并、字段类型、枚举与范围校验直接返回诊断；解码采用 YAML::convert 的布尔结果，仅 YAML::Load 暂留捕获，遵循“不换依赖”的决定。
- Json::Context 的 parse/object/array/required_child/read_scalar/validate_keys 全部返回 Result，移除 Json::Error 类型。simdjson 原生错误码附带来源与字段路径，不再经异常中转。
- Writer 记录首个非法状态、UTF-8 或非有限数错误，finish 返回 Result<string>；失败不向文件层交付半成品。资产、项目与 Scene 的消费者一起迁移，移除序列化外围 catch。
- Scene 属性快照复用描述符 copy_value，再校验有限数；写入通过 variant visit 分派。读档仍独立赋值，不受编辑接口 editable/read_only 限制；失败销毁候选 Scene，当前场景不被替换。
- Editor 的模式克隆失败由 EditorSceneSession 原有失败返回与日志处理，删除外层模式切换 catch。
- Vulkan Surface 能力、格式、模式和队列兼容性查询改用返回原生结果的输出参数重载，枚举处理 eIncomplete；退出等待检查 vkDeviceWaitIdle 返回码。
- MeshArtifact 长度限制、非有限顶点和发布校验直接返回失败；GLFW 拖放回调不再捕获，用 noexcept 阻止异常跨越 C 回调边界，非预期分配失败仍不可恢复。

这些修改只替换失败传递与清理机制，不更改配置或场景文件格式，不新增 System，也未更换第三方依赖。

### 9.5 可读性收敛

停止扩大异常语法清理范围，先整理已迁移的调用链：

- Json::Context 增加 read_field，把必需字段查找、标量解码和字段路径拼接合为一次操作；移除接收 Result<Node> 的 read_scalar 重载，避免嵌套结果调用。缺字段仍定位父对象，类型错误定位具体字段。
- 项目、材质、元数据和场景的标量字段改用 read_field，调用者只检查一次结果。对象与数组仍显式检查，不引入隐式失败状态或传播宏。
- SceneSerializer 删除七个只转发 Context 的辅助函数，内部解析和校验共享入口的 Context。只用一次的 UUID helper 内联为“读取文本、解析 UUID”，减少一层结果包装。
- 属性读取取消模板 lambda 的显式 operator() 调用，改成一致的直接 switch 分支；保留少量清晰重复，不为减少行数再引入抽象。
- Application::run 删除 end() 后重复的 Diagnostics 清理检查；关闭 owner 的职责仍只在 end()，关闭失败时保活行为不变。

保留严格校验、来源诊断、候选发布和 RAII 回滚。未回退为异常，也未新增框架、System 或更改文件格式。后续迁移必须同时评估正常流程可读性，不能只统计剩余关键字数量。

默认绑定直接由按键值构造，不再解析固定字符串。编辑器仅在加载成功后替换绑定，失败记录诊断并保留默认值。文件路径和动作名称继续附在错误中，新增缺失文件、非法 YAML 及非标量绑定测试，既有菜单和视口快捷键测试同步迁移。

### 9.6 调试线段拒绝协议

LineDrawList::append 改为 nodiscard bool，与 add_line/add_box 一致；三种追加入口在修改容器前检查最大顶点数量，不再显式抛 length_error。Renderer::submit_lines 消费失败并记录告警，只拒绝本次批次，不清除已提交线段。空追加立即成功，自追加仍在 resize 后读取源地址，避免重分配后访问旧地址。

现有测试明确检查追加返回值、复制所有权、自追加与空列表自追加。不通过新增生产容量配置来构造测试；最大容器数量分支未做巨量分配实测，依靠溢出前的减法边界检查。内存分配失败仍不是此 bool 协议覆盖的可恢复业务错误。

调试线段协议修改后，全量构建及普通 CTest 通过，仍为 670 项中 669 通过、1 跳过；shader_build_contract 通过，日志未检出 Validation Error 或 VUID。

### 9.7 材质数值修改拒绝协议

Material::set_scalar_property/set_vector_property 改为 nodiscard bool，删除两个非有限值异常。false 表示拒绝并保留原有属性和 revision；相同有限值返回 true，但不递增 revision。不将这类单一校验强行包装成通用 Result，也不为纹理 setter 增加不存在的失败分支。

AssetManager 检查每个 setter 的返回值，必要时返回包含属性名的 Error，未完成的候选不会发布。测试与渲染调用方显式检查接受结果；新增回归覆盖 NaN、正负无穷及 Vec4 所有分量，对已有属性与未创建属性均验证数据和版本不变。

材质数值协议修改后，全量构建及普通 CTest 通过，671 项中 670 通过、1 跳过；shader_build_contract 通过。此次未增加新的异常捕获，剩余渲染准备/构造、资产任务与文件操作边界仍列在异常审查文档中。

### 9.8 场景 pass 失败显式停止提交

材质准备与调试顶点缓冲扩容不再对 DeviceLost 抛异常。MaterialRenderer::render、DebugRenderer::render 和 SceneRenderer::render_scene_pass 返回 Result，错误码传至 Renderer，再沿已有 Engine 生命周期返回链处理。

材质准备的成功空值表示没有可绘制版本，沿用跳过行为；失败表示本帧必须终止，不把设备丢失当成普通缺失。普通材质失败仍保留兼容旧资源，调试扩容普通失败仍跳过本批并保留重试间隔。

Renderer 收到失败后清空单帧调试请求、停止 overlay 与提交并进入关闭状态；部分录制的命令缓冲不提交、不复用，由 owner 销毁。FrameScheduler 仅在成功提交后记录在途序号，关闭不等待未提交帧的 fence。现有真实绘制与材质热更新回归覆盖正常路径；未新增仅供测试的 GPU 工厂接口，设备丢失分支暂无底层故障注入实测，不以正常绘制通过代替该项证据。初始化返回链见 9.11。

场景 pass 返回链修改后，全量构建及普通 CTest 通过，671 项中 670 通过、1 跳过；shader_build_contract 通过，日志未检出 Validation Error 或 VUID。

### 9.9 离屏调整返回链

SceneRenderer::resize_offscreen_target 和 Renderer::set_render_view 返回 Result<void, GraphicsError>，Viewport::update 转换为通用 Error，Editor 在 on_frame_ready 的 ImGui 作用域内返回错误。ScopeExit 仍完成 UI 帧收尾，不继续提交选择反馈或渲染。

普通尺寸错误、延期及资源不足沿用原来的旧目标/重试策略，success 代表可继续使用当前目标，不保证实际尺寸等于请求尺寸；实际分辨率仍从 RenderTarget 读取。设备丢失不进入普通重试，Renderer 先关闭，再返回原生错误，且不安装请求的 RenderView。关闭后的视图更新也明确失败。

新增回归覆盖隐藏和零尺寸不替换目标、恢复可见后安装目标、关闭后拒绝调整且保留目标身份。原有非法尺寸去重、恢复及再次诊断测试均显式检查返回结果；设备丢失仍无底层故障注入实测。

离屏调整返回链修改后，全量构建与普通 CTest 通过，672 项中 671 通过、1 跳过；shader_build_contract 通过，日志未检出 Validation Error 或 VUID。

### 9.10 视口依赖由初始化流程准备

Viewport 构造器不再向 ResourceManager 获取采样器；Editor::setup_panels 先检查 get_nearest_clamp 的结果，失败保留原生码返回 on_init，成功后把共享采样器传给 Viewport。Viewport 仅持有准备好的依赖并绑定图像，移除该路径的 runtime_error，也移除对 ResourceManager 的依赖。

没有新增 Viewport::create、初始化标志或失败后不可用的半成品。空采样器属于调用方违反构造契约，仍有不变量检查；实际 GPU 获取失败在调用构造之前返回 Result，不用 LOG_FATAL 替代。现有视口 UI 回归同样显式准备依赖，验证绘制与关闭顺序；未注入采样器 GPU 创建故障。

视口依赖准备调整后，全量构建与普通 CTest 通过，672 项中 671 通过、1 跳过；shader_build_contract 通过，日志未检出 Validation Error 或 VUID。

### 9.11 完整依赖构造替代初始化异常

RenderContext::create 在局部创建 Context、Device、Swapchain，失败原样返回 GraphicsError；Renderer::create 准备资源管理器、帧调度器及场景目标，全部成功后才构造 Renderer。Engine::create 最后准备任务调度器并接收 Window、AssetRegistry 和 Renderer。私有构造器只移交完整依赖，不增加 initialized 标志，不公开可重复调用的 init 方法。

Application::run 检查 Engine 创建结果，失败时释放 Diagnostics，既不调用 on_init，也不调用尚无对应初始化的 on_shutdown。成功后的生命周期规则不变。测试及 UI 消费者显式检查创建结果，未保留抛异常兼容构造器。

零帧槽配置在 Renderer 创建入口被拒绝，不进入 FrameScheduler 的不变量终止路径。新增回归验证失败跳过钩子、错误原因保留，以及修正配置后同一个 Application 可以成功运行并关闭。该测试未模拟 GPU 创建失败；底层 Window/Context/Device 的既有 fatal 检查和标准库分配异常不因此变成可恢复 Result。

完整依赖构造迁移后，全量构建与普通 CTest 通过，673 项中 672 通过、1 跳过；shader_build_contract 通过，日志未检出 Validation Error 或 VUID。RenderContext 的公开方法不再保留半初始化空 Device 分支。

### 9.12 数据库版本检查前置与无变化更新

删除 AssetDatabase 的两个版本溢出 throw 与 issue_revision 包装。扫描在候选快照中判断新增/修改，再分配局部版本；耗尽时返回诊断、不提交快照、不输出部分新增/修改变化集。扫描前期可能已生成 .meta，该行为不被描述为文件系统事务。

update_import_settings 在写 .meta 前检查版本余量，写入失败不消耗版本；update_dependencies 在修改正反向索引前检查。真正提交后才推进全局计数，不允许回绕。相同设置且源签名未变时不重写 .meta，相同依赖及签名不重建反向索引。

新增测试验证无变化设置保留文件时间和 revision、非法设置失败不消耗版本、随后有效更新仅推进一次，以及依赖去重后仍保持版本和反向索引内容。未为测试暴露版本计数器，也未执行接近 uint64 上限的巨量更新；耗尽分支通过源码顺序检查，不能当成已做故障注入。

数据库版本边界调整后，全量构建与普通 CTest 通过，675 项中 674 通过、1 跳过；shader_build_contract 通过，日志未检出 Validation Error 或 VUID。

## 10. ImGui：允许恢复过程重复释放与重建

恢复可能在 Vulkan 后端已关闭、部分目标已创建时失败。仅凭 `m_is_recreating` 跳过释放，会遗漏这些局部资源；无条件重复 Shutdown 又可能触发第三方断言。

现在已初始化的上下文即使正在恢复，也继续清理 swapchain targets。重建除检查 format/image count 外，还检查 Vulkan backend 是否存在；关闭 Vulkan 和 GLFW backend 前分别检查各自状态。

因此“释放、部分失败、再次释放、重建”可以重复执行。没有修改第三方 ImGui 源码，也没有承诺覆盖其所有内部初始化失败或错误回调场景。

## 11. 测试与验证

### 11.1 新增或调整的覆盖

| 范围 | 关键验证 |
| --- | --- |
| Scene | UUID 删除后不可查、重挂父节点索引、长期持有引用后的修改、移除再添加 Transform、局部重算计数、即时查询不更新无关节点、相机缩放和位置 |
| Runtime | 更新中关闭窗口不渲染、拒绝循环重入、更新 Result 失败后运行标记复位、overlay 修改或替换场景仍在当帧生效 |
| Application 生命周期 | 初始化/更新/关闭失败只清理一次，直接保留原生错误码，关闭失败不覆盖主错误 |
| WSI | 请求延期和合并、dependent 失败后复用新代、退避后自动恢复、DeviceLost 保留原生错误、Surface 重建与旧代保活 |
| ImGui | 模拟后端部分关闭后重复释放和重建，并继续渲染 |
| Asset | Texture 创建时 revision 改变，Material 加载依赖时自身记录被删除，均拒绝发布过期候选 |
| 文件导入 | 非法 glTF 返回诊断且不发布；既有路径越界、符号链接、冲突及索引失败回滚测试继续通过 |
| 快捷键 | 配置 Result、文件路径和字段定位、非法 YAML/节点类型、失败保留原绑定及菜单/视口消费者 |
| 既有渲染测试 | 所有相关调用适配 Result，分别检查成功状态和帧就绪值 |
| ScopeExit | 提前返回清理、release 提交、移动专有捕获、逆序销毁 |
| JSON | 既有严格类型、重复字段、层级深度、非法 UTF-8 与原文件保留；新增嵌套向量错误位置和错误对象类型 |

SurfaceLost 测试是在 dependent 边界注入对应错误，再执行实际 Surface 替换，不代表已经复现全部平台的真实窗口系统丢失事件。

### 11.2 最近一次代码验证结果

```sh
cmake --build --preset dev-debug --parallel 8
ctest --preset dev-debug --parallel 4 --output-on-failure
git diff --check
```

全量目标构建成功。资源流程收敛后，146 个资产相关测试全部通过；GoogleTest 两个分片再次覆盖完整集合：分片 0 为 328 通过、1 跳过，分片 1 为 328/328 通过，合计 657 个测试、656 通过。shader_build_contract 均通过。跳过项为 `SamplerTest.CloseAnisotropyValuesDoNotCollideInPresetCache`。

普通单进程 unit_testing 重跑再次在 DebugRenderer 初始化时卡住，采样位于 MoltenVK 编译等待，系统报告 dispatch 线程达到上限；已主动终止，不能记为通过。最新采样保存在 `/tmp/unit_testing_2026-09-15_015712_JdmQ.sample.txt`，不是仓库产物。分片通过不等于单进程卡住的根因已解决。

后续对照实验定位到窗口动画相关的测试进程问题：采样中大量 dispatch Worker 停留在 AppKit `NSAnimation::_runBlocking`；仅用启动参数关闭窗口动画后，同一完整集合约 5.9 秒完成。`tests/CMakeLists.txt` 现仅在 APPLE 平台为 unit_testing 添加 `-NSAutomaticWindowAnimationsEnabled NO`，不改系统偏好、不改生产窗口、不拆分测试进程，也不跳过真实 Vulkan/WSI 测试。这是测试环境隔离措施，不是 AppKit/MoltenVK 驱动修复。

更新后执行 `ctest --preset dev-debug --parallel 4 --output-on-failure --repeat until-fail:3 --timeout 60`，普通单进程完整集合连续三次通过，每次 656 通过、1 跳过，shader_build_contract 也连续三次通过。无动画参数的直接运行仍可能触发上述平台问题。

随后补齐 Material Reload/Update 版本保护及参数化回归，全量构建成功；普通 `ctest --preset dev-debug --parallel 4 --output-on-failure --timeout 60` 覆盖 659 项，658 通过、1 跳过，shader_build_contract 通过，日志未检出 Vulkan Validation Error/VUID。

最新 TaskScheduler 关闭职责调整后，全量构建及同一普通 CTest 命令通过：660 项中 659 通过、1 跳过，shader_build_contract 通过。

ShaderReload 完成状态清理与关闭交付回归后，同一全量构建/CTest 验证通过，661 项中 660 通过、1 跳过，shader_build_contract 通过。

frame_ready 错误返回链路接通后，全量构建及普通 CTest 通过，662 项中 661 通过、1 跳过，shader_build_contract 通过。

后台资源完成错误链路接通后，全量构建及普通 CTest 通过，664 项中 663 通过、1 跳过，shader_build_contract 通过。

最新交互失败生命周期与 ImGui 收尾验证后，全量构建及普通 CTest 通过，666 项中 665 通过、1 跳过，shader_build_contract 通过。

材质扫描刷新转入完成队列后，全量构建及普通 CTest 通过，668 项中 667 通过、1 跳过，shader_build_contract 通过。新增覆盖版本过期及损坏后修复；移动测试改为验证完成处理前保留旧材质、处理后替换，坏材质扫描分别验证诊断与快照提交。

资产首次加载、依赖与编辑 Result 链路贯通后，全量构建及普通 CTest 通过，670 项中 669 通过、1 跳过，shader_build_contract 通过。新增真实资产准备拒绝 SceneDocument 安装、普通资源不足保留可修复引用，以及材质编辑依赖错误码/旧文件/旧资源保护验证。日志未检出 Validation Error 或 VUID。

```sh
GTEST_TOTAL_SHARDS=2 GTEST_SHARD_INDEX=0 ctest --preset dev-debug --parallel 4 --output-on-failure
GTEST_TOTAL_SHARDS=2 GTEST_SHARD_INDEX=1 ctest --preset dev-debug --parallel 4 --output-on-failure
```

最近测试日志未检出 `Validation Error` 或 `VUID-`，diff 空白检查通过。上述结果已更新为生命周期、配置、JSON、ScopeExit 和 Vulkan 查询迁移后的全量构建及测试结果。测试通过不表示剩余异常清单已经清零。

未提供跨平台、真实设备丢失恢复、ASan 或性能基准结果；减少扫描和矩阵运算是算法层面的变化，不宣称实测加速倍数。

## 12. 路线图同步与未完成边界

### 渐进架构整理进度

本轮按“场景切换 → 帧编排 → 失败与关闭 → 资产内部”完成实现与自动化回归。2026-09-15 用户要求需要手动配合的项目先跳过，因此完整桌面交互保留为未验证，不再作为本轮收尾阻塞项；跳过不等于通过。

最终复验：`cmake --build --preset dev-debug --parallel 8` 全目标通过；`ctest --preset dev-debug --parallel 4 --output-on-failure --timeout 60` 两个目标通过，683 个 GoogleTest 中 682 个通过、1 个采样器能力相关用例跳过，shader_build_contract 通过。`git diff --check` 通过。README 已复核，本次仅调整验收状态，无需修改使用说明。所有改动仍在工作区，未提交或推送。

- 场景切换：已将候选准备从替换回调移出。SceneDocument 和 EditorSceneSession 在安装前显式调用准备策略，策略返回 Result；无资源依赖的消费者可省略准备。Editor 当前接受缺失引用并保留修复能力，不吞掉准备策略的拒绝结果。
- New／Open／Play 的准备拒绝不替换当前场景；文档路径不改变，Play 留在 Edit。Stop 不调用准备，直接恢复原 Edit 实例。资产集成测试不再通过替换回调隐式加载。
- 场景安装：Editor::install_scene 成为统一入口，在旧场景存活时取消视口交互和属性事务，替换活动场景后更新选择、层级面板及命令历史，再返回旧 owner。Open／New／Play／Stop 不再各自追加 bind_active_scene。首次启动尚无面板时只绑定历史，面板随后按活动场景构建。
- Play／Stop 将目标 EditorMode 显式传给安装操作，绑定不依赖会话何时更新状态。回归验证安装模式顺序以及 Stop 恢复原 Edit 实例；尚未做桌面交互回放。
- 帧编排：已移除 Renderer 的 overlay prepare 和 ImGuiContext 的 UI 回调。Engine::tick 明确列出 update、图形准备、frame_ready、场景提取和绘制；Editor 明确列出 UI begin/end 之间的业务。仅 GPU overlay 录制与资源重建继续通过 Renderer 接入。未引入 System、固定步长或通用阶段注册表。
- 回归覆盖帧延期时继续 update 但不执行编辑／绘制；编辑修改或替换 Scene 后，当帧拾取与辅助线使用新数据。Viewport 测试直接编排 UI begin/end，不依赖旧回调接口。
- 应用错误模型：common/Error 仅含消息和 std::error_code，不依赖 Vulkan。Engine、Application、app/editor 钩子采用 Result<void, Error>；渲染内部仍保留 GraphicsError 供设备丢失与内存不足策略判断，跨边界时显式转换。新增普通系统错误码回归。
- 关闭顺序：Application::end 先调用 Engine::prepare_shutdown，由 TaskScheduler 停止接收、排空任务并回收线程，再由 Renderer::prepare_shutdown 停止新帧并等待 GPU，随后执行应用 on_shutdown。两个准备操作可重复调用，关闭后的 Engine 拒绝重新运行。Engine 不再通过 RenderContext 访问 Device。独立图形 owner 的析构兜底等待保留，不能因合并上层流程而破坏单独使用的安全性。
- 已完成资产职责复核：Database/ImportService/AssetTaskQueue/ResourceFactory/Registry 各自保留现有职责，Manager 负责跨对象的加载和发布编排，不再按资产类型增建服务。demo 独立启动失败已取得下述实际进程证据。自有异常语法已清理至 YAML 暂缓边界，标准库与第三方未预期异常不保证有序退出。
- 桌面验证经历锁屏与输入自动化无响应，具体证据见下文。需要手动确认的场景打开、Play/Stop、选择、历史、草稿与失败重试按用户要求跳过，不继续要求用户操作。
- 不新增 SceneManager，不重新引入异常，不承诺第三方异常已全部消除。既有场景格式、延迟请求和失败后保留旧状态契约不变。

### 验收结果与跳过项

收尾复核依据为当前源码与最近完整 CTest 的 LastTest.log，不仅依据接口名称：SceneDocumentTest 验证候选失败保留场景/路径，EditorSceneSessionTest 验证 Play 克隆/准备失败与 Stop 恢复原实例；FrameEditOrderTest 验证当前准备阶段替换场景后拾取和辅助线使用新数据；ApplicationLifecycleTest 验证十种正常/失败退出组合的关闭次数与主错误保留。资产回归覆盖连续背压后合并最新请求、移动后旧候选不能覆盖新版本、重导入失败保留 Artifact、坏材质修复及过期 Inspector 请求拒绝。帧编排、安装和资产发布的这些边界已获得源码与行为证据；完整编辑器交互仍是独立门槛。

2026-09-15 桌面已解锁。使用 `/tmp/comet-ui-check.dykrc8/project` 的 demo 副本及独立应用标识重试：截图显示纹理立方体与持续更新的 FPS；进程采样确认进入 Engine::tick，并非停在 ImGui 初始化。通过原生窗口关闭按钮退出的测试进程返回 0，RenderContext 析构一次；原有 build-editor 实例未关闭。自动化菜单点击、实体选择及快捷键未产生预期业务状态变化，原因尚未确认，不能据此认定输入回归，也不能将完整交互记为通过。首次同名测试进程使用 SIGTERM 结束，不计为正常退出证据。当前不为绕过验收修改架构，也不把非预期第三方异常的有序退出、完整 GPU 故障注入或未来路线图功能追认为本轮已经实现。

| 链路 | 当前缺口 | 完成所需证据 |
| --- | --- | --- |
| 首次加载 → 材质依赖 → EditorAssets | 已统一 Result，依赖补充消息但保留 code | 单元回归覆盖 DeviceLost、普通失败和旧资源保留；手动桌面验收按要求跳过 |
| 候选准备 → New/Open/Play → Editor | 实际资产准备已返回 Result，设备失败不安装候选 | 回归验证真实 Mesh 创建失败穿过 SceneDocument；手动桌面验收按要求跳过 |
| Inspector 请求 → Editor 执行 → frame_ready | 编辑执行已返回 Result，先确认草稿状态，再交付设备错误 | 资产编辑与生命周期分别有回归覆盖；手动桌面验收按要求跳过 |
| 应用入口 → 必需资产加载 | 缺失 Mesh、缺失材质、损坏材质三种启动失败已验证 | 实际 demo 进程退出码均为 1，关闭一次、不进入主循环；后两种覆盖已创建 Mesh 缓冲后的清理，不代表已注入真实 DeviceLost |
| 桌面 Open/Play/Stop/失败恢复 | 已确认画面和正常窗口退出，业务输入自动化尚未成功 | 按用户要求跳过需要手动配合的验收，保持未验证 |

本次前台迁移以完整调用链为单位，不只更改 AssetManager 返回类型，不增加全局“最后一个致命错误”标记，也不把错误转换成字符串后再猜类别。桌面未验证项依用户最新要求不阻塞收尾；自动化结果不作为这些跳过项的通过证明。

Play/Stop 会话的准备失败保留通用 Error；apply_mode_request 返回成功 false 表示无切换，成功 true 表示完成切换，failure 表示准备/状态错误。Editor 在 on_update 消费：普通错误记录并留在原模式，DeviceLost 保留原生码向上返回。回归验证错误消息/码、失败保留原 Edit、显式重试、无请求不重试与 Stop 不重新准备。

随后 SceneDocument::create_new/open/save/replace_scene 全部改为 Result<void, Error>，候选准备错误原样返回，显示消息继续保存在文档。SceneFileDialog 不再依赖 SceneDocument：render 只绘制并产生一次性 Request（操作、路径），Editor::process_editor_requests 执行文档操作后通过 complete 回传结果；普通失败显示在弹窗并允许重试，成功在下一次绘制关闭弹窗。DeviceLost 经 process_editor_requests/on_frame_ready 向上返回，draw_editor_ui 恢复为 void。弹窗提交优先于同帧菜单、层级、拖放和模式请求，旧请求已取出而不再执行。启动打开场景遇到 DeviceLost 不再回退空场景，创建空场景失败也直接返回初始化错误而非 LOG_FATAL。graphics/result 中导出的 is_device_lost(Error) 供资产与编辑器策略判断复用，不通过消息文本猜错误。New/Open 回归验证原始错误码与旧场景/路径保留，未新增错误旁路。

新增 SceneFileDialogTest 用真实 ImGui 帧验证：保存按钮只交付一次请求且绘制阶段不写文件；打开失败保留旧场景、弹窗与路径，重试仅在消费请求时安装场景；取消不产生操作。测试不添加生产注入接口，也不依赖项目窗口配置。

本步 dev-debug 全目标构建通过；680 个 GoogleTest 用例中 679 个通过、1 个原有采样器用例跳过，shader_build_contract 通过。ImGui 单元交互测试不代替完整桌面工作流验收。

demo 必需资产加载已移除 LOG_FATAL：两个 helper 返回 Result<AssetHandle, Error>，先复制 Handle 再加载，导入和首次加载错误均保留原始 Error，on_init 在创建场景前检查两项结果。未为测试另拆 helper 或新增生产配置入口；非预期标准库/工厂异常仍不在此捕获。

2026-09-15 实际启动验证：从 dev-debug 的 compile_commands/link.txt 复用编译链接参数，直接编译原 app/main.cpp，仅将 COMET_SAMPLE_PROJECT_DIRECTORY 指向 `/tmp/comet-startup-check.p1MQpY/project`，对象与可执行文件也位于该临时目录，未修改仓库 demo 或添加测试专用生产接口。分别运行空资产目录、只有有效 cube.gltf、再加入损坏 demo.mat 三种情况。每次退出码为 1；日志均包含一次 app init、app shutdown、Renderer/RenderContext 销毁，不含进入主循环或 Vulkan Validation Error/VUID。缺失材质和损坏材质两次均已导入 Mesh 并创建两个 Buffer；最终错误分别定位必需 Mesh、必需 Material、具体材质 JSON 路径。日志为该临时目录内的 missing-mesh.log、missing-material.log、invalid-material.log。此证据覆盖实际启动与失败清理，不替代编辑器可视交互或设备丢失测试。

README 已同步当前架构和使用契约；资产流水线文档补充队列、版本复核和发布边界，并修正部分旧异常描述；资源所有权文档补充阶段、矩阵同步、Surface 与恢复链路。

路线图新增架构收敛安排，并将后续事项放在对应阶段：

- 阶段 3：资产 GPU 创建/加载/刷新、Material 依赖、EditorAssets 和 app 已接通保留原生码的 Result；继续实际交互与其余异常边界验收。
- 阶段 3：只有出现第二个独立消费者，才评估资产队列抽成通用任务机制；多步发布仍不假装是全局事务。
- 阶段 5：继续迁移底层录制和等待失败；另行处理 ImGui 后端局部失败、设备丢失和呈现队列不兼容。
- 阶段 6：on_update 与帧就绪交互 on_frame_ready 显式排序；真实 System 接入时再明确固定步长、暂停策略及依赖调度。
- 阶段 6：先收口 Inspector、Gizmo、Undo、加载和运行逻辑的 Transform 写入契约，再拆即时查询与同步快照，最终实现 dirty 集合更新。

本轮没有引入完整 System、Fixed Update、物理、脚本系统或新渲染架构；也没有改变场景文件格式。已有 AGENTS.md 未修改，构建产物不属于本轮源码改动。

## 13. 完整文件清单

以下职责索引按相关文件分组；路径相对仓库根目录，`.h / .cpp` 写法表示同名两个文件。节末列出本次复核的完整路径清单。

| 文件 | 改动职责 |
| --- | --- |
| `engine/src/scene/scene.h / .cpp` | 三份索引、创建/层级/删除一致性、Transform 状态与矩阵计算 |
| `engine/src/core/engine.h / .cpp` | run/tick 职责、循环约束、Result 传播、延期等待 |
| `engine/src/core/window.h / .cpp` | 带超时的事件等待接口 |
| `engine/src/runtime/runtime.h / .cpp` | 单一 Application 更新入口及生命周期失败处理 |
| `engine/src/runtime/entry.cpp` | 移除异常兜底，消费配置和运行 Result 决定退出码 |
| `tests/runtime/test_entry.cpp` | 生命周期 Result、主错误/关闭错误和原生错误码验证 |
| `engine/src/config/config_loader.h / .cpp`、`tests/config/test_config.cpp` | 配置加载、合并及校验改 Result，仅保留 YAML 解析捕获 |
| `engine/src/common/json.h / .cpp` | JSON 读取直接返回 Result，Writer 首错状态和失败 finish |
| `engine/src/asset/serialization/json_serialization.h` | 移除 JSON 通用序列化边界捕获 |
| `engine/src/asset/serialization/metadata_serializer.cpp`、`material_serializer.cpp` | 显式消费字段与类型校验结果 |
| `engine/src/core/project.cpp`、`engine/src/scene/scene_serializer.cpp` | 完整 JSON 项目与场景加载/保存调用链迁移 |
| `engine/src/common/scope_exit.h`、`tests/common/test_scope_exit.cpp`（新增） | 作用域回滚及提交机制与验证 |
| `editor/src/scene/scene_commands.cpp` | 恢复实体树及组件快照改 ScopeExit 回滚 |
| `engine/src/graphics/device.cpp` | 关闭等待检查原生结果 |
| `engine/src/asset/artifact/mesh_artifact.cpp` | 二进制校验与发布失败直接返回 |
| `tests/asset/test_material_serializer.cpp` | JSON 嵌套字段错误定位回归 |
| `app/main.cpp` | 示例应用生命周期钩子返回 Result |
| `engine/src/asset/source_operations.cpp` | 文件导入业务校验改 Result，保留失败回滚 |
| `tests/asset/test_external_file_import.cpp` | 非法 glTF 失败诊断与不发布验证 |
| `engine/src/scene/component_registry.cpp` | 内置描述符配置错误改 LOG_FATAL |
| `editor/src/inspector/property_editor_registry.cpp` | 内置属性编辑器配置错误改 LOG_FATAL |
| `engine/src/asset/asset_manager.h / .cpp` | 委托异步执行、共用首次加载、记录快照与版本复核、材质发布尾部 |
| `engine/src/asset/import/asset_task_queue.h / .cpp`（新增） | 私有资产排队、调度、完成预算和关闭等待 |
| `engine/src/asset/import/import_candidate.h`（新增） | 内部导入候选类型 |
| `engine/CMakeLists.txt` | 将新增队列实现加入引擎编译 |
| `engine/src/graphics/context.h / .cpp` | Surface owner、私有候选创建、恢复兼容性检查 |
| `engine/src/graphics/swapchain.h / .cpp` | Generation 保活 Surface、Surface 恢复入口 |
| `engine/src/render/presentation.h / .cpp` | 恢复状态机、统一请求入口、重试与 Result |
| `engine/src/render/renderer.h / .cpp` | 帧接口 Result、重建请求和消费者衔接 |
| `editor/editor.cpp` | 编辑器更新顺序明确、只读属性比较 |
| `editor/src/inspector/inspector.cpp` | 展示属性使用 const 读取 |
| `editor/src/ui/imgui_context.cpp` | 重复释放、后端存在性判断和恢复 |
| `editor/src/ui/shortcuts.h / .cpp` | 配置失败改用 Result、复用文件读取、默认绑定直接构造 |
| `tests/editor/test_shortcuts.cpp` | Result 校验、非标量输入与文件/字段错误定位 |
| `tests/editor/test_editing_ui.cpp` | 菜单快捷键配置消费者迁移 |
| `tests/editor/test_viewport_gizmo_ui.cpp` | 视口快捷键配置消费者迁移 |
| `tests/scene/test_scene.cpp` | 索引、矩阵缓存、相机行为回归 |
| `tests/asset/test_asset_manager.cpp` | 创建中记录变化与过期发布回归 |
| `tests/graphics/test_swapchain.cpp` | 请求、重试、错误和 Surface 生命周期回归 |
| `tests/editor/test_viewport.cpp` | ImGui 恢复与帧接口迁移 |
| `tests/render/test_frame_edit_order.cpp` | 单帧更新顺序、重入/异常与当帧编辑链路 |
| `tests/render/test_debug_renderer.cpp` | Result 帧接口迁移 |
| `engine/src/render/line_draw_list.h / .cpp`、`tests/render/test_line_draw_list.cpp` | 追加返回显式拒绝结果，顶点数量溢出保护与空列表/自追加验证 |
| `tests/render/test_material_rendering.cpp` | Result 帧接口迁移 |
| `tests/render/test_material_runtime.cpp` | Result 帧接口迁移 |
| `engine/src/render/material.h / .cpp`、`tests/render/test_material.cpp`、`tests/shader/test_compiler.cpp` | 材质数值修改显式拒绝非有限值，调用方消费返回结果，旧值与版本保护 |
| `README.md` | 当前架构摘要与错误/恢复契约 |
| `docs/architecture/asset-pipeline.md` | 资产执行、候选校验和发布边界 |
| `docs/architecture/rendering-ownership.md` | 同步、Surface 所有权与呈现恢复 |
| `docs/engine-roadmap.md` | 当前完成状态与后续架构收敛计划 |
| `docs/current-changes.md`（新增） | 本轮工作区改动的集中说明，即本文 |
| `docs/architecture/error-handling-audit.md`（新增） | 全局异常分类、已收敛路径和剩余边界 |

### 本次复核的精确路径

<details>
<summary>119 个修改或新增文件</summary>

```text
README.md
app/main.cpp
docs/architecture/asset-pipeline.md
docs/architecture/error-handling-audit.md
docs/architecture/rendering-ownership.md
docs/current-changes.md
docs/engine-roadmap.md
editor/editor.cpp
editor/src/assets/asset_edit.h
editor/src/assets/editor_assets.cpp
editor/src/assets/editor_assets.h
editor/src/assets/project.cpp
editor/src/assets/project.h
editor/src/inspector/inspector.cpp
editor/src/inspector/inspector.h
editor/src/inspector/property_editor_registry.cpp
editor/src/render/shader_reload.cpp
editor/src/scene/editor_scene_session.cpp
editor/src/scene/editor_scene_session.h
editor/src/scene/scene_commands.cpp
editor/src/scene/scene_document.cpp
editor/src/scene/scene_document.h
editor/src/scene/scene_file_dialog.cpp
editor/src/scene/scene_file_dialog.h
editor/src/ui/imgui_context.cpp
editor/src/ui/imgui_context.h
editor/src/ui/shortcuts.cpp
editor/src/ui/shortcuts.h
editor/src/viewport/viewport.cpp
editor/src/viewport/viewport.h
engine/CMakeLists.txt
engine/src/asset/artifact/mesh_artifact.cpp
engine/src/asset/asset_manager.cpp
engine/src/asset/asset_manager.h
engine/src/asset/database.cpp
engine/src/asset/database.h
engine/src/asset/import/asset_task_queue.cpp
engine/src/asset/import/asset_task_queue.h
engine/src/asset/import/import_candidate.h
engine/src/asset/serialization/json_serialization.h
engine/src/asset/serialization/material_serializer.cpp
engine/src/asset/serialization/metadata_serializer.cpp
engine/src/asset/source_operations.cpp
engine/src/common/error.h
engine/src/common/json.cpp
engine/src/common/json.h
engine/src/common/scope_exit.h
engine/src/config/config_loader.cpp
engine/src/config/config_loader.h
engine/src/core/engine.cpp
engine/src/core/engine.h
engine/src/core/project.cpp
engine/src/core/task_scheduler.cpp
engine/src/core/task_scheduler.h
engine/src/core/window.cpp
engine/src/core/window.h
engine/src/graphics/context.cpp
engine/src/graphics/context.h
engine/src/graphics/device.cpp
engine/src/graphics/result.cpp
engine/src/graphics/result.h
engine/src/graphics/swapchain.cpp
engine/src/graphics/swapchain.h
engine/src/render/debug/debug_renderer.cpp
engine/src/render/debug/debug_renderer.h
engine/src/render/line_draw_list.cpp
engine/src/render/line_draw_list.h
engine/src/render/material.cpp
engine/src/render/material.h
engine/src/render/material_renderer.cpp
engine/src/render/material_renderer.h
engine/src/render/presentation.cpp
engine/src/render/presentation.h
engine/src/render/render_context.cpp
engine/src/render/render_context.h
engine/src/render/renderer.cpp
engine/src/render/renderer.h
engine/src/render/scene/scene_renderer.cpp
engine/src/render/scene/scene_renderer.h
engine/src/runtime/entry.cpp
engine/src/runtime/runtime.cpp
engine/src/runtime/runtime.h
engine/src/scene/component_registry.cpp
engine/src/scene/scene.cpp
engine/src/scene/scene.h
engine/src/scene/scene_serializer.cpp
tests/CMakeLists.txt
tests/asset/test_asset_manager.cpp
tests/asset/test_database.cpp
tests/asset/test_external_file_import.cpp
tests/asset/test_material_serializer.cpp
tests/common/test_result.cpp
tests/common/test_scope_exit.cpp
tests/config/test_config.cpp
tests/core/test_task_scheduler.cpp
tests/editor/test_asset_editing.cpp
tests/editor/test_editing_ui.cpp
tests/editor/test_editor_assets.cpp
tests/editor/test_editor_scene_session.cpp
tests/editor/test_project_panel.cpp
tests/editor/test_scene_document.cpp
tests/editor/test_scene_file_dialog.cpp
tests/editor/test_shader_reload.cpp
tests/editor/test_shortcuts.cpp
tests/editor/test_viewport.cpp
tests/editor/test_viewport_gizmo_ui.cpp
tests/graphics/test_swapchain.cpp
tests/render/test_debug_renderer.cpp
tests/render/test_frame_edit_order.cpp
tests/render/test_line_draw_list.cpp
tests/render/test_material.cpp
tests/render/test_material_rendering.cpp
tests/render/test_material_runtime.cpp
tests/render/test_offscreen_resize.cpp
tests/runtime/test_entry.cpp
tests/scene/test_scene.cpp
tests/shader/test_compiler.cpp
tests/support/blocked_worker.h
tests/support/engine_fixture.h
```

</details>
