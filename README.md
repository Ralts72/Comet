# Comet 引擎

Comet 是供作者个人学习使用的实验性 3D 引擎与 ImGui 编辑器，使用 C++20、CMake 和 Vulkan。目前重点是场景编辑、资产导入和单视口交互。Play 与示例 app 还支持固定步刚体模拟和基础音频播放；两者共用 RmlUi 项目游戏界面。动画、粒子、GI、路径追踪、渲染线程及内容导出继续按路线图推进；工程实现围绕实际使用和实测性能收敛，不预建通用 SDK 或插件生态。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：基础与数据模块，以及 runtime、audio、render、graphics 等运行后端 |
| `engine/shaders/` | 生产 Shader，按 material、lighting、shadow、environment、debug、post、common 分目录；仅编译 CMake 显式列表 |
| `engine/resources/fonts/` | App／Editor 共用的 Roboto Bold 与 Noto Sans SC Bold 字体 |
| `engine/src/ui/` | 合入 engine 的游戏 UI 对象模块 `comet_game_ui`：RmlUi 呈现、输入适配与项目 Lua 控制器桥接；不包含固定项目页面或菜单流程 |
| `tools/shader/` | 共用 CPU Shader 编译库与构建 CLI，不链接 engine 运行时 |
| `tools/asset/` | 编辑器与独立工具共用的项目 Shader 导入，以及无窗口的启动场景资产准备入口 |
| `tools/render_benchmark/` | 固定场景渲染性能基准及一键运行脚本，链接 engine，不依赖测试框架或编辑器 |
| `tools/asset_scan_benchmark/` | 可选的资产扫描 CPU 基准及一键运行脚本，分别测量候选准备与索引发布 |
| `editor/` | 编辑器入口，`src/` 按 scene、viewport、assets、inspector、project、render、ui 组织，`resources/` 保存私有图标 |
| `editor/src/ui/`、`editor/shaders/` | 编辑器 ImGui 控件与呈现适配；后端单独构建为 `editor_imgui`，设置面板属于 `editor/src/project/` |
| `app/` | 通用项目 Runtime 入口与 `resources/` 私有图标 |
| `demo/assets/ui/` | 示例项目的 RML 页面、RCSS 样式、Lua UI 控制器与菜单图标 |
| `demo/` | 随仓库提供的完整示例项目，与引擎／编辑器源码分开 |
| `demo/assets/` | 示例场景、源资产及相邻 `.meta`；可选大资源由脚本下载，不进入版本控制 |
| `demo/assets/scripts/` | Lua 项目行为；默认字段由脚本声明，实体仅保存覆盖值 |
| `demo/project.json` | 示例项目描述：版本、名称、启动场景和输入绑定 |
| `config/` | 开发者启动 Profile，只覆盖诊断、底层设备参数与资源预算；基础默认值由 C++ 提供 |
| `demo/.comet/` | 示例项目本机缓存、日志与编辑器状态，不进入版本控制 |
| `tests/`、`3rdparty/` | GoogleTest 测试与第三方依赖 |

类入口、依赖方向和资源所有权见[架构文档](docs/architecture/overview.md)。

`engine/src/common/` 提供共享文件读写、JSON 与二进制基础工具和错误定位；各模块负责自身的数据结构与字段校验。
显示、画质和音量选择共用 `config/player_settings` 存储流程，具体设置类型仍归所属功能模块。

编辑器工具栏与 demo 菜单选用了“570+ 图标 v1.0.3”中的少量透明 PNG。
编辑器图标合入 ImGui 字体图集；游戏图标由项目 RmlUi 页面引用。

## 构建与运行

需要 CMake 3.31+、C++20 编译器、Vulkan SDK、Git LFS 和 Submodule。
SPIRV-Reflect、glslang、Jolt Physics、miniaudio、Lua、RmlUi 6.3 和 FreeType 2.14.3 由固定提交 submodule 提供。
RmlUi 只链接 Core，FreeType 随源码静态构建，无需安装系统字体库。
Engine 始终包含游戏 UI 能力；App 和 Editor 按项目的 `ui` 入口决定是否装载界面，视口的“游戏 UI”控制显示。
构建会生成 `comet_shader_compiler`，无需安装 `glslangValidator`；engine／app 不链接源编译器。

```bash
git lfs install
git lfs pull
git submodule update --init --recursive
cmake --preset dev-debug
cmake --build --preset dev-debug --parallel
ctest --preset dev-debug
```

可选的 HDR 天空背景不进入 Git/LFS；需要时显式下载（依赖 `curl` 和 `sha256sum` 或 `shasum`）：

```bash
./tools/download_assets.sh
```

脚本按 1K、2K、4K、8K 顺序下载 Small Hangar 01 的四个版本，总计约 130.7 MiB，默认场景引用 4K。
每个版本有独立的 `.meta`，可在 Environment 的 HDR map 中切换；4K 转成单面 1024 的 cubemap，
8K 转成单面 2048，包含 mip 的纹理约占 256 MiB，解码时还需要额外 CPU 内存。16K 超出导入尺寸限制，不下载。
首次导入期间编辑器可显示低分辨率临时背景；app 等待启用的环境准备完成后启动场景。
可选环境缺失或失败时诊断并回退纯色，重导入失败保留旧版。
脚本可从任意工作目录运行，逐文件校验 SHA-256，跳过已校验文件；下载失败不会覆盖现有资源。

现有构建同时提供项目资产准备工具，可在启动 app 前增量准备项目：

```bash
./build/tools/asset/comet_prepare_project ./demo
```

工具按当前 Profile 额度准备启动场景的 Mesh、Environment 和 ShaderProgram；有效缓存不重写，失败保留旧产物。
声明 UI 入口时也检查页面、模板、样式、控制器及静态图片／字体引用，缺失文件报告来源及引用路径。
它在现有构建中复用 engine 与工具 Shader 编译库，不创建窗口或 GPU 对象，也不生成发布包。
`release.sh` 会在启动 app 前自动执行，无需先打开编辑器。
未下载时 demo 保留环境资产引用并提示缺失，背景回退为纯色；下载后重新打开项目即可。
构建和启动不会自动联网。普通测试使用小型本地数据，不自动导入下载的 HDR。
真实 HDR 验证需显式启用：`cmake --preset dev-debug -DCOMET_TEST_DOWNLOADED_ASSETS=ON`，
再运行 `ctest --preset dev-debug -L assets`；缺文件时跳过。设为 `OFF` 可恢复默认测试范围。
环境缓存位于项目 `.comet/cache/imported/environment/`，可删除重建；输入内容或算法版本变化后自动失效。
后台准备、失败回退和 GPU 发布协议见[环境资产准备](docs/architecture/overview.md#环境资产准备)。
资源来自 [Poly Haven 的 Small Hangar 01](https://polyhaven.com/a/small_hangar_01)，作者 Sergej Majboroda，
采用 [CC0 许可](https://polyhaven.com/license)；脚本下载未修改的原始 HDR，可使用、修改和再分发。
新增可下载资源时，同步维护脚本内的路径／URL／SHA-256、对应的 `.gitignore` 规则和本节来源说明。
`demo/assets/environments/` 默认忽略资源本体，但保留 `.meta`。
相邻 `.meta` 与场景仍进入 Git；小型必需图片、字体继续使用现有 LFS 规则。

macOS 的 CTest 仅在测试进程内关闭窗口动画，避免大量窗口创建/销毁产生的动画任务阻塞 Metal 编译；不影响 app/editor，图形测试仍使用真实窗口和 GPU。

| Profile | 类型 / 目标 | 脚本 |
| --- | --- | --- |
| `dev-debug` | Debug：app、editor、tests、benchmark | `./build.sh` |
| `editor-dev` | RelWithDebInfo：editor | `./editor.sh` |
| `app-release` | Release：app | `./release.sh` |

构建 app/editor 需指定 `COMET_CONFIG_PROFILE`，并按需组合 `COMET_BUILD_APP/EDITOR/TESTS/BENCHMARKS`。
保留 `build/`、`build-editor/`、`build-release/` 三个构建目录，CI 复用 `build/`。
对外通过 `engine`／`Comet::Engine` 使用引擎；内部对象库按职责约束依赖，最终汇入同一个 engine 动态库，
不增加独立构建配置或模块动态库。源码归属见 `engine/cmake/module_sources.cmake`，依赖见 `modules.cmake`。
`comet_runtime` 负责 SceneRuntime、RuntimeSession 与 System 执行契约，只依赖 World／Input 和无后端的服务接口。
`comet_audio` 拥有音频请求、设备和播放实例；Engine 装配 AudioService，Runtime 通过 RuntimeServices 借用命令接口。
`comet_physics` 拥有 Jolt 世界、刚体和冲量队列；PhysicsSystem 同步场景配置并回写姿态，Runtime 借用 PhysicsCommands。
`asset/script` 保存不可变脚本定义，`asset/runtime/script_loader` 准备源码并复用 Lua 校验；共用参数值归 `common/parameters`。
`comet_scripting` 负责 Lua 实例、场景绑定与热重载；ScriptSystem 从同一 Registry 读取定义，Lua 依赖限于实现。
所有场景系统由 SceneRuntime 拥有；Editor 通过只读 `find_system<T>()` 查询已注册系统，Engine 不为单个系统提供专用访问入口。
`comet_runtime_assets` 负责加载、依赖与版本编排，不包含 Vulkan；ImportService 准备 CPU 数据，RenderAssetPublisher 在渲染层创建与发布对象，共用原 Registry。
`comet_platform` 拥有窗口、事件和剪贴板；`comet_graphics` 拥有 Vulkan 后端，窗口 Surface 接线集中在私有适配中。
`comet_render` 编排渲染与资产发布；各层使用自己的配置值，完整 Config 仅由宿主聚合。
`comet_game_ui` 通过 `engine/cmake/game_ui.cmake` 作为对象模块汇入 engine，App 只链接 engine。
RmlUi Core 与 FreeType 均静态编入 engine，不单独部署 UI 动态库。
窗口、渲染和具体系统由 engine 组合。
World 保存场景内容，不依赖 Input 或 Runtime；运行输入和本局状态归 Runtime。
编辑器分为无 ImGui 的 `editor_core`、ImGui 呈现适配 `editor_imgui` 与功能界面 `editor_ui`；新增源码需维护所属库清单。
Editor 宿主负责装配游戏 UI，适配器只借用窗口、Renderer、项目及设置服务。
仅启用 tests 时仍构建 core；测试辅助代码位于 `tests/support/`。
测试按执行条件分组，源码只编译到所属入口，不重复运行：

| 入口／目录 | 依赖 | CTest 标签 |
| --- | --- | --- |
| `unit_testing`，含 `tests/editor/core/` | CPU 逻辑；链接完整运行库 | `cpu`、`unit` |
| `editor_ui_testing`，`tests/editor/ui/` | 内存中的 ImGui，不创建窗口／GPU | `ui`、`integration` |
| `integration_testing`，含 `tests/editor/integration/` | 真实窗口／GPU | `gpu`、`integration` |

`ctest --preset dev-debug -L '^(cpu|ui)$'` 可验证逻辑、无窗口 UI 和构建边界；
`ctest --preset dev-debug -L gpu` 运行 GPU 测试及隔离恢复入口。同一 GPU 用例只在普通集成或同步验证入口执行一次。
完整 `ctest --preset dev-debug` 保持全部回归；仅启用 tests、不构建 editor 时没有 UI 入口。
`COMET_NATIVE_OPTIMIZATION` 只适合本机构建。配置与诊断采用“编译期能力 + Profile 运行时策略”。

### 可复现渲染测量

`render_benchmark` 是固定场景的渲染性能基准程序，用于比较引擎改动前后的性能。
它自行创建场景、采样并退出，不监控其他进程，也不加载任意项目；app/editor 的实时观察使用下方渲染诊断。
一键构建并测量，复用 `build-release/` 的 Release 引擎，不创建另一套专用构建目录：

```bash
./tools/render_benchmark/run.sh
./tools/render_benchmark/run.sh /tmp/comet-benchmark.csv 64 640 360 240 1
./tools/render_benchmark/run.sh /tmp/comet-multi.csv 2048 640 360 240 0 64
./tools/render_benchmark/run.sh /tmp/comet-moving.csv 2048 640 360 240 0 64 moving
./tools/render_benchmark/run.sh /tmp/comet-culling.csv 2048 640 360 240 0 64 culling
./tools/render_benchmark/run.sh /tmp/comet-active.csv 512 640 360 240 0 32 physics-active
./tools/render_benchmark/run.sh /tmp/comet-sleeping.csv 512 640 360 240 0 32 physics-sleeping
./tools/render_benchmark/run.sh /tmp/comet-scaled.csv 256 960 540 480 1 8 static 1 2 0.75
./tools/render_benchmark/run.sh --help
```

无参数时使用第二条命令的场景参数，报告保存到 `build-release/reports/render-benchmark.csv`；成功时替换上一次报告。
脚本仅构建基准目标及其依赖，不构建测试或编辑器、不启动 app；自定义相对报告路径以调用时的工作目录为准。
macOS 测量期间临时阻止系统空闲睡眠，并关闭本进程窗口动画，退出后恢复。
这是工具输出，不是某个用户项目的日志；需要归档的报告可通过参数另存。

`COMET_BUILD_BENCHMARKS` 可独立开启；`dev-debug`／`ci-release` 默认开启，完整构建会生成工具，
完整 CTest 会运行 `render_benchmark_smoke`，但只验证短场景和报告正确性，不比较耗时门槛。
只想快速验证可运行以下命令，Debug 数据不作为正式性能基线：

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug --target render_benchmark --parallel
ctest --preset dev-debug -R '^render_benchmark_smoke$'
```

无脚本时可用 `cmake --preset app-release -DCOMET_BUILD_BENCHMARKS=ON`，然后只构建 `render_benchmark`。
工具位于 `<构建目录>/tools/render_benchmark/`；仅构建工具无需开启 `COMET_BUILD_TESTS`。
`app-release`／`editor-dev` 默认不构建基准；再次使用这些 preset 配置会恢复该默认，不删除已编译产物。

参数依次为 CSV 路径、物体数（1..4096）、逻辑窗口宽高（64..4096）、采样帧数（8..10000）、Bloom（0/1）。
末尾可追加材质数（1..256，不能超过物体数）及场景类型（`static`、`moving`、`light-moving`、`culling`、`project-shader`、`physics-active`、`physics-sleeping`），默认单材质、静态场景。
场景类型后可一起追加 MSAA、各向异性和渲染比例，范围与游戏画质设置相同；省略时仍为 4／1／1。
macOS 可在末尾追加 `-NSAutomaticWindowAnimationsEnabled NO` 关闭该进程的窗口动画；报告以实际 framebuffer 像素为准。
固定场景使用 PBR 材质、共享立方体网格与地面、三类光源、方向光阴影和 SDR 输出；IBL 关闭，
不依赖可选 HDR 下载。资产复制到临时目录后走生产扫描／导入／加载，结束清理，不修改 demo 的资源和缓存。
多材质参数在临时项目中生成稳定身份的 PBR 变体，按网格顺序交错分配，实体身份固定。
`moving` 每帧将所有立方体绕自身 Y 轴旋转 0.5°，走真实 Transform 更新、场景提取与实例上传；不启用物理，便于与 `static` 比较持续变换的成本。
`light-moving` 保持网格静止，仅将方向光绕 Y 轴每帧旋转 0.5°，测量阴影视图更新及实例上传成本。
`culling` 将后 3/4 立方体移到屏外，保留它们的阴影提交；报告记录主材质的候选数、裁剪数和实际 draw 数。
`project-shader` 使用与内置 PBR 相同代码的项目程序，测量项目材质输入同步及逐物体绘制，便于与内置材质区分比较。
内置不透明材质按同一 Mesh 和实际材质版本自动实例化，阴影按 Mesh 合批；项目 Shader 和内置顶点源码覆盖继续逐物体绘制。
各飞行帧槽位复用未变化的实例矩阵，变换或阴影矩阵改变时重新上传；缓存、排序和资源寿命契约见[架构文档](docs/architecture/overview.md#材质准备与寿命)。
提取与提交按场景／实体／变换版本复用矩阵，世界界限只跟随对象变换与 Mesh 发布版本更新；发布材质等其他资产不会使全部界限失效。
资产发布时只释放版本变化的 Mesh、材质和环境引用，下一次解析获取新资源；未受影响的资源与矩阵继续复用。
场景切换、重排、增删及热重载会更新相应输入。
单个对象增删时，资源提交和界限缓存按相邻实体身份对齐槽位，复用未变资源、矩阵与界限；其他重排继续逐项核对。
报告同时记录实际 draw、绘制实例、材质准备、Mesh 绑定，以及主材质／阴影本帧实际上传字节数（稳定场景预热后为零），便于检查合批收益和上传成本。
可见物体少于材质数时只准备可见材质；使用同参数的 `static` 场景观察全可见时的裁剪开销。
物理场景走 Engine 默认系统，每个渲染帧单步推进 1/60 秒，基准限制为 512 个动态立方体加静态地面，为现有接触缓存保留余量；这不是引擎刚体容量上限。
`physics-active` 每 24 步把立方体放回固定空中位置，保持活动；`physics-sleeping` 先沉降 300 步。
采样逐帧检查实际活动刚体数、姿态回写数和固定步数；状态不符则拒绝报告。
预热 32 帧（休眠场景额外沉降 300 帧）后输出 CPU 整帧及分段、CPU/GPU 图和各 pass 的样本数、P50/P95/P99，
以及设备、呈现模式、实际材质／刚体计数和 VMA 分配量。`cpu_runtime_update` 是 `cpu_update` 内的 Runtime 部分，不重复相加；它包含默认系统开销，不是纯 Jolt 模拟时间。
报告分别记录请求画质、实际生效画质、输出与内部场景尺寸；设备可能限制各向异性，不把请求值当成实测值。
`cpu_scene_extract` 以及资产解析、材质程序、几何界限、光源准备均是 `cpu_render_submit` 内的子阶段，不重复计入整帧；后四项位于渲染图录制之前。
GPU 样本按提交序号去重；`gpu_status` 区分完整、部分、不支持和降级，不把缺样本写成零耗时。
窗口／呈现变化、少画物体或提前退出会拒绝报告；成功报告原子替换指定文件。

测量请求关闭 validation，外部强制 layer 仍需自行排除；应在同机 Release、设备保持唤醒、无并行构建或其他 GPU 测试时重复比较。
可分别提高物体数、提高实际分辨率、关闭 Bloom，避免一次改变所有变量，也可固定场景只修改画质参数。
当前材质没有纹理，适合比较 MSAA／渲染比例；各向异性的纹理采样成本需用实际纹理场景测量。物体数增加时会缩小立方体，
它不是纯 CPU 实验，也不代表透明物体、IBL 或编辑器开销；活动物理场景包含周期性传送的更新成本。CPU 墙钟包含等待，GPU 图不含呈现完成，
VMA 分配量不等于系统总显存；各分段百分位不能直接相加。CI smoke 只验证测量契约，不设置绝对耗时门槛。

资产扫描基准可接收任意包含 `assets/` 的项目目录。它先复制资产到临时项目并完成一次预热扫描，
随后对输入不再变化的完整扫描分别输出只读准备和发布阶段的 p50／p95／最大耗时；不会给原项目生成 `.meta`。
复制耗时不计入样本，项目越大越需要留意临时磁盘空间；这项 CPU 测量不等同于编辑器帧时间。

```bash
./tools/asset_scan_benchmark/run.sh
./tools/asset_scan_benchmark/run.sh /path/to/project 30
./tools/asset_scan_benchmark/run.sh --synthetic 1000 30
./tools/asset_scan_benchmark/run.sh --profile --synthetic 1000 30
```

脚本复用 `build-release/` 并只构建所需目标，构建消息写到标准错误，标准输出为 CSV，可重定向保存。
`--profile` 改用 `build-editor/` 的 RelWithDebInfo 构建，把扫描内部阶段的采样写到标准错误；其耗时不能直接与 Release 的 CSV 数值比较。
退出时会清理临时副本；若清理失败，工具会打印残留路径。Debug 构建只用于验证工具，性能判断应使用 Release 与实际规模的项目。
`--synthetic` 会在临时项目生成指定数量的简单 Lua 资产及扫描产生的 `.meta`，用于观察文件数量扩大时的开销；它不代表真实项目的资产类型、依赖或存储条件。

若要观察扫描对编辑器帧的影响，可在 `config/profiles.json` 的 `editor-dev` 分组临时启用 `diagnostics.enable_profiler`，
再用 `./editor.sh /path/to/project` 打开项目并触发资产变化。退出时的 Profiler 日志包含 `Engine::Frame`、
`Editor::on_update`、`EditorAssets::update`、`SceneAssetReferences::restore` 和数据库扫描分段。
这些是各自的累计／最大耗时，最大值不保证来自同一帧，不能直接相加；编辑器「渲染统计」可另行采集帧时间趋势。
测量时保持窗口可见；最小化后的 `Engine::Frame` 可能包含等待窗口事件的时间，不代表扫描卡顿。

### 交互式渲染诊断

`diagnostics.enable_render_diagnostics` 独立于 scope Profiler 的编译开关；`dev-debug` 默认开启，
`editor-dev`／`app-release` 默认关闭。编辑器默认显示「渲染统计」面板，也可通过「视图 / View」菜单显示／隐藏。
配置只决定启动时是否采样；「采集数据」在运行时的帧边界切换采样，隐藏面板不会停止采样，切换结果不写回配置。
面板区分包含等待的 CPU 整帧墙钟时间、场景图 CPU 录制和已完成帧的 GPU 时间；GPU 不包含 UI 绘制与呈现完成，
CPU/GPU 分别统计，不保证来自同一帧。不支持 GPU 时间戳时仍可观察 CPU。
面板每 250 毫秒刷新，显示近 1 秒均值／峰值及近 5 秒趋势；底层仍逐帧采集，峰值不会因 UI 降频而丢失。
隐藏 Viewport 会跳过离屏场景绘制，Runtime、UI 和资源维护继续；诊断面板标明未绘制场景，不将历史图耗时当成新样本。
「暂停显示」只冻结面板，不停止采集；CPU 阶段、渲染阶段和显存堆明细可展开。日常观察无需保存报告。
CPU 阶段明细另列场景提取、资产解析、材质程序、几何界限与光源准备，区分场景准备和图录制的成本；这些子阶段已计入渲染／提交，不能与整帧重复相加。
显存预算至多每秒采样一次，并标明驱动报告或 VMA 估算。「保存显存分配报告」手动生成详细报告，
原子保存到项目 `.comet/editor/diagnostics/gpu-allocations.json`，再次保存替换旧报告，结果写入 Log。

普通文件日志与 Scope Profiler 日志统一保存到**当前项目**的 `.comet/logs/`，
分别命名为 `comet_<时间戳>.log`、`profiler_<时间戳>.log`；app/editor 使用同一目录规则。
例如默认 demo 的路径是 `demo/.comet/logs/`，打开外部项目则写到外部项目内，不依赖仓库根目录或工作目录。
各 Profile 关闭文件日志；在 `config/profiles.json` 的对应分组中将 `diagnostics.enable_file_logging` 改为 `true`
后才创建目录与文件。Profiler 文件还需当前构建支持且启用 `diagnostics.enable_profiler`。
排查资产监视卡顿时，可在 Profiler 输出中分别查看 `AssetSourceMonitor` 的后台局部文件检查／完整快照与主线程结果接纳，以及 `AssetDatabase` 的局部扫描／全量准备／发布（含发布前输入复核和源签名计算）和 `EditorAssets::accept_scan` 的结果处理耗时。
路径由启动入口传入，不作为开发者 Profile 中的机器路径配置。无日志路径时仅保留终端／自定义输出端，
目录无法写入时向标准错误提示并保留这些输出，不回退写到其他目录；项目／配置加载前的失败仍输出到终端。
旧仓库根 `logs/` 不自动搬迁或删除。

App／Editor 共用 `engine/resources/fonts/` 中的 Roboto Bold 和 Noto Sans SC Bold；各自 UI 后端负责加载与 DPI 缩放。
Editor 游戏 UI 按视口显示尺寸换算离屏像素比例，切换渲染分辨率不改变控件的显示大小。
RmlUi 使用 FreeType 解析字体、读取字形度量并栅格化文字，Comet 的 Vulkan 后端上传和绘制图集。
引擎 UI 可配置字体文件、族名与回退；FreeType 可用于其他文字模块，当前 ImGui 仍使用自己的字体后端。
编辑器固定使用简体中文，文案内置在所属界面的 C++ 代码中，不提供语言选择或外部翻译词表。
可交互标签通过 `###` 分隔显示文案与稳定控件 ID，继续复用已有窗口布局和状态；项目名称、脚本字段和原始诊断保持原文。

App 的输出模式和 HDR 校准通过游戏「设置 → 显示设置」在运行中修改并保存，项目设置提供新玩家默认值。
Editor 的 Play 固定使用 SDR 预览，保留独立 App 的输出选择。窗口、VSync、帧率上限、画质、音量和改键
通过编辑器项目设置或游戏菜单修改；项目默认值保存在 `project.json`，玩家选择保存在本地用户目录。
Profile 不接收这些选项，也不接收 HDR 输出／校准字段；没有项目设置的宿主使用 C++ 基础默认值。
开发者 Profile 在启动时读取，只需写入要覆盖的诊断、底层设备格式、在途帧或资源预算；无需复制完整默认值。

`sdr` 强制普通输出；`hdr` / `auto` 在驱动提供 RGBA16F + 扩展线性 sRGB 时使用该组合，否则回退配置的 SDR 格式并记录原因。
Editor 的启动呈现模式使用 C++ 默认的 `immediate`；App 使用项目／玩家显示设置的 VSync，运行中可切换。
`fifo` 等待垂直同步，`immediate` 不等待；设备不支持所选模式时回退并记录原因。
交换链获取、呈现或重建时发生内存不足会报告错误并退出；窗口尺寸变化仍正常重建。
日志区分请求模式与实际模式。`auto` 检测的是 Vulkan 输出支持，不是显示器实测亮度，也不会切换系统 HDR 设置。
macOS 由 MoltenVK 配置 EDR layer；实际高亮受屏幕与系统亮度限制。编辑器启动策略暂时强制 SDR，避免 UI 和视口混用编码。
HDR 使用相对白色的线性输出，不承诺固定 nits；相对白色同时影响场景和游戏 UI，高光范围只影响场景映射。
SDR 忽略 HDR 校准，菜单显示实际输出及回退状态。输出模式切换在帧边界完成交换链、场景和 UI 重建。
暂不支持 HDR10/PQ、跨屏模式适配或自动亮度校准。
SDR/HDR 指显示输出；内部场景目前始终使用浮点 HDR 目标与最终输出 Pass，关闭 Bloom 不会切换成 LDR 管线。

Bloom（泛光）和曝光属于场景内容，不在开发者 Profile 中配置。点击「层级 / Hierarchy」中的「场景 / Scene」，
在 Inspector 的「后处理 / Post Processing」修改：曝光 0..100、泛光开关、强度 0..10、阈值 0..65504。
拖动实时预览，松手记录一次撤销，Esc 取消；双击可输入数值。保存场景后写入 `.scene` 的 `post_process`，
Edit、Play 和独立 app 共用这份数据。Play 中该面板只读，运行时代码可修改 Runtime Scene，不回写 Edit 文档。
新场景默认关闭泛光；demo 场景显式开启强度 0.15、阈值 1。关闭开关保留调好的参数，强度为 0 也不执行泛光。

Bloom 在线性 HDR 中处理高亮后再做曝光和显示映射，不影响 ImGui，也不替代环境照明。
资源准备失败保留上次效果并有限重试；可关闭再开启重试，设备丢失仍退出。
暂不支持自动曝光、相机级覆盖或局部后处理区域；算法与资源协议见[架构文档](docs/architecture/overview.md#bloom)。

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
./release.sh                       # app 运行同一个 demo/project.json 的启动场景
./release.sh /Projects/MyGame      # app 运行外部项目
```

app 和 editor 可执行文件都接受同样的可选路径参数；相对路径以调用者的工作目录为基准。`--help` 显示用法。
编辑器中的“文件 → 打开项目”可输入项目目录或 `project.json` 路径；“文件 → 最近项目”列出最近打开的项目。
“文件 → 新建项目”输入一个尚不存在的项目目录；编辑器会以目录名命名项目，生成 `project.json`、
`assets/scenes/main.scene`（含主相机），并沿用打开项目的未保存场景确认流程切换过去。已有目录不会被覆盖。
ImGui 面板布局是跨项目的用户偏好，与最近项目列表保存在同一用户目录的 `imgui.ini`，不随项目重置。
编辑器主窗口的普通逻辑尺寸与最大化状态保存到同目录的 `window.json`，正常退出或切换项目时写入，
下次启动在创建窗口前恢复；首次启动或记录无效时使用 C++ 内置的 960×720。最小化不会覆盖普通尺寸。
删除 `window.json` 可恢复内置尺寸；app 使用项目默认值或玩家保存的显示设置，不读写此编辑器状态。
列表最多保留 10 项，存于用户目录的编辑器本地状态（macOS：`~/Library/Application Support/Comet/recent-projects.json`；
Windows：`%APPDATA%/Comet/recent-projects.json`；Linux：`$XDG_STATE_HOME/comet/recent-projects.json`，未设置时使用 `~/.local/state/comet/`），不写入项目。
候选项目校验通过后，
编辑器先处理未保存场景，再结束旧会话并打开新项目；取消或无效路径不会切换项目。
运行模式下先停止 Play，才能打开项目菜单。
“项目 → 重命名项目”可修改 `project.json` 的项目名，校验通过后原子写入。
项目描述由 Editor 独占写入；手动修改应先关闭 Editor，再重新打开，不处理运行期间的外部修改冲突。
“项目 → 启动场景”列出当前项目的 `.scene` 文件，可直接指定启动场景，无须先在编辑器里打开目标场景。
刚保存、尚未被资产索引的当前场景也会临时列出。
编辑器会在项目的 `.comet/editor/session.json` 记录上次打开的场景；重新打开项目时优先恢复该场景，
失效时回退到 `project.json` 的启动场景或空场景。明确新建的未保存场景也会记录为“空场景”。
本地会话保存失败只警告，不撤销已完成的场景操作或资产移动；当前会话仍使用新路径。
app 始终使用项目启动场景，不读取编辑器会话状态。
项目需要 `project.json` 和 `assets/`；资源及相邻 `.meta` 一起迁移，`.comet/` 是可重建的本地数据。
编辑器生成的 `.scene`（v2）、`.mat`（v2）、`.meta`（v3）使用 JSON，扩展名不变；
`.scene` 的 `entities` 只放根实体，子实体通过 `children` 嵌套，不再保存 `parent` 引用；UUID 仍全场景唯一。
项目描述 `project.json`（v2）同样使用 JSON；开发者 Profile 和编辑器用户快捷键覆盖也统一使用 JSON。
JSON 解析共用 simdjson。`config/profiles.json` 集中保存三个开发者 Profile，启动入口按 CMake preset 选择一个分组。
只读取该分组的覆盖项，缺省值来自 C++；手动修改后重启生效，不合并其他分组。
后台导入使用有界队列，合并同一资产的旧请求；队列满时暂存重试，内容错误等待新变更或 Reimport，失败保留旧资源。
源文件、预估工作集、队列与外部导入额度由 `AssetImportLimits` 提供默认值，需要调试覆盖时在当前 Profile 中添加 `assets`。
发布默认每次最多 2 项、2 ms 非抢占软预算，均不代表整帧或进程内存上限。
任务与发布边界见[Owner 结构](docs/architecture/overview.md#owner-结构)。示例项目根目录是 `demo/`，可完整复制作为外部项目。
旧仓库根 `.comet/` 不自动迁移；项目缓存缺失会重建。

当前尚未发布，项目及资产描述只接受当前 `FORMAT_VERSION`；缺失、非法或不匹配的版本直接报错。

```json
{
  "version": 2,
  "id": "817a665a-34e6-422c-8d81-32b8e795087c",
  "name": "My Game",
  "startup_scene": "scenes/main.scene",
  "display": {"width": 960, "height": 720, "mode": "windowed", "vsync": false}
}
```

`startup_scene` 相对项目 `assets/`，必须填写非空 `.scene` 路径；项目描述不配置默认材质，场景保存自己的材质引用。
`display` 是游戏显示默认值，省略时采用 960×720、窗口模式、关闭 VSync；出现时四项须完整填写。
可选的 `frame_rate_limit` 为 0 到 1000 的整数，0 表示无上限；项目省略时为 0，旧玩家文件省略时继承项目默认值。
`mode` 支持 `windowed`、`borderless`、`fullscreen`。宽高是普通窗口逻辑尺寸，全屏／无边框使用显示器尺寸；
framebuffer 像素由 DPI 决定，内部渲染比例另行管理。项目默认值与玩家选择在创建 App 窗口前合成；运行中可通过游戏设置菜单调整并保存玩家选择。
`id` 是项目的持久 UUID，新建项目自动生成，改名或移动项目目录不改变它；复制目录并保留 ID 代表同一项目身份。
创建独立项目应使用新项目入口，或显式赋予新的项目 ID。旧版项目描述只报版本错误，不自动转换或写回。
在编辑器中可通过“项目 → 启动场景”选择项目中的场景；未保存的当前场景不能设为启动场景。
修改会立即写入 `project.json`，下次启动 editor／app 时生效。
app 与 editor 共用 Project、SceneSerializer 和场景资产引用，不再分别创建示例物体、相机或灯光。
app 使用场景 primary Camera；Edit 使用编辑器相机，因此同一场景不保证相同取景。
app 窗口创建时使用 `project.json` 的项目名，运行时显示 `项目名 | 120 FPS`；编辑器标题固定为 `Comet Editor`。
窗口标题由宿主提供，不从开发者 Profile 读取。
FPS 复用 editor 的平滑统计，每 0.5 秒采样一次。
该数值表示主循环帧率，不是 GPU 耗时；全屏隐藏标题栏时不可见。
app 启动时同步补齐所引用 Mesh 的 Artifact 并加载资源；指定场景或必需资源加载失败会终止启动，
不像 editor 那样保留缺失引用供修复。这仍是开发期运行入口，不是已打包的 Shipping Player。
示例立方体通过 Script 组件引用 `demo/assets/scripts/spin.lua`，`speed` 为每秒角度，`enabled` 控制是否旋转。
app 和 editor Play 共用该行为，不依赖 UUID 或项目路径；Edit 不执行旋转，Play 修改不保存回 Edit 场景。
示例场景还有一个脚本交互：进入 Play（或运行 app）后，用左右方向键移动左侧小方块碰触右侧条纹目标。
目标的触发回调会记录本次运行的分数、播放一次提示音、退出物理模拟并创建 `Collected_Goal_1` 实体；
目标本身继续升起和旋转约 0.5 秒后删除，中间的旋转立方体读取同一分数后上升并变色。
颜色默认绿色；Edit 中选中 `Editor Cube`，在 Script 参数的 `score_color` 色框调整，再 Play 触发得分即可看到效果。
变色只覆盖这个实体的材质参数；共用 `cube.mat` 的移动方块不变色，Stop 清除覆盖，不修改材质文件。
新实体在目标上方显示为小型条纹方块，使用目标原有网格和材质，不带碰撞或脚本；Play 层级面板也可选中它。
运行中按 `R` 重新开始本局：恢复目标、初始位置和分数，清除得分标记与材质覆盖；app 无需关闭重开，
Editor 无需 Stop／Play。按键由项目动作 `demo.restart` 配置；Edit 场景及撤销历史不变。
空格仍可暂停／恢复中间立方体的旋转。左右方向键绑定在项目 `project.json`，不占用相机的 WASD 控制。
左侧的 `Impulse_Cube` 是动态刚体；按 `J` 施加一次向上冲量，方块弹起后受重力影响落地。
长按不会连续施加，松开再按可再次弹起（不限制必须落地）；Edit 中可调整该实体 Script 的 `impulse` 参数。
刚体面板的“质量 (kg)”默认 1；demo 使用 1 kg 与 3.5 的冲量。保持冲量不变，将质量改成 2 后重新 Play，
方块获得的向上速度增量约减半；质量不会改变重力加速度。质量支持保存及 Undo／Redo，不再随模型缩放隐式变化。
J 绑定在项目动作 `demo.impulse`，与方向键和空格同属 `gameplay` 组；得分后这些操作停止响应，
已产生的物理运动继续，`R` 重开后恢复操作。
demo 的首个标准手柄也可操作这些玩法，绑定仍来自 `project.json`，Lua 不区分设备：

| 操作 | 键盘 | 标准手柄 |
| --- | --- | --- |
| 移动方块／调色时换色 | 左右方向键 | 方向十字键左右 |
| 施加冲量／调色时重置颜色 | J | West（左侧动作键） |
| 切换旋转／调色时确认 | Space | South（下方动作键） |
| 开关调色模式 | Tab | North（上方动作键） |
| 重新开始 | R | Start |

调色组按绑定消费对应手柄按钮，得分后游戏组同样停用；相机和公共重开不受影响。
启用项目 UI 时，App／Editor Play 的 Start 优先打开控制设置，重开使用键盘 R；关闭游戏 UI 的 Editor 仍按项目绑定处理 Start。
菜单导航有模拟手柄回归，真实硬件体验仍需验收。
项目 `.lua` 位于 assets，由 `.meta` 提供身份，也可从 Finder 导入；新增脚本无需改 CMake 或重编译宿主。
引擎私有链接 Lua 5.4.8；参数编辑与运行限制见下方“场景运行时”。
两种入口遇到项目描述错误或缺少 assets 都会启动失败，不回退仓库项目；仅 editor 在启动场景缺失／损坏时
记录错误并打开空场景，供用户修复，不覆盖原文件。
引擎只读取 `config/` 中当前构建选择的开发者 Profile；编辑器快捷键使用内置默认值及用户状态目录中的覆盖文件。
字体／图标／Shader 不需要复制到每个项目。
当前支持在编辑器中创建、打开项目；切换通过重启编辑器进程完成，尚不支持原地切换或独立打包。

### 运行时输入

`Engine::get_input_frame()` 提供键盘、鼠标和标准手柄的只读帧快照，不依赖 ImGui。
Window 采集事件，Engine 在 Update 前发布一次；`down / pressed / released` 分别表示按住／刚按下／刚松开，
同一批事件内的快速按下再松开会同时保留两种边沿。轮询或等待事件不推进快照，最小化跳过 Update 时也不发布。
失焦释放按钮并清空位移／滚轮／轴；手柄首次连接、重连或恢复焦点时只建立按住状态，不伪造一次新的按下。
光标为窗口逻辑坐标，滚轮保留双轴偏移；手柄摇杆为 [-1,1]、Y 向下，扳机为 [0,1]，死区由消费者决定。

App 与 Editor Play 共用可选的 `CameraControllerComponent`：在 Edit 中选中主相机，
通过 Inspector → Add Component → Camera Controller 添加，并配置启用、移动速度、鼠标灵敏度和持续转向速度；保存进 `.scene`。
只控制实际渲染的主相机；未添加／未启用组件时不移动，多个 primary 时与渲染一致选择最小 EntityId。
仓库 demo 已默认添加；外部项目需要同时启用组件并配置下述 `camera.*` 动作，不依赖项目路径或硬编码相机 UUID。
右键拖动转向（本地俯仰限制 ±89°），WASD 沿相机朝向移动，Q/E 沿世界上下移动，左 Shift 加速，滚轮沿视线移动。
转向期间锁定／隐藏光标，鼠标移动不受屏幕边缘限制；松开转向动作恢复光标，支持玩家改键。
平台支持时自动使用原始鼠标位移；macOS 使用 GLFW 的相对位移。第一个标准手柄支持左摇杆移动、左右扳机升降和右摇杆转向。
右摇杆无需按住转向按钮或锁定鼠标，满量程默认每秒转动 120°；可在绑定中调整死区和反向，组件中调整角速度。
鼠标位移不乘时间，摇杆转向按运行时间推进；暂停不转向，单步只推进该步的时间。目前没有相机碰撞。
Play 成功启动或从暂停继续时自动聚焦 Viewport，鼠标停在工具栏也能直接按键操作。
键盘／手柄跟随 Viewport 焦点，鼠标进入画面可自动取得焦点；转向从画面内开始，捕获期间无需保持画面悬停。
未捕获时鼠标按钮／位移／滚轮只在画面内接收；捕获时 UI 暂停鼠标命中，松开转向即可操作面板。
点击其他面板、窗口失焦、弹窗或编辑文字时停止接收；重新取得输入后原先按住的按钮需要松开重按。
Ctrl／Option（Alt）／Cmd 本身可作为游戏绑定，不会中断其他按住的动作；Ctrl+Tab 实际切换编辑器窗口时仍阻断游戏输入。
App 中 Esc 退出；Editor Play 中 Esc 与 Stop 一样返回 Edit。暂停、Stop、失焦、最小化和输入弹窗均释放光标。
控制只改变运行状态，退出 Play 恢复 Edit 场景，不生成逐帧撤销记录。
Edit 相机和编辑器快捷键保持独立；玩家重绑定见下文，文本／IME 仍是后续事项。
Frame 可复制，但不是持久回放格式；授权与阶段消费规则见[运行链路](docs/architecture/overview.md#一帧经过哪里)。

<a id="项目输入动作"></a>

### 项目默认与动作组

`project.json` 的可选 `input_actions` 保存具名动作；省略表示无绑定，不注入示例按键。
编辑器 Edit 模式可通过“项目 → 设置 → 输入”面板添加或修改项目默认动作及绑定，保存时校验并原子写回 `project.json`；
取消或没有变化不会重写文件；写入失败保留原内存配置和草稿。
键盘按键可以录入或填写；鼠标按钮、手柄按钮／轴、鼠标位移／滚轮从下拉框选择。
编辑器中保存的修改在下次 Play 生效，独立 App 需要重启；手动修改项目文件应先关闭宿主，不自动监视或重载。
动作和绑定各有持久 `id`：面板新增时生成，改名、换控制或排序时保留，删除后重新创建不会复用。
ID 不在普通面板展示；动作 `name` 仍是 Lua／相机查询使用的语义名称，改名后相应代码引用仍需同步。
玩家覆盖按这些身份定位，与项目默认值分开保存。

动作可归入具名上下文（动作组），统一启停；没有 `context` 的动作属于始终启用的公共组。以下是项目配置片段：

```json
"input_contexts": [
  {"name": "gameplay", "enabled": true},
  {"name": "menu", "enabled": false, "priority": 100, "consume": true}
],
"input_actions": [
  {"id": "0dcb0121-c04b-493a-811c-b60bfc73c8df", "name": "jump", "context": "gameplay", "type": "button", "bindings": [
    {"id": "17e3e5b7-9cba-4bea-ab67-76cddc2a9111", "source": "key", "control": "Space"},
    {"id": "17e3e5b7-9cba-4bea-ab67-76cddc2a9112", "source": "gamepad_button", "control": "South"}
  ]},
  {"id": "0dcb0121-c04b-493a-811c-b60bfc73c8e0", "name": "move", "context": "gameplay", "type": "axis", "bindings": [
    {"id": "17e3e5b7-9cba-4bea-ab67-76cddc2a9113", "source": "key", "control": "D"},
    {"id": "17e3e5b7-9cba-4bea-ab67-76cddc2a9114", "source": "key", "control": "A", "scale": -1},
    {"id": "17e3e5b7-9cba-4bea-ab67-76cddc2a9115", "source": "gamepad_axis", "control": "LeftX", "deadzone": 0.15}
  ]}
]
```

`button` 合并键／鼠标按钮／手柄按钮的电平与边沿；`axis` 合并数字按键和手柄轴，限制到 [-1,1]；
`delta` 只接受 `motion`（CursorX／CursorY／ScrollX／ScrollY），保留位移单位，不乘 delta time。
`scale` 默认为 1，可用于轴反向；`deadzone` 默认为 0，仅用于手柄轴。手柄取第一个连接的标准设备。
面板切换到 Button 时会将倍率归为 1，保留绑定和组；不兼容的新类型／输入来源仍需显式修改，不自动删除绑定。
键名覆盖现有物理键枚举：A–Z、0–9、F1–F25、方向／编辑键、左右修饰键、标点键（如 `Comma`、`Minus`）、
小键盘（`Keypad0`–`Keypad9`、`KeypadEnter` 等）、锁定键及 `Menu`；不识别的名字会报错，不静默忽略。
录入保留物理键身份，包括小键盘和 macOS 左右 Ctrl／Cmd；平台提供事件时也可录入 `F25`、`World1`／`World2`。
Esc 取消录入，合法的 `Escape` 仍可手动填写。失焦、输入中断或切换编辑对象会取消录入，录入键不触发编辑器快捷键。
录入时仍可用鼠标转到参数输入框或关闭面板；点击参数框后，键盘输入归该字段，不再同时录成绑定。
最多 128 个动作、每动作 16 个绑定；`bindings: []` 显式禁用动作。完整相机配置见 `demo/project.json`：
`camera.move_x/y/z` 为局部右／世界上／局部后方向轴，`camera.look/boost` 为按钮，
`camera.look_x/y` 和 `camera.zoom` 为位移，`camera.look_rate_x/y` 为持续转向轴。
相机缺失动作视为未绑定，类型错误会报告运行失败。持续转向也可绑定数字按键，不把手柄类型判断写进控制器。

Lua 在 `update`／`fixed_update` 中调用 `comet.action_value(name)` 或按钮专用的
`comet.action_down/pressed/released(name)`；未知名称或错误类型会报告脚本错误。
固定步保留零步帧的短按，多次补步只触发一次边沿；普通更新有独立快照，不与固定步抢输入。
demo 的空格／手柄 South 切换方块旋转；运行状态保存在 Lua `self`，Stop 不回写场景参数。

项目输入面板可编辑组、默认状态、优先级和消费开关，并为动作选择所属组。Lua 调用
`comet.set_input_context("gameplay", false)`，在下一次 Runtime 更新开始时生效；Stop／重开恢复项目默认状态。
组的 `priority` 默认为 0、`consume` 默认为 false。启用消费的组会屏蔽较低优先级组中相同的按键／轴；
同级共享，公共动作不参与屏蔽，其他绑定仍可用。消费只作用于配置中的控制，不暂停物理或吞掉整个设备。
“绑定关系”解释双方组都启用时的共享／消费，并标出默认关闭的组；不是当前 Play 状态，合法重叠不阻止保存。

demo 得分后禁用 `gameplay` 组，方向键移动、空格切换与 J 冲量停止响应，`camera` 组和公共的 R 重开仍有效。
按 Tab／手柄 North 开关调色模式：左右方向键／方向十字键换色，J／West 恢复初始颜色，空格／South 确认退出。
调色时这些控制不再操作游戏组，相机和 R 重开仍可用；退出调色不会重新启用已因得分关闭的 gameplay。
成功重载、换绑或清空旋转脚本会退出调色模式，保留已写入 Scene 的颜色，但不保留任意 Lua self 状态。
`on_stop` 可用 `comet.set_input_context("palette", false)` 释放组，不能开启组或操作 Scene／实体；
完整 Stop 恢复项目默认状态。Lua 统一查询具名动作，不提供绕过动作组的原始字母键入口。
这不是完整菜单或输入栈框架；切换基线与固定步规则见[运行链路](docs/architecture/overview.md#一帧经过哪里)。

### 游戏显示设置

编辑器通过“项目 → 设置 → 显示”保存项目默认宽高、窗口模式、VSync、帧率上限和 HDR 输出，不修改已有玩家设置或编辑器主窗口。
没有玩家设置的 App 使用项目默认值；已有玩家可以在游戏菜单恢复默认后应用。
demo 在“设置 / F1”中提供显示设置；宽高可直接输入，也可点击预设尺寸在 960×720、1280×720、1920×1080 间切换。
点击模式或 VSync 请求切换选项，再点“应用显示设置”。非法宽高保留草稿并提示错误，不关闭菜单。
App 修改尺寸、窗口模式或输出模式后先试用 15 秒，点击“确认保留”才保存；超时或按“还原 / Esc”恢复试用前的设置。
试用期间其他设置控件暂时禁用；菜单热重载沿用原倒计时，退出程序不会保存未确认的选择。最小化时暂停更新，恢复窗口后处理超时。
只修改 VSync、帧率上限或 HDR 校准时直接应用并保存，无需确认；输出模式仍等待交换链应用后才能确认。
帧率上限可输入 0 到 1000，或依次选择无上限／30／60／120／144／240；默认无上限。
主循环每帧只等待剩余时间，不忙等；VSync、GPU 开销和系统调度可能使实际帧率低于上限。它不修改物理固定步频率。
显示与改键分别提交，取消只丢弃尚未应用的草稿；输入框草稿随 UI 热重载保留。
“显示默认值”恢复项目默认草稿，仍需应用。VSync 在后续帧复用交换链重建，设备不支持关闭时会保留同步呈现并记录原因。
“当前同步呈现”显示实际交换链状态，与尚未应用的 VSync 草稿分开；设备回退时可据此确认结果。
App 退出时也记住普通窗口拖动后的尺寸，不用全屏或最大化尺寸覆盖它。
玩家选择保存到改键文件旁的 `display.json`，按项目 UUID 隔离，不写回项目；改名／移动项目继续使用同一份设置。
Editor Play 将尺寸用于固定像素分辨率预览，直接应用；窗口模式、VSync、帧率上限和 HDR 控件禁用，恢复默认也保留这些 App 偏好。
Editor 主窗口尺寸与最大化仍保存为独立本地状态。

### 游戏画质设置

“项目 → 设置 → 画质”保存 MSAA、各向异性过滤和渲染比例的项目默认值。`project.json` 的可选 `quality` 字段如下：

```json
"quality": {"msaa_samples": 4, "max_anisotropy": 8, "render_scale": 1}
```

省略时使用上述默认值；MSAA 为 1／2／4／8，各向异性为 1～16，渲染比例为 0.5～1。
App 启动使用玩家选择或项目默认值；Editor 在进入 Play 时应用同一份设置。
demo 的“设置 / F1”提供设备支持的 MSAA、各向异性选项和 50%／75%／100% 渲染比例。
另提供三个草稿档位；档位策略由 demo 的 Lua 控制器定义，Engine 只应用参数，不保存平行的档位编号。

| 档位 | MSAA | 各向异性 | 渲染比例 |
| --- | --- | --- | --- |
| 性能 | 1× | 2× | 75% |
| 均衡 | 2× | 4× | 100% |
| 质量 | 4× | 8× | 100% |

MSAA 选不超过目标的最高支持值，各向异性限制到设备上限；设备限制使多个档位相同时，菜单并列显示匹配名称。
仍可逐项修改，未匹配档位时显示“自定义”；恢复默认取项目值，不固定为某个档位。档位名称不承诺目标帧率。
“应用画质设置”单独提交并保存到玩家目录的 `quality.json`；取消保留已应用值，丢弃未应用草稿。
更改在下一帧准备时生效，旧资源保留至在途 GPU 帧结束；失败保持原画质并显示原因，可以再次应用。
菜单显示实际生效值，各向异性按设备上限限制。画质切换会重建目标和 Pipeline，可能产生一次短暂停顿。
渲染比例仅缩放 HDR 场景及 Bloom，最终输出与游戏 UI 保持原分辨率；Play 中不会改变编辑器主窗口。

### 游戏音量设置

“项目 → 设置 → 音频”保存主音量、音效和音乐的默认值；已有玩家选择保持独立。
`project.json` 可选的 `audio` 字段缺省时三项均为 1：

```json
"audio": {"master_volume": 1, "effects_volume": 1, "music_volume": 1}
```

App／Play 的“设置 / F1”提供三个 0–100% 滑块。“应用音量设置”保存到玩家目录的 `audio.json`，
按项目 UUID 隔离，重开程序或 Play 时恢复；“音量默认值”只恢复草稿，取消丢弃未应用的值。
主音量和分类音量作用于正在播放及后续声音，不重启播放；最终增益为音源音量 × 分类音量 × 主音量。
Audio Source 的“音频分类”选择音效或音乐，缺省为音效；自动播放和 `comet.play_one_shot()` 都遵守该分类。
显示、画质、音量和改键各自提交；“应用改键并返回”只提交改键草稿。
音乐分类目前也使用已解码的短 WAV，长音乐流式播放继续按路线图推进。

### 玩家改键与保存

App 通过画面右上角“设置”、F1 或手柄 Start 打开 RmlUi 控制菜单，提供 FPS HUD、动作切换、
按钮录入、禁用、恢复默认、应用和取消。Tab／方向键或手柄方向键导航，Enter／South 确认，Esc／East 返回。
Editor 视口的“视图 → 游戏 UI”控制项目界面显示：Edit 预览 HUD，Play 使用同一页面与 Lua 控制器，暂停时菜单仍可操作。
“视图 → 重载 UI”重新装载页面和控制器；初次装载失败不影响编辑，修正源码后可重试。
Play 中“视图”的调试分组保留“输入”，打开 ImGui 玩家面板；打开时关闭项目菜单，两者共用改键模型、宿主设置实例和提交策略，各自持有草稿。
视口失焦、隐藏和 Stop 释放输入；菜单关闭当帧阻断游戏，Esc 在菜单内返回，菜单关闭后才用于 Stop。
app 首轮不提供来源、倍率和死区编辑，已有这些字段及未显示的覆盖记录仍保留。

项目通过 `project.json` 的可选 `ui` 声明页面和 Lua 控制器，路径相对 assets：

```json
"ui": {"document": "ui/runtime.rml", "controller": "ui/runtime.ui.lua"}
```

没有 `ui` 的项目不创建游戏界面；无效入口直接报告错误。app 不约定菜单控件、命令或快捷键。
demo 的页面、样式与 HUD／改键菜单交互位于 `demo/assets/ui/`；修改 `runtime.ui.lua` 无需重编译 app。
Lua 编排界面操作；录入、草稿校验和提交状态由 Engine 的 C++ `PlayerInputEdit` 实现，个人设置保存与运行时换绑通过宿主服务完成。
页面通过 `data-model="ui"` 使用控制器的标量 `model`，通过 `command(...)` 交付控件事件。
控制器的 `on_mount` 校验页面，`on_frame` 接收 FPS／游戏可用状态，`on_present` 更新呈现，
`on_event` 决定交互；`on_input_result` 决定保存后关闭或保留菜单，`on_deactivate` 处理场景重启。
受控 API 提供模型、布局、焦点和 `PlayerInputEdit` 事务服务，不暴露 GPU 或 Editor 对象。
生命周期、API 与预算见[项目 UI 控制器](docs/architecture/overview.md#项目-ui-控制器)。

共用字体位于 `engine/resources/fonts/`，复制到 app 资源目录；项目页面始终从自身 assets 装载。
demo 按 F6 手动重载页面与控制器，候选失败保留旧页面、控制器和改键草稿；
成功重载迁移 `state` 中的标量及已有模型值，取消正在进行的按键录入。
资产扫描接受 `.rml`／`.rcss`／`.ui.lua` 源文件，不生成 `.meta` 或资产句柄；组件脚本继续使用普通 `.lua`。
项目准备与 UI 重载共用静态依赖收集，包含隐藏控件、媒体条件内的图片、精灵图集及 `@font-face` 字体；
结果相对 assets 排序去重，重载验证成功后才替换当前清单。图片和字体内容仍在实际装载时解码校验。
文档／模板引用相对当前文档，样式中的图片相对样式文件；模板中的 `<img>` 沿用入口页面基准，`@font-face src` 相对 assets。
代码或数据绑定动态生成的路径尚不属于该清单；独立导出及动态资源声明继续按路线图推进。
项目 UI 的后续交互验收、资源导出、IME、滤镜与图层等功能计划见[路线图](docs/engine-roadmap.md#项目-ui-入口与控制器部分实现)。
已提交 Unicode 文本与中文字体支持不能替代完整 IME 验收。

以下高级控件说明适用于 Editor 玩家面板：
可按动作名筛选下拉列表（英文字母不区分大小写）；清除筛选恢复全部候选，不改变当前动作或个人配置。
它只修改已有绑定的控制、倍率／死区和禁用状态；新增动作／绑定仍在项目默认设置中完成。
应用先保存个人文件，再于下一次输入更新边界替换绑定，不重启场景／System，也不重置当前动作组。
玩家文件在 App 启动、重开游戏、每次进入或重开 Play 时读取；菜单关闭和重新打开复用本局的宿主设置实例。
同一项目的个人文件约定由一个活动宿主写入，运行中不支持外部修改或多个宿主同时保存。

键盘和标准手柄按钮可直接录入，轴与鼠标控制从下拉框选择。例如选择 `spin.toggle` 的手柄绑定，
点“录入按钮”后按 East，再应用即可用 East 切换旋转。只接收开始录入后的新按下，不把原先按住算作一次录入。
失焦或输入中断会取消录入；手柄锁定当前首个已连接槽，换槽或该槽已采样到的断开／重连会取消手柄录入，
不影响键盘录入。已发生的失焦和已采样的断连不会因 UI 跳帧被遗忘。
Esc 先取消录入，再按关闭面板；面板及关闭当帧不向游戏交付输入，但不自动暂停模拟。
转去编辑倍率或死区会结束录入，输入数字只修改该字段，不会同时变成新的绑定键。
切换离开手柄轴时清零不适用的死区；切回手柄轴时重新继承默认死区。同一轴来源内换控制保留个人死区，包括显式零值。
来源切回原类别时优先使用项目默认控制；默认控制被宿主保留时仍选择其他合法候选，不恢复此前个人来源历史。
当前宿主保留 Esc 用于退出／停止；app 还保留 F1／F6 用于菜单和模板重载，禁止新录入这些键。
已有绑定仅警告、不自动删除。
项目默认面板仍允许填写合法 `Escape`，宿主保留策略不改变文件格式。

绑定标明继承默认、个人覆盖或覆盖未生效；“绑定关系”使用合成后的有效配置，仍是上述两两关系说明。
禁用动作或绑定保留个人字段，重新启用时按当前默认重新校验；禁用行的只读摘要不代表正在生效的映射。
“恢复绑定／动作／全部”才删除相应覆盖、重新继承默认，不把默认值复制成个人配置。
失配记录保留并显示诊断，控件展示有效默认；恢复后才能编辑控制字段。诊断旁的“移除此覆盖”可单独清理失效项，
无需因项目删除默认动作／绑定而清空其他改键。

取消不保存；保存失败保留草稿与原运行配置，并在面板顶部显示原因，可修正问题后重试。
若保存成功但运行时拒绝换绑，会明确提示“已保存但未应用”，保留草稿供再次应用，不回滚已保存文件。
加载失败显示错误弹窗，不把坏文件当作空配置覆盖。小窗口可滚动正文和长错误，底部应用／取消／全部恢复始终可达。
App 与 Editor 共用 Engine 目录内的字体；App 不依赖 Editor 的翻译或布局文件。

文件位于用户配置目录下的 `players/<project.json 的 id>/default/input.json`，不是项目 `.comet`：

- macOS：`~/Library/Application Support/Comet/players/...`
- Windows：`%APPDATA%/Comet/players/...`
- Linux：`$XDG_CONFIG_HOME/comet/players/...`，未设置时使用 `~/.config/comet/players/...`

当前只提供一个本地玩家 `default`，它不是“第一个手柄”的身份。文件不存在时使用项目默认值，不自动创建文件。
例如 demo 中把旋转开关从 Space 改为 K，文件内容为：

```json
{
  "version": 2,
  "project_id": "bf04a980-f080-4d3f-b1ac-1a938379a190",
  "actions": [{
    "id": "0120f784-0b77-4a50-bba9-000000000009",
    "type": "button",
    "bindings": [{
      "id": "0120f784-0b77-4a50-bba9-000000000033",
      "source": "key",
      "control": "K"
    }]
  }]
}
```

未写入的控制、倍率、死区继续继承项目默认值，手柄 South 及后来新增的绑定也保留。
动作或绑定的 `"disabled": true` 对应上述禁用开关，可与个人字段共存。
纯禁用记录在重新启用时移除；删除最后一条绑定覆盖时也删除未禁用的空动作记录。
全部恢复可保留 `"actions": []`。玩家文件当前为 v2；旧版本只报错，不自动迁移或覆盖。
未知身份或类型变化只跳过对应记录并报告；非法 JSON／格式／项目 ID 则整文件回退默认。启动不会清洗或覆盖原文件。

保存、映射和 UI 边界已有自动回归；真实 App／Play 的“应用—运行生效—重开”及真实手柄体验尚未完成整体验收。

### Lua 实体与会话

脚本使用受保护的实体引用，不访问 EnTT 或渲染对象。常用接口：

| 接口 | 用法 |
| --- | --- |
| `comet.self_entity()`／`comet.find_entity(uuid)` | 当前实体／按 UUID 查找；引用提供 `:is_valid()`、`:position()`、`:translate()`、`:rotate()` |
| `{type = "entity"}` 参数 | Inspector 选择或从 Hierarchy 拖入；场景保存 UUID，可与接触回调的 `other` 比较 |
| `comet.create_entity()`／`comet.destroy_entity()` | 请求创建／删除子树，在阶段末提交，只修改运行场景 |
| `comet.has_rigid_body()`／`comet.remove_rigid_body()` | 查询／请求移除刚体，保留实体与其他组件；提交前查询仍见旧组件 |
| `comet.apply_impulse(x,y,z)` | 向本实体质心施加世界空间冲量，要求 Transform、Collider 和 Dynamic Rigid Body |
| `comet.session_set/get()` | 共享本局临时值；支持 bool、有限数值、string、Vec3，设置 nil 删除 |
| `comet.restart_scene()` | 恢复启动内容基线，不重读磁盘；demo 绑定 R |

创建时可设置本地 Transform 与网格／材质来源；不复制其他组件或运行覆盖，不等于 Prefab：

```lua
local id = comet.create_entity("Marker", {
    translation = {0, 1, 0},
    scale = {0.15, 0.15, 0.15},
    mesh_source = comet.self_entity(),
})
```

`comet.emit("demo.score_changed", score)` 发送场景通知，接收脚本声明事件与方法的映射：

```lua
local script = {}
script.events = { ["demo.score_changed"] = "on_score_changed" }

function script:on_score_changed(score)
    -- 根据新分数更新本实体的表现。
end

return script
```

通知载荷与会话值类型相同，也可省略；会话保存状态，通知表达变化。接触回调为 `on_collision_enter/exit(self, other)`，
Trigger 对应 `on_trigger_enter/exit`，不产生物理碰撞响应。结构提交、引用失效、容量、暂停和交付顺序见[Lua 架构](docs/architecture/overview.md#lua-脚本与参数)。

### 场景运行时

app 与 editor Play 共用 SceneRuntime：先固定更新，再普通更新，退出时逆序停止 System；Edit 不执行游戏行为。
RuntimeSession 保存本局会话值、输入组请求和重开意图，System 显式访问当前会话；Lua API 保持一致。
暂停保留会话，停止／失败后清空；场景保存或克隆只复制内容，两个运行域不共享本局状态。
宿主在 UI 更新后通过 `on_runtime_input` 每个非挂起帧交付一次授权输入；渲染延期仍推进 Runtime，未授权只关闭游戏输入。
默认固定步 1/60 秒，每帧最多补算 8 步；暂停仍允许 UI 和资源维护，单步只推进一次固定更新和普通更新。
Play 修改只作用于副本；脚本启动或运行失败会记录错误并恢复 Edit，不关闭编辑器。设备丢失等渲染故障仍退出。
独立 app 默认在运行错误时退出，不自动重试已部分执行的一帧。

demo 场景的 Ground 有静态盒碰撞体，Falling Cube 有动态刚体；打开编辑器点击 Play（或运行 app）即可看到方块落地，
Edit 中位置保持原样。刚体与碰撞体在 Inspector 添加、编辑并保存到 `.scene`；物理世界不会保存，Stop 即销毁。
Engine 为每个运行域装配 PhysicsService；启动前绑定服务，停止／失败时清空模拟对象和待处理冲量。
脚本可在 `on_start` 提交冲量，下一固定步执行；未装配物理服务时调用 `comet.apply_impulse` 会明确报错，组件配置仍可编辑和保存。
只添加碰撞体不会参与模拟，还需添加刚体；静态刚体本身不会下落，也只有与其他物理 body 接触时才有碰撞效果。
运动类型可选静态、动态、运动学：动态由物理推进并回写位置；运动学由脚本／场景 Transform 给出每个固定步的目标，
不受重力或碰撞反推，可推动动态物体并触发静态 Trigger。脚本驱动移动平台或旋转障碍物应使用运动学，
不要与动态物理同时控制 Transform。运动学不是角色控制器，不会自动被墙阻挡；普通静态／运动学物体之间暂不产生接触通知。
目前只支持无父级实体的盒／球碰撞体；盒尺寸乘以实体正缩放，球体要求均匀正缩放。
首版在主线程模拟，最多 1024 个刚体；脚本可收到碰撞／触发进入与离开通知，约束和角色控制器尚未接入。
接触回调不会因为刚体进入休眠而报告离开；移动／删除静态支撑或触发区会唤醒附近物体，重新检测实际接触。

demo 的 `Move_Cube` 使用运动学刚体，与目标保持同一高度；Play／app 中按左右方向键移动，无需先等待它落地。
目标的 Script 参数 `player` 指向 `Move_Cube`，只有指定方块碰触目标才计分；其他物体不会误触发。
目标附有 Audio Source，关闭启动自动播放；触发时脚本调用 `comet.play_one_shot()` 播放一次 `demo/assets/audio/play_chime.wav`。
这项调用提交当前实体 Audio Source 的片段、音量和分类快照，忽略循环配置；AudioService 保存请求和播放实例，目标在同帧删除也不会立刻截断声音。
AudioSystem 只同步声音源组件；每个运行域使用独立服务，默认由 Engine 装配。脚本可在 `on_start` 请求播放；停止／失败清空声音和待播请求。
没有 Audio Source、有效片段或音频服务时会报告脚本错误。一般声音源仍可通过 `play_on_start` 在 Play／app 启动时自动播放，默认开启；Edit 不播放，Stop 销毁播放实例。
项目 WAV 由 Git LFS 管理，与相邻 `.meta` 一起使用，也可从 Finder 拖入 Project；场景保存 Audio Clip 的 Handle、自动播放、循环、0..1 音量和音效／音乐分类。可在 Inspector 添加或编辑 Audio Source。
首版将短音效完整解码到内存，限制为单／双声道、约 1600 万采样值；尚无流式音乐、空间定位或混音编辑器。
脚本短音效最多同时播放 64 个，超额新请求直接丢弃，不排队补播；不占用自动 Audio Source 的播放名额。
无输出设备时静音继续；暂停冻结声音，单步静音推进声音时间，继续只播放剩余部分，Stop 全部丢弃。

### Lua 参数与材质

Lua 的 `properties` 声明显式导出的 bool／float／Vec3／Vec4／string 及实体引用配置；只有编辑过的字段保存为实体覆盖。
裸三／四分量数组分别是 Vec3／Vec4，只有显式 `type = "color"` 才显示颜色控件，不根据变量名猜测：

```lua
script.properties = {
    weights = {1, 0, 0, 1},
    score_color = {type = "color", default = {0.2, 1, 0.25, 1}},
}
```

颜色在运行时仍是 Vec4，使用 `self.parameters.score_color[1]` 到 `[4]` 分别读取 RGBA；
数值允许有限的 HDR／负值，不自动做 gamma 转换。`.scene` 只保存覆盖值，颜色编辑语义保留在 Lua 中。
实体声明只包含 `type = "entity"`，目标由场景配置，不在脚本源文件硬编码默认 UUID。
Inspector 切换／清空 Script 引用会同时清空覆盖，一次 Undo 恢复旧脚本和参数；加载失败不改原绑定。
“恢复默认参数”清空覆盖，可撤销，不重新加载源码。Play 面板跟随活动实例的定义，不混用更新后的资产参数。
只恢复某一项时，右键参数名称选择“使用脚本默认值”，删除该项覆盖并保留其他配置；
之后会跟随脚本默认值变化，而不是写入一份当前默认数值。颜色输入框原有右键选项保留。
源码删除字段或改变类型后，可点击“移除不兼容参数覆盖”，仅删除失配项，保留其他已配置的值；
这也是一次可撤销的编辑，不会自动保存场景或改写脚本。

编辑器 Play 中保存当前使用的 Lua 文件，既有资产监听会验证并发布新版；下一次实际运行更新时重建该脚本的活动实例。
Project 中右键组件脚本或 `.module.lua` 选择“打开源码”，可交给外部文本编辑器修改；
macOS 优先使用已安装的 VS Code，无需配置 `code` 命令；未安装时使用系统默认文本编辑器。
Windows 使用系统记事本，Linux 使用 `text/plain` 默认程序。
此操作不运行脚本、不改资产身份，也不自动保存场景；没有编辑程序或源文件已移走时会显示错误。
脚本和模块使用 UTF-8 文本，允许文件开头的 UTF-8 BOM；LF／CRLF 均可，暂不转换 UTF-16 等其他编码。
可把 `demo/assets/scripts/spin.lua` 中的 `comet.rotate` 方向改为负数，保存后观察旋转反向，无需 Stop／Play。
暂停时等待继续或单步；同名同类型的参数覆盖保留，删除／改类型的覆盖丢弃并采用新默认值，不改 Edit 场景。
新实例重新执行 `on_start`，不保留任意 `self` 状态，也不重置整个场景的物理或会话值。
语法／声明错误不替换旧版本；新 `on_start` 的执行错误仍会停止 Runtime，并让编辑器恢复 Edit。
Inspector 在 Play 中借用只读 ScriptSystem 查询实际运行版本，暂停或候选安装失败时继续显示旧实例的字段；组件只保存脚本引用和参数覆盖。
独立 app 共用实例换代机制，但本轮不为它增加开发期源文件监听。详细顺序见[Lua 架构](docs/architecture/overview.md#lua-脚本与参数)。
Stop 后再次 Play 仍读取未自动改写的 Edit 覆盖；若字段已改名／改型，先在 Edit 中移除不兼容覆盖，再按需配置新字段。
`self.parameters` 是只读配置；累计时间等内部状态放在 self 的其他字段，不显示或保存到场景。
目前每实体一个脚本，提供本实体变换和已授权的具名动作查询。
脚本可修改本实体 MeshRenderer 的已登记材质参数，例如 PBR 材质：

```lua
comet.set_material_scalar("roughness", 0.25)
comet.set_material_vector("base_color", 0.2, 1, 0.25, 1)
```

这只创建运行时覆盖，不修改共享 Material、`.mat`、Edit 场景或 Undo 历史；暂停保留，单步按脚本更新，Stop 清除。
名称、类型、有限值及标量声明范围须符合布局，否则按脚本错误处理；Vec4 不统一限制在 0..1。
参数变化不重新编译 Shader；布局校验、快照复用及 GPU 发布边界见架构文档。
暂不支持脚本纹理切换、全局 Shader 参数或保留任意 Lua 状态的热迁移。
Lua 有内存与指令预算，但不是面向不可信代码的安全沙箱。调用、寿命和失败边界见[架构说明](docs/architecture/overview.md#lua-脚本与参数)。

脚本可使用普通辅助方法，不必把每段逻辑写进生命周期函数：

```lua
local script = {}
script.properties = {speed = 1}

function script:move(dt)
    comet.translate(self.parameters.speed * dt, 0, 0)
end

function script:update(dt)
    self:move(dt)
end

return script
```

辅助方法与生命周期共用错误处理及执行预算，运行状态保存在各实体自己的 `self`；只有显式 `properties` 进入 Inspector。
普通错误在 Log 显示源码位置和调用栈；内存耗尽不保证完整调用栈，这不是断点调试器。
需要观察正常执行时，可在生命周期／事件方法及其辅助方法中调用：

```lua
comet.log("Goal collected; score=" .. score)
```

消息以 Info 进入 Log 与 app 日志，带源码位置；仅接受一个字符串，顶层准备阶段不可调用。
每回调最多 16 条、每条 4096 字节，超限省略并最多提示一次 Warning；详细预算见[Lua 架构](docs/architecture/overview.md#lua-脚本与参数)。

### Lua 模块复用

项目组件脚本可以在顶层通过 `require("scripts.demo_score")` 引用
`assets/scripts/demo_score.module.lua`，点分名称从项目 assets 根解析，不要求文件都放在 scripts 目录。
模块返回 table，同一实例重复 require 返回同一张表；不同实体不共享这张表或其中的运行状态。
demo 的 `collect_goal.lua` 和 `spin.lua` 共用计分模块；刻意共享的分数仍由 `comet.session_get/set` 保存。

```lua
local demo_score = require("scripts.demo_score")
local script = {}

function script:on_start()
    self.last_score = demo_score.get()
end

return script
```

`.module.lua` 是源码依赖，不生成 `.meta`，不能挂到实体的 Script 槽位。
Project 右键 New Lua Module 可创建空模块，显示可复制的 `require` 引用；New Script 仍创建组件脚本。
模块在 Project 中显示为源码文件，点击或拖动不改变当前资产选择；右键 Rename 可在同目录内改名，支持仅修改名称大小写，保留 `.module.lua` 后缀。
可将模块拖到现有项目目录或 assets 根节点，移动与改名共用单源码事务，不生成 `.meta` 或场景撤销记录。
例如移到 `scripts/lib/shared.module.lua` 后，作者需要将引用改为 `require("scripts.lib.shared")`；
拖动时提示引用不会自动更新，成功后 Log 记录旧／新 require。目标冲突或目录名不能组成模块名时保留原文件并报错。
弹窗显示新旧 `require` 引用，引用代码需手动更新；未修好的已加载脚本保留旧版本，首次加载则报告缺失模块。
右键 Delete 确认后将模块源码移入系统回收站，不清除 `require` 引用；源码内容仍由外部编辑器编辑。
模块不参与资产引用拖放或场景撤销，补回同路径文件后可恢复依赖它的脚本。
名称及父目录须为 ASCII 标识符（字母或下划线开头，后续可用数字），点分引用不超过 256 字节。
Finder 导入只支持独立组件脚本，暂不处理 Lua 多文件依赖包；需要模块的脚本直接在项目 assets 内编写。
路径限点分标识符，不支持绝对路径、`..`、原生库、符号链接别名或运行回调中首次发现新模块。
生命周期回调内可再次 require 已在顶层加载过的模块，不读取磁盘。

编辑器保存共享模块后，现有依赖索引会刷新关联脚本；整组候选失败保留旧版，成功则在下一次运行更新切换。
暂停中仍等单步或继续；实例的模块状态随换版重建，不改 Edit 场景和历史，也不重置 RuntimeSession 会话值。
首次缺失模块或中途删掉模块会报告错误，补齐文件后可由既有资产监听恢复；app 加载同一项目模块，但不自动监视源码。

## 编辑器使用

- File → Open/Save 操作当前项目 assets 内的 `.scene`，拒绝越界路径。
  编辑器优先恢复项目 Session 中上次明确打开／新建／保存的文档，恢复失败再尝试启动场景；
  app 始终使用项目启动场景。坏资源引用保留并记录 Log，后台导入完成后自动重试加载。
- Edit 使用独立相机；Play 运行场景副本及其 primary Camera，Stop 不回写运行时修改。
  Edit 工具栏的 2D/3D 菜单切换编辑相机投影；选中场景相机可在 Inspector 设置 Play/app 使用的透视或正交投影及正交高度。
  Play 的预览菜单合并分辨率与显示缩放：自由、16:9、1280×720、1920×1080，适应为等比缩放，1:1 为原尺寸裁切。
  入口显示当前预览设置，窄视口中缩成“预览”，悬停可查看完整设置。
  Edit 只显示播放，Play 显示停止、暂停／继续，暂停时显示单步；图标悬停显示说明，控制请求在下一次宿主更新执行。
- Edit 视口右键或 Option/Alt+左键环绕，中键或 Option/Alt+Shift+左键平移，滚轮／双指滚动缩放。
  左键按模型包围盒粗拾取，空白点击清空；视口获得键盘焦点后按 F 聚焦选中 Mesh。橙色选中框受场景遮挡。
- Edit 选中实体后，Tool → Mode 选择 Move／Rotate／Scale，拖动轴、圆环或缩放方块。
  Move／Rotate 的 Space 可选 World／Local；Scale 固定 Local，轴手柄调整单分量，中心手柄沿屏幕右上拖动等比放大。
  Snap 相对拖动起点吸附，默认距离 0.25、角度 15°、缩放增量 0.1，设置只保留在会话中。
  World 旋转不接受非均匀缩放父级，此时使用 Local。Escape、失焦或隐藏视口取消拖动。
- Edit 中名称、Transform、Camera 和 Mesh/Material 引用支持撤销；一次手势只记一条历史。
  Inspector 的 Add Component／组件标题右键支持 Camera、Mesh Renderer、Light 增删，Name／Transform 不开放增删。
  Play 仅实时调试已有属性，不记录 Edit 历史；Stop 恢复原 Edit 历史，New/Open 成功才清空历史。
  New/Open 和窗口关闭遇到未保存场景时提供 Save/Discard/Cancel；保存失败或取消另存路径不会继续切换。
  未保存状态使用历史状态 ID 与保存点判断，支持撤销回保存点、分支编辑和历史截断；不包含独立的资产文件编辑。
- Light 支持 Directional／Point／Spot，类型和参数共用场景保存与撤销。方向由 Transform 的本地 -Z 决定；
  Point／Spot 的 Range 是世界距离，聚光角度是半锥角。受光材质统一使用 `pbr`，无直接光与环境照明贡献时为黑色。
  Directional 的 Cast shadow 可启用阴影；最多选择一盏有效方向光，使用 1024² 深度图与 3×3 PCF。
  默认示例包含投影 Key Light、Ground 和纯色 PBR 地面材质 `materials/ground.mat`。
  阴影覆盖当前提交网格的包围盒，暂不支持级联、透明裁切或点／聚光阴影。
  内置静态网格材质按当前相机做视锥裁剪，屏外物体仍参与阴影；项目 Shader 暂不裁剪，以免误判顶点变形。
  PBR 支持全局 IBL：环境漫反射和随粗糙度变化的镜面反射，不添加固定 ambient。
- 点击 Hierarchy 的 Scene，在 Inspector 的 Environment 中选择 HDR map，勾选 Background 显示天空盒。
  Lighting 独立控制环境照明，Lighting intensity 控制照明强度；隐藏背景时仍可照明，Intensity 只控制背景。
  背景色 / Background color 保存在线性 RGB 的场景环境设置中（0..65504），不再读取引擎 `clear_color`。
  新场景默认黑色；关闭天空盒或环境资源尚未就绪时显示该颜色，它仍经过曝光、Bloom 和显示映射。
  demo 预配置并启用了 Poly Haven 的 Small Hangar 01 4K HDR 背景和照明（CC0，约 25.1 MiB），app/editor 共用；
  使用 `./tools/download_assets.sh` 获取，来源与许可见上方构建说明，运行时无需联网。
  强度范围 0..64，旋转绕世界 Y 轴、复用 Transform 的角度循环规则；拖动实时预览，松手提交一次撤销，Esc 取消。
  双击可输入数值，回车或失焦提交；保存、撤销和进入 Play 前统一结束当前环境编辑。
  配置支持撤销、保存重开及 Play 克隆；Edit 中可下拉选择或从 Project 拖入环境资产，Play 中只读。
  缺失环境引用保留并诊断，背景回退纯色、IBL 无贡献；旧场景缺少照明字段时默认关闭。
  支持 2:1 Radiance `.hdr`（宽度 4..8192，最大 256 MiB）；损坏或超出 float16 范围的像素会被拒绝。
  首次使用与重载在后台准备，失败保留旧版；缓存可删除重建。尺寸、工作集预算、预计算与 GPU 发布见[环境资产准备](docs/architecture/overview.md#环境资产准备)。
  暂无 EXR、六面图片、局部反射探针、环境遮蔽或动态 GI。
- Hierarchy 空白处／Scene 右键创建根实体，实体右键重命名、创建子实体、删除或 Duplicate 整棵子树；
  名称在右键弹窗中修改，确认后记录一次撤销；Inspector 不再显示名称输入框。
  选中实体后按 Ctrl+C／macOS Cmd+C 保存子树快照；切换场景后按 Ctrl+V／Cmd+V 粘贴为根实体，
  或在另一实体上右键 Paste 粘贴为子实体；
  每次粘贴生成新实体身份并作为一次场景撤销。剪贴板仅在当前编辑器进程内有效，不使用系统剪贴板。
  选中实体后按 macOS Cmd+Backspace／其他平台 Ctrl+Backspace 可删除整棵子树，支持 Undo；文本输入时不会触发删除快捷键。
  拖动实体修改父级，保留本地 Transform，因此世界位置可能改变。结构操作支持撤销，仅在 Edit 开放。
- 编辑器快捷键默认值内置于代码；可在 Edit → 快捷键设置中修改，保存后立即生效。
  用户覆盖仅保存不同于默认值的动作，写入用户状态目录的 `shortcuts.json`，不修改项目配置或仓库文件。
  Shader 文件变化后默认等待 200 ms 静默期以合并连续写入；这是编辑器内部策略，不属于项目或用户设置。
  Undo/Redo 默认 Ctrl+Z／Ctrl+Y，macOS 为 Cmd+Z／Cmd+Shift+Z，文本编辑时不抢占控件的撤销。
  `Primary` 代表 Cmd／Ctrl，`[]` 禁用绑定；冲突会记录日志并回退默认配置。
- Project 自动监视资产变化；右键 Refresh 重扫，Reimport 强制重建 Mesh 缓存。
  文件树同时显示已索引资产和未索引普通文件，后者只供浏览，不参与资产选择、拖放和改名删除；`.meta` 等辅助文件隐藏。
  顶部搜索框按文件名或目录名筛选树，清空后恢复全部显示；筛选不改变当前选中项或资产索引。
  资产变化在后台复核，主线程发布；Refresh 仍同步重扫。macOS 使用目录通知，其他平台暂用 500 ms 轮询；
  监视及过期候选规则见[Owner 结构](docs/architecture/overview.md#owner-结构)。
  拖动资产到目录可移动，右键 Rename 改名；右键 Delete 或选中资产后按 Cmd/Ctrl+Backspace，均经确认后把资产及 `.meta` 成对送入系统回收站；
  同目录改名支持仅修改名称大小写，资产与 `.meta` 保留身份；扫描失败恢复原文件名，不覆盖已有目标文件。
  在编辑器内移动场景会同步保存路径、项目启动场景和 Session；配置保存失败会补偿回滚，不改变场景内容或 Undo。
  当前打开的场景与启动场景不能直接删除，需先切换；外部 Finder 移动不自动改写项目配置。
  已被其他索引资产引用的文件不能删除，场景引用不会自动清空，资产删除不能通过编辑器 Undo 撤销。
  系统回收站调用失败时删除回滚；失败回滚依赖同卷硬链接，不支持时拒绝删除。
  从系统回收站恢复时需同时放回源文件与 `.meta`，然后 Refresh。`.comet/pending-deletions/`
  仅用于事务暂存，异常中断后若有残留需人工检查。暂不移动整目录。
  Inspector 的纹理设置按变化提交，材质参数在手势结束后保存；日志统一进入 Log，材质资产历史与场景历史分开。
- Project 目录或空白处右键 New Material，填写名称并选择 `pbr`／`unlit_color`，创建后自动选中。
  `.mat` 与稳定身份 `.meta` 成对创建，不覆盖同名文件；普通失败回滚本次创建，不保证进程崩溃时的双文件原子性。
- Project 目录或空白处右键 New Script，填写名称后创建组件 `.lua` 和稳定身份 `.meta`，并自动选中。
  默认脚本只包含 `update` 方法，可按需增加 `properties`、`on_start`、`fixed_update`、`on_stop`；脚本分配给实体后在 Play／app 中执行。
- New Lua Module 创建返回空 table 的 `.module.lua`，不生成 `.meta`，供组件脚本 `require`。
  两种创建共用不覆盖／失败回滚流程；模块创建成功会更新依赖扫描，缺失该模块的组件可重新加载。
- Finder／系统文件管理器可将 PNG/JPEG、HDR 环境图、glTF/GLB 拖入 Project，复制到落点目录。
  glTF 连同相对 buffer／图片复制，新建身份、不移动源文件、不沿用外部 .meta、不覆盖同名目标。
  暂不接收整目录、独立 .bin、网络或含 `..` 的依赖；整批失败回滚。复制和校验在后台准备，
  单批源文件（含 glTF 依赖）合计默认上限 512 MiB，可在当前 Profile 的 `assets.source_max_mib` 调整；重复输入只计一次，编辑器主线程发布并更新索引。
  此上限约束暂存输入量，纹理校验另受解码工作集上限约束，都不是进程内存上限；大批量索引发布仍可能造成短时卡顿，暂不提供导入进度和取消。
- Mesh 自动后台生成 Artifact；未加载模型只生成缓存，不创建 GPU 对象。删除缓存后用 Refresh 或重启补建。
  Edit 中将 Project Mesh 拖到视口，在相机关注平面创建实体并记录一次撤销；首次导入未完成时需等待后重试。
  新实体材质暂留空，需在 Inspector 指定后才绘制；不导入 glTF 材质。
- Inspector 引用框支持按类型过滤的资产路径下拉框；Edit 还可从 Project 拖入 Mesh／Material／Texture。
  底层仍保存 Handle，加载失败保持旧引用，丢失引用显示 Missing。Play 仅支持下拉调试，不接受资产拖放。
  内置模板为 `unlit_color`（color、intensity）和 `pbr`（base_color、base_color_texture、metallic、roughness）。
  默认立方体使用带纹理的 `cube.mat`，地面使用纯色 `ground.mat`，两者都是使用 `pbr` 模板的项目材质。
  旁边的 Project Shader Cube 使用 `stripes.mat` 和项目 `stripes.shader`，演示独立 `.vert/.frag` 如何覆盖 `unlit_color` 模板。
  材质资产按需通过 New Material 创建；`unlit_color` 适用于不受场景光源影响的颜色标记。
  PBR 基础颜色为线性颜色参数乘纹理采样值；基础颜色图片通常按 sRGB 导入，由 GPU 解码，不在 Shader 重复 gamma 转换。
  `base_color_texture` 可选，选择 None 恢复纯色；指定但失效的纹理引用仍视为错误，不静默使用默认纹理。
  PBR 当前支持直接光照、方向光阴影与全局 IBL，不含法线／金属粗糙度贴图或透明；金属度范围 0..1，粗糙度范围 0.045..1。
  Inspector 按共享布局显示纹理、标量和颜色参数。拖动参数时实时预览，松手保存一次；Esc 恢复手势前版本，不写文件。
  双击输入数值，回车或失焦结束编辑；仅查看默认值或没有实际变化不会写文件。
  Edit 中选中材质后，Undo／Redo 操作该材质的独立历史；一次拖动对应一次撤销，切换场景不会清空材质历史。
  Render Template 下拉框可切换已发布模板，确认时列出不兼容参数；保留兼容值、新参数使用默认值，材质身份不变。
  编辑时先准备依赖与 GPU 绑定。参数预览成功后才替换运行版本，保存失败保留草稿，可“重试保存”或“取消修改”；
  切换资产、隐藏 Inspector 或进入 Play 会结束当前手势。模板／纹理的离散编辑失败恢复原值，旧在途帧继续使用旧资源。
  必填纹理槽需补齐后才发布，切换其他资产会丢弃未完成草稿；项目 Shader 的材质属性可独立声明，非材质接口仍须兼容所选模板。
- View 菜单与面板关闭按钮共享显隐状态；菜单只展示已接通的操作。

## Shader 开发

项目示例位于 `demo/assets/shaders/stripes.vert`、`stripes.frag` 和 `stripes.shader`，材质 `demo/assets/materials/stripes.mat`
通过稳定 Handle 引用程序。`stripes.shader` 为 `frequency` 等属性提供默认值和编辑范围；启动编辑器打开默认场景，可看到右侧条纹立方体，在 Inspector 修改频率或编辑 `stripes.frag` 后可观察变化。
项目 Shader 不进入引擎的 CMake 内嵌程序列表；编辑器与 `comet_prepare_project` 共用源编译逻辑，app 只加载产物。

以下目录相对 `engine/shaders/`。

### 目录与职责

| 目录 | 内容 |
| --- | --- |
| `material/` | 网格顶点入口与材质片元着色 |
| `common/` | 共用网格／全屏三角形顶点实现与 `frame.glsl` 相机帧布局 |
| `lighting/forward.glsl` | 前向光源布局、方向与衰减计算 |
| `shadow/` | 方向光深度生成，与材质前向采样分开 |
| `environment/` | `skybox.vert` + `skybox.frag`，仅绘制场景背景 |
| `debug/` | 调试线绘制 |
| `post/` | Bloom 高亮提取／模糊与最终合成、曝光、色调映射、SDR/HDR 编码 |

### 材质与阶段配对

- `unlit_color`：`unlit_color.vert` + `unlit_color.frag`，直接输出颜色和强度。
- `pbr`：`pbr.vert` + `pbr.frag`，金属度／粗糙度 PBR，使用 `lighting/forward.glsl` 的光源衰减和阴影采样。
- 调试线与显示输出分别使用 `debug/line.vert/.frag`、`post/display.vert/.frag`。
- Bloom 使用 `post/bloom.vert/.frag`，与 display 共用 `common/fullscreen.glsl` 顶点实现。
- 阴影使用 `shadow/directional.vert/.frag`；片元阶段无颜色输出，只写深度。

`lit` 表示受光，`unlit` 表示不受光，和 HDR/SDR 输出模式无关。
材质模板名属于资产持久化协议；文件名描述当前算法，二者不要求同名。
网格入口包含同一份顶点实现；`COMET_MESH_LIGHTING` 只为受光版本启用世界位置、
逆转置法线计算与 UV 输出；不受光版本只计算顶点位置，不携带无用的法线和 UV 输出。
全屏顶点与调试线的输入协议不同，保持独立。

外部旧材质迁移：`lit_color` 改用 `pbr`，`albedo` 改为 `base_color`，设置 metallic=0、roughness=1，
可得到粗糙非金属表面，但不与 Lambert 像素等价。旧 `unlit_texture_blend` 不再注册，双纹理混合没有直接等价的 PBR 参数。

### 修改与验证

完整程序以同目录、同名 `.vert/.frag` 表示；新增程序需加入 `engine/shaders/CMakeLists.txt` 显式配对列表。
当前生成文件使用阶段文件名，须保持全局唯一。
公共 `.glsl` 通过相对路径包含，构建依赖与编辑器热重载均跟踪实际 include。
Project 中右键项目的 `.vert`、`.frag`、`.comp`、`.geom` 阶段源码、`.glsl` 公共文件或
`.shader` 程序描述文件，选择“打开源码”即可用外部编辑器修改；macOS 同样优先使用 VS Code。
正在使用的项目材质程序及其依赖保存后沿用既有的 Shader 热重载流程；
仅打开文件不会触发编译，语法错误也不妨碍打开修复。
编辑器热重载已登记的材质程序及其公共 include；调试线、阴影、天空盒与显示输出修改需重新构建。
macOS 的内置 Shader 热重载由目录通知唤醒，其他平台暂用 500 ms 输入复核；真正编译前仍校验输入快照。
固定接口采用 Frame set 0、Material set 1 和 Object push constant；新增或修改布局需同步 C++／GLSL 与契约测试。
程序定义、阶段完整性、反射与发布边界见[材质、Shader 与 Pipeline](docs/architecture/overview.md#材质shader-与-pipeline)。

运行 `cmake --build --preset dev-debug --parallel` 和 `ctest --preset dev-debug` 验证。

## 架构入口

| 文档 | 内容 |
| --- | --- |
| [架构与所有权](docs/architecture/overview.md) | 模块依赖、类入口、运行时与 Lua、帧时序、GPU 生命周期和 Shader 发布 |
| [路线图](docs/engine-roadmap.md) | 优化计划、阶段与验收条件 |

C++ 遵循根目录 `.clang-format`（100 列），只格式化相关代码，不处理 Shader 和第三方源码。
测试按所属模块放在 `tests/`，公共辅助工具放在 `tests/support/`。
编辑器的纯 CPU 测试位于 `tests/editor/core/`，面板测试位于 `tests/editor/ui/`，无 UI 的图形工作流测试位于 `tests/editor/integration/`。
新增测试须在 `tests/CMakeLists.txt` 明确归入 CPU、集成或独立进程组；配置时检查遗漏和重复，不根据目录自动猜测。
`module_boundaries` 检查内部模块的传递 include 与 CPU 资产／编辑器功能的直接边界；`module_dependency_contract` 验证禁止依赖确实被拦截。
头文件应能独立编译，实现文件直接包含自己使用的类型，不依赖入口头的传递包含。
