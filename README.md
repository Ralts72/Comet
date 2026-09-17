# Comet 引擎

Comet 是使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与 ImGui 编辑器，目前重点是场景编辑、资产导入和单视口交互。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：core、scene、asset、render、graphics、config、diagnostics |
| `engine/shaders/` | 引擎 Shader；只编译 CMake 显式列表，其余源码保留供学习 |
| `tools/shader/` | 共用 CPU Shader 编译库与构建 CLI，不链接 engine 运行时 |
| `editor/` | 编辑器入口，`src/` 按 scene、viewport、assets、inspector、ui 组织，`resources/` 保存私有字体等资源 |
| `app/` | Runtime 示例入口及 `resources/` 私有图标 |
| `demo/` | 随仓库提供的完整示例项目，与引擎／编辑器源码分开 |
| `demo/assets/` | 示例项目场景、源资产及相邻 `.meta`，进入版本控制 |
| `demo/project.json` | 示例项目描述：版本、名称和启动场景 |
| `config/` | `common.yaml` 与各 Profile 配置 |
| `demo/.comet/` | 示例项目本机缓存与编辑器布局，不进入版本控制 |
| `tests/`、`3rdparty/` | GoogleTest 测试与第三方依赖 |

## 构建与运行

需要 CMake 3.31+、C++20 编译器、Vulkan SDK、Git LFS 和 Submodule。
SPIRV-Reflect 以固定版本 submodule 接入，仅作为 engine 的私有静态反射依赖；不构建其工具与测试。
glslang 以正式版本 `16.6.0` 的固定提交作为 submodule，由构建生成 `comet_shader_compiler`；不再要求额外安装 `glslangValidator`。
首次构建会增加源编译器的编译耗时，但 engine／app 不链接该编译库，发布运行不需要源编译器。

```bash
git lfs install
git lfs pull
git submodule update --init --recursive
cmake --preset dev-debug
cmake --build --preset dev-debug --parallel
ctest --preset dev-debug
```

macOS 的 CTest 仅在测试进程内关闭窗口动画，避免大量窗口创建/销毁产生的动画任务阻塞 Metal 编译；不影响 app/editor，图形测试仍使用真实窗口和 GPU。

| Profile | 类型 / 目标 | 脚本 |
| --- | --- | --- |
| `dev-debug` | Debug：app、editor、tests | `./build.sh` |
| `editor-dev` | RelWithDebInfo：editor | `./editor.sh` |
| `app-release` | Release：app | `./release.sh` |

手动配置需指定 `COMET_CONFIG_PROFILE`，并按需组合 `COMET_BUILD_APP/EDITOR/TESTS`。
编辑器源码由 `editor_core`（不依赖 ImGui）和 `editor_ui` 两个内部库管理，入口与测试共同链接。
物理目录按功能聚合，编译目标按依赖划分；例如 `scene/scene_document` 属于 core，`scene/hierarchy` 属于 ui。
仅启用 tests 时仍构建 editor_core，不构建 UI；新增编辑器源码只需维护所属库的清单。
`tests/support/` 提供测试专用的 ImGui Context、临时目录与 Worker 同步辅助，不进入引擎。
`COMET_NATIVE_OPTIMIZATION` 只适合本机构建。配置与诊断采用“编译期能力 + Profile 运行时策略”。

macOS 和 Windows 下 app/editor 分别使用橙色、蓝色彗星静态图标，资源位于各自的 `resources/icons/`，不参与项目资产扫描。
macOS 在构建目录内生成 `app/Comet.app` 和 `editor/CometEditor.app`，内含静态 ICNS 图标；启动脚本自动使用 bundle 内的新入口。
Windows 通过 `.rc` 将 ICO 编译进 exe，GLFW 自动用作初始窗口图标；不在运行时加载 PNG，Linux 暂不配置图标。
开发构建仍依赖仓库资源与开发动态库，不是独立分发包。
GLFW 以动态库构建，确保引擎和 UI 后端共用一份窗口系统状态；Windows 构建会复制目标运行时 DLL 到可执行文件目录。

## 打开项目

```bash
./editor.sh                         # 打开仓库 demo/ 中的示例项目
./editor.sh ./demo                  # 显式打开同一示例项目
./editor.sh /Projects/MyGame        # 读取该目录的 project.json
./editor.sh /Projects/MyGame/project.json
```

编辑器可执行文件也接受同样的可选路径参数；相对路径以调用者的工作目录为基准。`--help` 显示用法。
项目需要 `project.json` 和 `assets/`；资源及相邻 `.meta` 一起迁移，`.comet/` 是可重建的本地数据。
编辑器生成的 `.scene`（v2）、`.mat`（v2）、`.meta`（v3）使用 JSON，扩展名不变；
`.scene` 的 `entities` 只放根实体，子实体通过 `children` 嵌套，不再保存 `parent` 引用；UUID 仍全场景唯一。
项目描述 `project.json` 同样使用 JSON；仅 `config/` 中的引擎、编辑器 Profile 与快捷键配置继续使用 YAML。
JSON 解析直接依赖已有 simdjson。
后台导入采用有界任务队列，同一资产尚未执行的旧请求会被最新 revision 合并替换；
队列满时底层返回拒绝，编辑器自动导入和已加载资源刷新保留轻量待办，在容量恢复后重试；导入内容错误等待新变更或 Reimport。刷新失败继续保留旧资源。
后台结果默认每次最多处理 2 项、采用 2 ms 非抢占软预算；未发布结果继续占据在途额度，同步扫描／加载不受此预算约束。
示例项目根目录是仓库的 `demo/`，不是仓库根；可完整复制该目录作为外部项目。
旧仓库根 `.comet/` 不自动迁移，新位置缺少缓存／布局时会重新生成，旧数据保留。

当前尚未发布，项目及资产描述只接受当前 `FORMAT_VERSION`；缺失、非法或不匹配的版本直接报错。

```json
{
  "version": 1,
  "name": "My Game",
  "startup_scene": "scenes/main.scene"
}
```

`startup_scene` 相对项目 `assets/`；省略或空字符串表示空场景。项目描述不配置默认材质，场景保存自己的材质引用。
项目描述错误或缺少 assets 时启动失败，不回退仓库项目；启动场景缺失／损坏则记录错误并打开空场景，不覆盖原文件。
引擎 Profile、编辑器快捷键仍读取开发构建自带的 `config/`，字体／图标／Shader 不需要复制到每个项目。
当前支持启动时选择一个项目，尚不支持运行中切换项目、最近项目列表、项目创建向导或独立打包。

## 编辑器使用

- File → Open/Save 操作当前项目 assets 内的 `.scene`，拒绝越界路径。启动打开 project.json 指定的场景，
  不恢复上次打开的其他文档；坏资源引用保留并记录 Log，后台导入完成后自动重试加载。
- Edit 使用独立相机；Play 运行场景副本及其 primary Camera，Stop 不回写运行时修改。
  2D/3D 只切换 Edit 投影；Play 分辨率可选 Free、16:9、HD、FHD，Fit 等比适应，1x 原尺寸裁切。
- 视口右键或 Option/Alt+左键环绕，中键或 Option/Alt+Shift+左键平移，滚轮／双指滚动缩放。
  左键按模型包围盒粗拾取，空白点击清空；视口获得键盘焦点后按 F 聚焦选中 Mesh。橙色选中框受场景遮挡。
- Edit 选中实体后，Tool → Mode 选择 Move／Rotate／Scale，拖动轴、圆环或缩放方块。
  Move／Rotate 的 Space 可选 World／Local；Scale 固定 Local，轴手柄调整单分量，中心手柄沿屏幕右上拖动等比放大。
  Snap 相对拖动起点吸附，默认距离 0.25、角度 15°、缩放增量 0.1，设置只保留在会话中。
  World 旋转不接受非均匀缩放父级，此时使用 Local。Escape、失焦或隐藏视口取消拖动。
- Edit 中名称、Transform、Camera 和 Mesh/Material 引用支持撤销；一次手势只记一条历史。
  Inspector 的 Add Component／组件标题右键支持 Camera、Mesh Renderer 增删，Name／Transform 不开放增删。
  Play 仅实时调试已有属性，不记录 Edit 历史；Stop 恢复原 Edit 历史，New/Open 成功才清空历史。
  New/Open 和窗口关闭遇到未保存场景时提供 Save/Discard/Cancel；保存失败或取消另存路径不会继续切换。
  未保存状态使用历史状态 ID 与保存点判断，支持撤销回保存点、分支编辑和历史截断；不包含独立的资产文件编辑。
- Hierarchy 空白处／Scene 右键创建根实体，实体右键创建子实体、删除或 Duplicate 整棵子树；
  拖动实体修改父级，保留本地 Transform，因此世界位置可能改变。结构操作支持撤销，仅在 Edit 开放。
- 编辑器快捷键位于 `config/profiles/editor-dev.yaml` 的 `editor.shortcuts`，修改后重启。
  Undo/Redo 默认 Ctrl+Z／Ctrl+Y，macOS 为 Cmd+Z／Cmd+Shift+Z，文本编辑时不抢占控件的撤销。
  `Primary` 代表 Cmd／Ctrl，`[]` 禁用绑定；冲突会记录日志并回退默认配置。
- Project 自动监视资产变化；右键 Refresh 重扫，Reimport 强制重建 Mesh 缓存。
  拖动资产到目录可移动，右键 Rename 改名；源文件与 .meta 成对操作，不覆盖冲突文件，暂不移动整目录。
  Inspector 的材质／纹理设置按变化提交，日志统一进入 Log；资产文件修改暂不纳入场景撤销。
- Finder／系统文件管理器可将 PNG/JPEG、glTF/GLB 拖入 Project，复制到落点目录。
  glTF 连同相对 buffer／图片复制，新建身份、不移动源文件、不沿用外部 .meta、不覆盖同名目标。
  暂不接收整目录、独立 .bin、网络或含 `..` 的依赖；整批失败回滚，复制大文件仍可能阻塞 UI。
- Mesh 自动后台生成 Artifact；未加载模型只生成缓存，不创建 GPU 对象。删除缓存后用 Refresh 或重启补建。
  Edit 中将 Project Mesh 拖到视口，在相机关注平面创建实体并记录一次撤销；首次导入未完成时需等待后重试。
  新实体材质暂留空，需在 Inspector 指定后才绘制；不导入 glTF 材质。
- Inspector 引用框支持按类型过滤的资产路径下拉框；Edit 还可从 Project 拖入 Mesh／Material／Texture。
  底层仍保存 Handle，加载失败保持旧引用，丢失引用显示 Missing。Play 仅支持下拉调试，不接受资产拖放。
  内置模板支持 `unlit_texture_blend`（两纹理、blend、tint）和 `unlit_color`（color、intensity）。
  Inspector 按共享布局显示纹理、标量和颜色参数，变化后自动保存并更新渲染，无需确认；仅查看默认值不会写文件。
  缺失纹理槽需补齐后才发布，切换其他资产会丢弃未完成草稿；尚不支持切换模板或动态指定项目 Shader。
- View 菜单与面板关闭按钮共享显隐状态；菜单只展示已接通的操作。

## 架构入口

- 启动：app/editor 共用 `RUN_APP` 和 `Comet::launch`，统一参数传递、`--help`、错误退出和 Application 所有权。
  各入口提供返回 `Result` 的创建函数，负责参数校验和依赖准备；`Editor` 只接收已加载的 `Project`，不解析命令行。
  `Comet::run` 读取配置，`Application::run` 驱动初始化、更新和关闭；三个生命周期钩子直接返回 Result，入口消费失败并返回非零退出码，不再设置异常兜底。
  生命周期使用通用 Error（消息与标准 error_code），图形层在边界保留 Vulkan 错误类别和数值。关闭先由 Engine 停止接收后台任务并排空已接收任务、Renderer 停止帧并等待 GPU，再执行应用资源清理。
  Engine／Renderer／RenderContext 使用 create 返回 Result，依赖准备成功后才构造完整对象；Engine 创建失败不调用应用钩子。
  demo 的必需资产缺失、导入或加载失败通过初始化错误退出并清理，保留原始错误码，不直接 LOG_FATAL。
- 场景：Scene 维护 EntityId／UUID 和父子索引，创建、删除与换父级同步更新索引。
  世界变换比较本地 TRS 与父级版本，仅重算发生变化的节点；单个矩阵查询只检查祖先链。
  相机继承世界位置与层级旋转，朝向不受本地及祖先缩放影响。
  Engine::run 管主循环，内部 tick 显式推进应用更新、帧准备、on_frame_ready 编辑和场景提取。Editor 在 on_update 消费上一 UI 帧的请求、后台完成与模式请求；文件读写、扫描、资产编辑和拖放加载均在获取渲染帧前执行。on_frame_ready 只绘制 UI、收集请求、处理即时属性/Gizmo 编辑和视口更新；渲染延期不阻止已收集请求执行。
  on_frame_ready 返回 Result；失败会停止引擎生命周期并保留原始错误，不绘制或重用已获取的帧。
  Play/Stop 会话返回 Result<bool, Error>，区分未发生切换与准备失败；准备错误保留错误码，设备丢失交由主循环退出。
  场景 New/Open/Save 同样返回完整错误；普通文件错误留在对话框内，设备丢失不会触发启动场景回退。
  Inspector 的材质/纹理编辑只产生带版本的 AssetEdit；Editor 在统一请求阶段交给 EditorAssets::apply_edit 校验版本并保存/重导入，再回传结果。失败恢复仍匹配该请求的面板草稿，不在控件绘制回调中创建 GPU 资源。
- 渲染：`Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
  帧准备与 UI 修改完成后才提取 Scene；Scene 只保存组件和资产 Handle，GPU 生命周期由渲染层管理。
  Renderer 装配 FrameScheduler，Presentation 负责 acquire／submit／present 与有序重建；SceneRenderer 只管理场景目标与 pass。
  完整目标切换先准备 pass、附件、材质和辅助线绘制器，全部成功后安装；旧帧保留完整依赖版本。
  SceneResolver 只解析 Mesh/Material 引用；MaterialRenderer 准备并绘制材质队列，无相机帧也清理缓存和重置统计。
  MaterialRuntimeCache 按材质版本和不可变布局准备纹理与参数快照；同一布局驱动 descriptor 和参数打包。
  Material 数值 setter 返回是否接受；非有限值被拒绝，相同值不递增版本。
  已有材质的 CPU／GPU 更新失败保留同一资产的兼容旧版本；清除引用或切换资产不会使用无关历史材质。
  材质准备或调试缓冲扩容遇到设备丢失时返回帧错误，停止后续录制与提交，不复用部分录制的帧。
  属性显示保留声明顺序，GPU binding 仅在准备绑定时排序；热发布日志包含管线准备、候选复制及材质准备耗时。
  相机 FrameSet 按 slot 更新，MaterialSet 按材质版本跨 slot 复用，物体矩阵使用 push constant；在途版本由 FrameSlot 保活。
  Shader 加载时反射实际 SPIR-V，Pipeline 创建／缓存查询前校验绑定及 push constant，材质另检查参数块类型与偏移。
  ShaderInterface 只公开 Comet 值类型；Vulkan 布局转换与覆盖校验留在 ShaderLayout 实现中。
  CPU 创建／校验使用公共 `Result<T>`；图形错误与原生结果位于 `graphics/result.h`，句柄创建工具只供后端实现使用。
  GPU 候选由 RAII 回收，管理器只发布成功对象；普通重载失败保留旧版本。资产加载及部分绘制路径的设备丢失异常仍待迁移，尚不能保证这些路径有序退出。
  资产首次加载、重载及材质依赖失败统一返回 Result；设备丢失经编辑器/应用传给 Engine，停止后续操作，已落盘或已发布产物不回滚。
  MaterialRenderer 只描述资源与槽位，DescriptorSet 负责原生批量写入，CommandBuffer 负责集合绑定；不在材质层拼装 Vulkan 结构。
  WSI 与提交返回显式结果；退休交换链不重新发布，失败提交不产生 completion，也不登记在途帧。
  WSI 暂时失败进入无呈现状态，按 1／2／4 秒最多重试三次；SurfaceLost 重建 surface 并校验呈现队列兼容性。
  手动重建只登记请求，在下一次帧准备统一执行；帧准备返回 Result，成功值 false 表示延期。
  预期呈现失败经 Renderer／Engine 原样返回，在 Application 生命周期边界统一处理。
  重试耗尽、设备丢失和不支持的配置沿生命周期边界退出；具体契约见资源所有权文档。
  Pipeline 按 Shader 字节码／入口、specialization、布局、渲染状态及 RenderPass 域复用，名称只作标签；缓存弱引用不代替在途帧保活。
  specialization 支持 bool 与 32 位数值，按阶段和位模式校验／缓存并传给 GPU；只用于固定接口的创建期变体。
  改变数组长度的变体使用编译期 defines，不用 specialization；材质逐帧参数仍走原有 uniform。
  `graphics/pipeline/` 中，`pipeline_config` 管配置，`pipeline_key` 管缓存身份，`pipeline` 管 GPU 对象创建与复用。
  反射可重绑定已登记材质属性的物理布局，但不会为未知属性生成 Inspector 控件；名称、默认值和颜色语义仍来自手工 metadata。
- 窗口：Window 管 GLFW 初始化与最后一个窗口释放后的终止；上层通过窗口接口请求关闭、查询最小化状态。
  GLFW 是 engine 的私有依赖，原生句柄仅供 Vulkan／ImGui 后端及底层测试对接，不用于普通业务操作。
- 调试绘制：`LineDrawList` 提交单帧世界空间线段/包围盒，`DebugRenderer` 在场景 pass 内绘制，
  使用当前相机和正常深度测试；不依赖 ImGui，编辑器选中框是其中一个调用方。
  添加或合并顶点数量超限时返回失败；Renderer 拒绝该批次并保留本帧已提交的线段。
- 资产：`AssetDatabase` 管身份与依赖，`ImportService` 管导入，`AssetManager` 协调加载与发布，
  `AssetRegistry` 是唯一 Handle 缓存；`ResourceManager` 只创建设备资源。
  Manager 内部的 `AssetTaskQueue` 管后台排队、同资产请求合并、背压、完成预算与关闭等待，不作为公共引擎服务导出。
  `TaskScheduler::try_submit` 是唯一提交入口；空任务、队列满或关闭时返回空结果，调用方须检查后再使用 future。
  调度器队列容量和资产异步预算必须为正；零值视为编程错误，不表示关闭异步功能，也不会自动改成默认值。
  Worker 的业务失败通过导入候选或编译诊断返回；消费前 get 同步并检查任务是否正常结束，不将未预期异常吞掉后继续发布。
  Mesh／Texture／Material 首次加载共用缓存与发布校验，创建期间资产 revision 变化则丢弃候选。
  显式纹理重导入同样在创建后复核 revision，过期候选不写入导入设置、不替换旧纹理。
  Material Reload/Update 在依赖加载后复核 revision，过期操作不保存文件或发布材质。
  数据库无变化的设置/依赖更新不消耗 revision；版本耗尽时拒绝变更，不允许回绕。
  扫描触发的材质刷新通过后台读取、主线程完成处理发布；扫描和 Worker 不创建 GPU 资源，发布前保留旧材质。
  活动场景引用在安装或编辑历史变化时重新索引；后台完成保留变更 Handle，只恢复受影响和未解析的引用，每次默认最多处理 2 项、软预算 2 ms。初次场景准备仍完整执行，单次 GPU 创建不可抢占；同步扫描和文件复制仍可能阻塞主线程，不宣称已实现全异步 I/O。
  导入、资产序列化和数据库更新统一用公共 `Result<T>` 返回预期失败，调用方决定如何报告；GPU 错误仍保留 Vulkan 结果码。
  公共文件读取／原子写入同样返回 `Result`，写入先完成同目录临时文件，再替换目标；失败由 RAII 尝试清理临时文件。
- 持久化：`Project::load`、项目资产路径解析和 `SceneSerializer` 返回 `Result`；Open 失败保留当前场景，
  Save 失败不更新文档路径，Play 克隆失败留在 Edit。JSON 解析、字段校验与序列化直接返回 Result；配置暂保留 yaml-cpp 解析异常转换，不更换依赖。
  Open／New／Play 在安装候选前显式准备资产，缺失引用仍保留供修复；准备被拒绝时不替换当前场景。Stop 直接恢复保留的 Edit 场景，不重新执行资产准备。
  Editor 统一安装活动场景并更新选择、层级面板与命令历史；安装时先取消旧交互，再替换场景，Play／Stop 显式指定目标模式。
- 编辑器：`Editor` 装配依赖与帧阶段，`EditorAssets` 管资产编辑校验与执行、引用选择／模型放置的资源准备、源监视和写入确认，
  `SceneFileDialog` 只管理路径弹窗并产生请求，Editor 在统一请求阶段调用 `SceneDocument` 后回传结果；属性控件显式返回手势状态，文档与 Play 会话仍保持独立。
  Project 产生刷新、移动和重导入请求，由 Editor 调用 EditorAssets 执行；面板消费扫描结果更新目录树，不在绘制时扫描或移动文件。Inspector 按 Handle/revision 管理自己的资产缓存，不依赖入口手动失效。
  `viewport/viewport.h` 是视口功能入口，拥有 ViewPanel 与 TransformGizmo，负责纹理同步、相机更新和拾取／选中反馈；
  采样器由 Editor 初始化时准备并传入，获取失败返回初始化错误，Viewport 不自行创建该 GPU 依赖。
  活动 Scene 按调用传入，不持有 Engine。相机状态及算法集中在 `viewport/camera_controller`，资产引用控件与载荷集中在 `assets/asset_reference`。
- Mesh Runtime 只读已发布的 Mesh Artifact；缓存丢失需先导入，不自动回退解析 glTF。
  Texture 暂时直接解码源文件，后续再引入 Artifact。
- 离屏视口 resize 失败保留旧画面与实际分辨率；内存不足按 1、2、4 秒最多重试三次，耗尽或其他错误等待新尺寸。
  设备丢失通过视口更新返回帧错误并停止渲染，不进入普通 resize 重试。
- 世界 +Y 向上，Vulkan Viewport 用负高度转换画面坐标；`flip_y` 仅控制纹理导入。
  Shader 编译产物只进入构建目录，学习源码不作为生产 Shader 的隐式依赖。
  Shader 编译库与构建 CLI 独立于 engine；材质描述集中在 `render/material.h`，准备缓存与 GPU 绘制各自独立。
  开发编辑器会后台编译 `engine/shaders/glsl/material_mesh.vert`、`material_textured.frag`、`material_solid.frag`；
  修改源码或 include 后自动尝试整批更新，失败保留旧画面，诊断只进入日志区。材质目前只支持现有内置契约，
  已校验基础顶点输入和 Vertex→Fragment 的 location／类型匹配；当前要求精确匹配的 32 位标量／向量，
  Frame／Object 资源布局以构建内嵌程序为基线，字段顺序、矩阵存储方式变化拒绝发布。
  已登记材质属性支持按 Shader 名称重新映射 offset／块大小／binding，驻留材质全部准备成功才切换，并同步 Inspector 布局；
  未知／缺失／改类型的属性和不兼容采样图片仍拒绝，不从 GLSL 自动猜测新属性的默认值或编辑语义。
  兼容更新复用原材质绑定，重复发布相同 Pipeline 不制造新材质版本；发布入口只允许在活动帧之外调用。
  复杂 I/O、顶点格式转换、项目 Shader 与新属性 metadata 仍在路线图中；app／engine 不链接 glslang。
  监视每 500 ms 复核已知输入内容，变化后防抖 200 ms；Worker 只编译，消费端反射校验、GPU 管线创建和发布仍在主线程。
  材质 Shader 发布遇到 Vulkan 主机／设备内存不足时，依次等待 1、2、4 秒，最多自动重试三次并复用 CPU 编译结果；
  耗尽后保留旧版本并记录日志，等待新的源码修改或请求，不持续尝试分配；
  新请求或输入变化使旧重试失效，接口错误不自动重试，设备丢失仍退出清理。
  辅助线 Shader 仅使用构建时内嵌版本，不参与热重载；修改其源码需重新构建并启动。

详细说明：[资源所有权](docs/architecture/rendering-ownership.md) ·
[资产管线](docs/architecture/asset-pipeline.md) · [场景格式](docs/architecture/scene-format.md) ·
[路线图](docs/engine-roadmap.md)。

C++ 遵循根目录 `.clang-format`（100 列），只格式化相关代码，不处理 Shader 和第三方源码。
头文件应能独立编译，实现文件直接包含自己使用的类型，不依赖入口头的传递包含。
引擎 PCH 仅预编译常用标准库头，不包含 Vulkan、ImGui 或项目业务头；PCH 不是隐式依赖来源。
排查 include 可用 `cmake --preset dev-debug -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON` 关闭 PCH，
验证后用同一命令将该选项设回 `OFF`。
频繁切换分支或清理重建时，可安装 ccache，使用 CMake 原生编译器缓存接口：

```bash
cmake --preset dev-debug -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
cmake --build --preset dev-debug --parallel
ccache --show-stats
```

缓存模式关闭 PCH，保持 ccache 默认严格校验；首次构建需填充缓存，后续收益以命中统计为准。
本地默认构建仍使用 PCH；恢复默认需将两个 `COMPILER_LAUNCHER` 设为空、`CMAKE_DISABLE_PRECOMPILE_HEADERS` 设为 `OFF`。
ccache 数据位于其本机缓存目录，可用 `ccache --max-size=1G` 限制占用，不与 `build/` 混用。
CI 使用 Ninja、最多 4 个编译任务和 500 MB 的 ccache；不缓存整个构建目录，每次配置后复用编译结果。
编译器、参数和依赖变化由 ccache 判断是否失效；测试范围及项目调试信息策略不变。
贡献约定见 [AGENTS.md](AGENTS.md)。
