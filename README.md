# Comet 引擎

Comet 是使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与 ImGui 编辑器，目前重点是场景编辑、资产导入和单视口交互。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：runtime、core、scene、asset、render、graphics、config、diagnostics |
| `engine/shaders/` | 生产 Shader，按 material、lighting、shadow、environment、debug、post、common 分目录；仅编译 CMake 显式列表 |
| `tools/shader/` | 共用 CPU Shader 编译库与构建 CLI，不链接 engine 运行时 |
| `tools/render_benchmark/` | 固定场景渲染性能基准，链接 engine，不依赖测试框架或编辑器 |
| `editor/` | 编辑器入口，`src/` 按 scene、viewport、assets、inspector、ui 组织，`resources/` 保存私有字体等资源 |
| `app/` | Runtime 示例入口及 `resources/` 私有图标 |
| `demo/` | 随仓库提供的完整示例项目，与引擎／编辑器源码分开 |
| `demo/assets/` | 示例场景、源资产及相邻 `.meta`；可选大资源由脚本下载，不进入版本控制 |
| `demo/assets/scripts/` | Lua 项目行为；默认字段由脚本声明，实体仅保存覆盖值 |
| `demo/project.json` | 示例项目描述：版本、名称和启动场景 |
| `config/` | `common.yaml` 与各 Profile 配置 |
| `demo/.comet/` | 示例项目本机缓存、日志与编辑器状态，不进入版本控制 |
| `tests/`、`3rdparty/` | GoogleTest 测试与第三方依赖 |

`runtime/` 管应用入口与宿主生命周期；`scene/` 管 ECS 数据、组件元信息与调度，System 接口及具体行为集中在 `scene/systems/`。
`asset/data/` 保存 Mesh、Texture、Material 的 CPU 数据。
`render/material/` 聚合材质定义、准备缓存与绘制，`render/debug/` 聚合辅助线，`render/passes/` 保存具体渲染步骤。
`RenderResources` 组织 Mesh/Texture 创建、上传和 Sampler 复用；资产身份缓存仍只由 `AssetRegistry` 管理。
编辑器的 `ProjectPanel` 位于 `assets/project_panel.*`，`ViewportPanel` 位于 `viewport/viewport_panel.*`，
InspectorPanel 分发选择并编辑场景属性，AssetInspector 独立持有材质／纹理草稿；面板不直接保存文件或发布 GPU 资源。

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

可选的 HDR 天空背景不进入 Git/LFS；需要时显式下载（依赖 `curl` 和 `sha256sum` 或 `shasum`）：

```bash
./tools/download_assets.sh
```

脚本按 1K、2K、4K、8K 顺序下载 Small Hangar 01 的四个版本，总计约 130.7 MiB，默认场景引用 4K。
每个版本有独立的 `.meta`，可在 Environment 的 HDR map 中切换；4K 转成单面 1024 的 cubemap，
8K 转成单面 2048，包含 mip 的纹理约占 256 MiB，解码时还需要额外 CPU 内存。16K 超出导入尺寸限制，不下载。
脚本可从任意工作目录运行，逐文件校验 SHA-256，跳过已校验文件；下载失败不会覆盖现有资源。
未下载时 demo 保留环境资产引用并提示缺失，背景回退为纯色；下载后重新打开项目即可。
构建和启动不会自动联网。普通测试使用小型本地数据，不自动导入下载的 HDR。
真实 HDR 验证需显式启用：`cmake --preset dev-debug -DCOMET_TEST_DOWNLOADED_ASSETS=ON`，
再运行 `ctest --preset dev-debug -L assets`；缺文件时跳过。设为 `OFF` 可恢复默认测试范围。
环境首次使用和重载都在后台准备；未驻留时使用纯色背景，不视为加载失败，重载期间保留旧有效资源。
真正的缺失或导入失败由资产加载层诊断，渲染解析只报告已发布对象的类型错误。
缓存位于项目 `.comet/cache/imported/environment/`，可删除重建；输入内容或导入算法版本变化后自动失效。
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
编辑器源码由 `editor_core`（不依赖 ImGui）和 `editor_ui` 两个内部库管理，入口与测试共同链接。
物理目录按功能聚合，编译目标按依赖划分；例如 `scene/scene_document` 属于 core，`scene/hierarchy` 属于 ui。
仅启用 tests 时仍构建 editor_core，不构建 UI；新增编辑器源码只需维护所属库的清单。
`tests/support/` 提供测试专用的 ImGui Context、临时目录与 Worker 同步辅助，不进入引擎。
测试分为 `unit_testing`（CPU 逻辑）和 `integration_testing`（图形／UI／运行时）；
按实际设备依赖分组，纯 CPU 的图编译／光照计算／拾取数学仍属 unit；同一 GPU 用例只在普通集成或同步验证入口执行一次。
`ctest --preset dev-debug -L unit` 可快速检查逻辑，完整 `ctest --preset dev-debug` 仍包含 GPU 生命周期、同步和 WSI 回归。
`COMET_NATIVE_OPTIMIZATION` 只适合本机构建。配置与诊断采用“编译期能力 + Profile 运行时策略”。

### 可复现渲染测量

`render_benchmark` 是固定场景的渲染性能基准程序，用于比较引擎改动前后的性能。
它自行创建场景、采样并退出，不监控其他进程，也不加载任意项目；app/editor 的实时观察使用下方渲染诊断。
一键构建并测量，复用 `build-release/` 的 Release 引擎，不创建另一套专用构建目录：

```bash
./tools/benchmark.sh
./tools/benchmark.sh /tmp/comet-benchmark.csv 64 640 360 240 1
./tools/benchmark.sh --help
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
macOS 可在末尾追加 `-NSAutomaticWindowAnimationsEnabled NO` 关闭该进程的窗口动画；报告以实际 framebuffer 像素为准。
固定场景使用共享 PBR 材质、立方体网格与地面、三类光源、方向光阴影、4×MSAA 和 SDR 输出；IBL 关闭，
不依赖可选 HDR 下载。资产复制到临时目录后走生产扫描／导入／加载，结束清理，不修改 demo 的资源和缓存。
预热 32 帧后输出 CPU 整帧及分段、CPU/GPU 图和各 pass 的样本数、P50/P95，以及设备、呈现模式和 VMA 分配量。
GPU 样本按提交序号去重；`gpu_status` 区分完整、部分、不支持和降级，不把缺样本写成零耗时。
窗口／呈现变化、少画物体或提前退出会拒绝报告；成功报告原子替换指定文件。

测量请求关闭 validation，外部强制 layer 仍需自行排除；应在同机 Release、设备保持唤醒、无并行构建或其他 GPU 测试时重复比较。
可分别提高物体数、提高实际分辨率、关闭 Bloom，避免一次改变所有变量。物体数增加时会缩小立方体，
它不是纯 CPU 实验，也不代表多材质、透明物体、IBL 或编辑器开销。CPU 墙钟包含等待，GPU 图不含呈现完成，
VMA 分配量不等于系统总显存；各分段百分位不能直接相加。CI smoke 只验证测量契约，不设置绝对耗时门槛。

### 交互式渲染诊断

`diagnostics.enable_render_diagnostics` 独立于 scope Profiler 的编译开关；开发 Profile 默认开启，
`app-release` 默认关闭。编辑器默认显示「渲染统计」面板，也可通过「视图 / View」菜单显示／隐藏。
配置只决定启动时是否采样；「采集数据」在运行时的帧边界切换采样，隐藏面板不会停止采样，切换结果不写回配置。
面板区分包含等待的 CPU 整帧墙钟时间、场景图 CPU 录制和已完成帧的 GPU 时间；GPU 不包含 UI 绘制与呈现完成，
CPU/GPU 分别统计，不保证来自同一帧。不支持 GPU 时间戳时仍可观察 CPU。
面板每 250 毫秒刷新，显示近 1 秒均值／峰值及近 5 秒趋势；底层仍逐帧采集，峰值不会因 UI 降频而丢失。
隐藏 Viewport 会跳过离屏场景绘制，Runtime、UI 和资源维护继续；诊断面板标明未绘制场景，不将历史图耗时当成新样本。
「暂停显示」只冻结面板，不停止采集；CPU 阶段、渲染阶段和显存堆明细可展开。日常观察无需保存报告。
显存预算至多每秒采样一次，并标明驱动报告或 VMA 估算。「保存显存分配报告」手动生成详细报告，
原子保存到项目 `.comet/editor/diagnostics/gpu-allocations.json`，再次保存替换旧报告，结果写入 Log。

普通文件日志与 Scope Profiler 日志统一保存到**当前项目**的 `.comet/logs/`，
分别命名为 `comet_<时间戳>.log`、`profiler_<时间戳>.log`；app/editor 使用同一目录规则。
例如默认 demo 的路径是 `demo/.comet/logs/`，打开外部项目则写到外部项目内，不依赖仓库根目录或工作目录。
各 Profile 默认 `diagnostics.enable_file_logging: false`；在 `config/profiles/<Profile>.yaml` 中改为 `true`
后才创建目录与文件。Profiler 文件还需当前构建支持且启用 `diagnostics.enable_profiler`。
路径由启动入口传入，不作为 YAML 中的机器路径配置。无日志路径时仅保留终端／自定义输出端，
目录无法写入时向标准错误提示并保留这些输出，不回退写到其他目录；项目／配置加载前的失败仍输出到终端。
旧仓库根 `logs/` 不自动搬迁或删除。

编辑器使用 16px Roboto Bold，并合并 Noto Sans SC Bold 覆盖中文。
顶栏「语言 / Language」可切换简体中文和 English，默认中文，本次会话有效。
切换只影响编辑器内置显示文本，保留控件身份及布局；资产名、路径、Shader 标识和原始日志不翻译。
中文词表位于 `editor/resources/locales/zh-CN.yaml`，启动时加载一次；修改文案后重启即可，无需重新编译。
缺词回退英文原文，文件无效时记录日志并使用英文；键和值必须是字符串，格式占位符须与英文原文完全一致。

启动时的显示输出在 `config/common.yaml` 的 `render` 下设置，也可由当前 Profile 覆盖：

```yaml
render:
  output_mode: sdr  # sdr / hdr / auto，修改后重启
  hdr_headroom: 4  # HDR 峰值相对于 SDR 白色的倍数，范围 1..16
```

`sdr` 强制普通输出；`hdr` / `auto` 在驱动提供 RGBA16F + 扩展线性 sRGB 时使用该组合，否则回退配置的 SDR 格式并记录原因。
日志区分请求模式与实际模式。`auto` 检测的是 Vulkan 输出支持，不是显示器实测亮度，也不会切换系统 HDR 设置。
macOS 由 MoltenVK 配置 EDR layer；实际高亮受屏幕与系统亮度限制。编辑器启动策略暂时强制 SDR，避免 UI 和视口混用编码。
HDR 使用相对白色的线性输出，不承诺固定 nits；暂不支持 HDR10/PQ、运行时切换、跨屏模式适配或自动亮度校准。
SDR/HDR 指显示输出；内部场景目前始终使用浮点 HDR 目标与最终输出 Pass，关闭 Bloom 不会切换成 LDR 管线。

Bloom（泛光）和曝光属于场景内容，不在引擎 YAML 中配置。点击「层级 / Hierarchy」中的「场景 / Scene」，
在 Inspector 的「后处理 / Post Processing」修改：曝光 0..100、泛光开关、强度 0..10、阈值 0..65504。
拖动实时预览，松手记录一次撤销，Esc 取消；双击可输入数值。保存场景后写入 `.scene` 的 `post_process`，
Edit、Play 和独立 app 共用这份数据。Play 中该面板只读，运行时代码可修改 Runtime Scene，不回写 Edit 文档。
新场景默认关闭泛光；demo 场景显式开启强度 0.15、阈值 1。关闭开关保留调好的参数，强度为 0 也不执行泛光。

Bloom 在半分辨率提取高亮，横／纵模糊后在线性 HDR 中合成，再做曝光和 SDR/HDR 显示映射；不影响 ImGui。
开启时若 GPU 资源不足，保留上次可用效果并有限重试，错误写入日志；场景中的开关意图不回滚。
重试耗尽后可关闭再开启；设备丢失仍会退出，不属于可恢复的效果准备失败。
阈值在曝光前应用，只让超阈值亮区向邻域扩散，不提供环境照明，也不要求显示器支持 HDR。
代码通过 `Scene::set_post_process(PostProcessSettings)` 修改内容；渲染器消费场景快照，不另设全局覆盖入口。
纯参数变化不重建图或材质管线，是否执行泛光发生变化时才重编排。首次启用才创建资源，关闭后保留最近纹理供复用。
目前没有自动曝光、相机级覆盖或局部后处理区域。

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
app 与 editor 共用 Project、SceneSerializer 和场景资产引用，不再分别创建示例物体、相机或灯光。
app 使用场景 primary Camera；Edit 使用编辑器相机，因此同一场景不保证相同取景。
app 窗口创建时使用 `project.json` 的项目名，运行时显示 `项目名 | 120 FPS`；编辑器标题固定为 `Comet Editor`。
FPS 复用 editor 的平滑统计，每 0.5 秒采样一次。
该数值表示主循环帧率，不是 GPU 耗时；全屏隐藏标题栏时不可见。
app 启动时同步补齐所引用 Mesh 的 Artifact 并加载资源；指定场景或必需资源加载失败会终止启动，
不像 editor 那样保留缺失引用供修复。这仍是开发期运行入口，不是已打包的 Shipping Player。
示例立方体通过 Script 组件引用 `demo/assets/scripts/spin.lua`，`speed` 为每秒角度，`enabled` 控制是否旋转。
app 和 editor Play 共用该行为，不依赖 UUID 或项目路径；Edit 不执行旋转，Play 修改不保存回 Edit 场景。
项目 `.lua` 位于 assets，由 `.meta` 提供身份，也可从 Finder 导入；新增脚本无需改 CMake 或重编译宿主。
引擎私有链接 Lua 5.4.8；参数编辑与运行限制见下方“场景运行时”。
两种入口遇到项目描述错误或缺少 assets 都会启动失败，不回退仓库项目；仅 editor 在启动场景缺失／损坏时
记录错误并打开空场景，供用户修复，不覆盖原文件。
引擎 Profile、编辑器快捷键仍读取开发构建自带的 `config/`，字体／图标／Shader 不需要复制到每个项目。
当前支持启动时选择一个项目，尚不支持运行中切换项目、最近项目列表、项目创建向导或独立打包。

### 运行时输入

`Engine::get_input_frame()` 提供键盘、鼠标和标准手柄的只读帧快照，不依赖 ImGui。
Window 采集事件，Engine 在 Update 前发布一次；`down / pressed / released` 分别表示按住／刚按下／刚松开，
同一批事件内的快速按下再松开会同时保留两种边沿。轮询或等待事件不推进快照，最小化跳过 Update 时也不发布。
失焦释放按钮并清空位移／滚轮／轴；手柄首次连接、重连或恢复焦点时只建立按住状态，不伪造一次新的按下。
光标为窗口逻辑坐标，滚轮保留双轴偏移；手柄摇杆为 [-1,1]、Y 向下，扳机为 [0,1]，死区由消费者决定。

app 与 editor Play 共用可选的 `CameraControllerComponent`：在 Edit 中选中主相机，
通过 Inspector → Add Component → Camera Controller 添加，并配置启用、移动速度和转向灵敏度；保存进 `.scene`。
只控制实际渲染的主相机；未添加／未启用组件时不移动，多个 primary 时与渲染一致选择最小 EntityId。
仓库 demo 已默认添加；外部项目同样按组件启用，不依赖项目路径或硬编码相机 UUID。
右键拖动转向（本地俯仰限制 ±89°），WASD 沿相机朝向移动，Q/E 沿世界上下移动，左 Shift 加速，滚轮沿视线移动。
第一个标准手柄支持左摇杆移动和左右扳机升降；暂不锁定／隐藏光标，也没有碰撞或手柄转向。
app 中 Esc 退出；Play 中鼠标进入画面即可操作，无需点击激活；Esc 与 Stop 一样直接返回 Edit。
鼠标离开画面、失焦、弹窗或编辑文字时停止接收；回来后原先按住的按钮需要松开重按。
控制只改变运行状态，退出 Play 恢复 Edit 场景，不生成逐帧撤销记录。
Edit 相机和编辑器快捷键保持原有 ImGui 路径；动作绑定、重映射、文本／IME、鼠标锁定仍是后续事项。
Frame 可复制，但不是持久回放格式。

### 场景运行时

app 与 editor Play 共用 SceneRuntime：先固定更新，再普通更新，退出时逆序停止 System；Edit 不执行游戏行为。
默认固定步 1/60 秒，每帧最多补算 8 步；暂停仍允许 UI 和资源维护，单步只推进一次固定更新和普通更新。
Play 修改只作用于副本；脚本启动或运行失败会记录错误并恢复 Edit，不关闭编辑器。设备丢失等渲染故障仍退出。
独立 app 默认在运行错误时退出，不自动重试已部分执行的一帧。

Lua 的 `properties` 声明显式导出的 bool／float／Vec3／string 配置；只有编辑过的字段保存为实体覆盖。
Inspector 切换／清空 Script 引用会同时清空覆盖，一次 Undo 恢复旧脚本和参数；加载失败不改原绑定。
“恢复默认参数”清空覆盖，可撤销，不重新加载源码。Play 面板跟随活动实例的定义，不混用更新后的资产参数。
修改源码后重新 Play 使用新版；字段改名或类型变化会报告覆盖不匹配，可恢复默认参数后重新配置。
`self.parameters` 是只读配置；累计时间等内部状态放在 self 的其他字段，不显示或保存到场景。
目前每实体一个脚本，提供本实体变换和已授权 A-Z 按键查询；不支持 require、材质 API 或运行中源码替换。
Lua 有内存与指令预算，但不是面向不可信代码的安全沙箱。调用、寿命和失败边界见[架构说明](docs/architecture/overview.md#lua-脚本与参数)。

## 编辑器使用

- File → Open/Save 操作当前项目 assets 内的 `.scene`，拒绝越界路径。启动打开 project.json 指定的场景，
  不恢复上次打开的其他文档；坏资源引用保留并记录 Log，后台导入完成后自动重试加载。
- Edit 使用独立相机；Play 运行场景副本及其 primary Camera，Stop 不回写运行时修改。
  2D/3D 只切换 Edit 投影；Play 分辨率可选 Free、16:9、HD、FHD，Fit 等比适应，1x 原尺寸裁切。
  Play 工具栏的 `||` 暂停、`>` 继续、`|>` 单步；单步仅在暂停时可用，控制请求在下一次宿主更新执行。
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
  新增照明字段缺省关闭，保持旧场景外观；缺失引用保留并诊断，背景回退到 clear color、IBL 无贡献，不替换成另一张环境图。
  支持 2:1 Radiance `.hdr`（宽度 4..8192，最大 256 MiB），线性解码到 RGBA16F cubemap 与背景 mip 链，单面最高 2048²。
  后台导入同时生成漫反射 cubemap（最高 16²）、GGX 镜面预滤波（最高 128²，各 mip 对应粗糙度）与 128² BRDF LUT；
  它们和背景统一缓存于 `.comet/cache/imported/environment/`，算法版本变更后自动重建，不提交 Git。
  拒绝损坏文件和超出 float16 范围的像素；首次准备与重载均在后台读取缓存／预计算，主线程整组发布 GPU 资源，失败保留旧资源。
  环境 CPU 准备按预估工作集共享 2 GiB 预约预算；这不是进程总内存上限。外部文件复制、普通纹理首次加载与 GPU 创建仍同步。
  此阶段未提供 EXR、六面图片导入、局部反射探针、环境遮蔽或动态 GI；全局 IBL 不读取方向光阴影图。
- Hierarchy 空白处／Scene 右键创建根实体，实体右键创建子实体、删除或 Duplicate 整棵子树；
  拖动实体修改父级，保留本地 Transform，因此世界位置可能改变。结构操作支持撤销，仅在 Edit 开放。
- 编辑器快捷键位于 `config/profiles/editor-dev.yaml` 的 `editor.shortcuts`，修改后重启。
  Undo/Redo 默认 Ctrl+Z／Ctrl+Y，macOS 为 Cmd+Z／Cmd+Shift+Z，文本编辑时不抢占控件的撤销。
  `Primary` 代表 Cmd／Ctrl，`[]` 禁用绑定；冲突会记录日志并回退默认配置。
- Project 自动监视资产变化；右键 Refresh 重扫，Reimport 强制重建 Mesh 缓存。
  拖动资产到目录可移动，右键 Rename 改名；源文件与 .meta 成对操作，不覆盖冲突文件，暂不移动整目录。
  Inspector 的材质／纹理设置按变化提交，日志统一进入 Log；资产文件修改暂不纳入场景撤销。
- Project 目录或空白处右键 New Material，填写名称并选择 `pbr`／`unlit_color`，创建后自动选中。
  `.mat` 与稳定身份 `.meta` 成对创建，不覆盖同名文件；普通失败回滚本次创建，不保证进程崩溃时的双文件原子性。
- Finder／系统文件管理器可将 PNG/JPEG、HDR 环境图、glTF/GLB 拖入 Project，复制到落点目录。
  glTF 连同相对 buffer／图片复制，新建身份、不移动源文件、不沿用外部 .meta、不覆盖同名目标。
  暂不接收整目录、独立 .bin、网络或含 `..` 的依赖；整批失败回滚，复制大文件仍可能阻塞 UI。
- Mesh 自动后台生成 Artifact；未加载模型只生成缓存，不创建 GPU 对象。删除缓存后用 Refresh 或重启补建。
  Edit 中将 Project Mesh 拖到视口，在相机关注平面创建实体并记录一次撤销；首次导入未完成时需等待后重试。
  新实体材质暂留空，需在 Inspector 指定后才绘制；不导入 glTF 材质。
- Inspector 引用框支持按类型过滤的资产路径下拉框；Edit 还可从 Project 拖入 Mesh／Material／Texture。
  底层仍保存 Handle，加载失败保持旧引用，丢失引用显示 Missing。Play 仅支持下拉调试，不接受资产拖放。
  内置模板为 `unlit_color`（color、intensity）和 `pbr`（base_color、base_color_texture、metallic、roughness）。
  默认立方体使用带纹理的 `cube.mat`，地面使用纯色 `ground.mat`，两者都是使用 `pbr` 模板的项目材质。
  材质资产按需通过 New Material 创建；`unlit_color` 适用于不受场景光源影响的颜色标记。
  PBR 基础颜色为线性颜色参数乘纹理采样值；基础颜色图片通常按 sRGB 导入，由 GPU 解码，不在 Shader 重复 gamma 转换。
  `base_color_texture` 可选，选择 None 恢复纯色；指定但失效的纹理引用仍视为错误，不静默使用默认纹理。
  PBR 当前支持直接光照、方向光阴影与全局 IBL，不含法线／金属粗糙度贴图或透明；金属度范围 0..1，粗糙度范围 0.045..1。
  Inspector 按共享布局显示纹理、标量和颜色参数，参数变化后自动保存；仅查看默认值不会写文件。
  Template 下拉框可切换已发布模板，确认时列出不兼容参数；保留兼容值、新参数使用默认值，材质身份不变。
  编辑时先准备依赖与 GPU 绑定，再保存并发布；失败恢复面板原值，旧在途帧继续使用旧资源。
  必填纹理槽需补齐后才发布，切换其他资产会丢弃未完成草稿；尚不支持动态指定项目 Shader。
- View 菜单与面板关闭按钮共享显隐状态；菜单只展示已接通的操作。

## Shader 开发

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
编辑器热重载已登记的材质程序及其公共 include；调试线、阴影、天空盒与显示输出修改需重新构建。
`MaterialShaders` 按程序名持有顶点/片元字节码，允许提交任意完整程序对；
缺失单个阶段会拒绝整个候选批次，未提交的程序保留原版本，目标重建仍沿用成功发布的版本。
程序定义、默认字节码、固定契约校验和覆盖合并位于 `engine/src/render/material/material_shader.h/.cpp`。
编辑器和 MaterialRenderer 共用这份程序定义；未知程序名或显式空程序同样被拒绝。
Frame 位于 set 0，材质位于 set 1，Object 使用 push constant；修改布局须同步 C++ 和契约测试。
Frame binding 0 保存 160 字节相机数据，PBR 区分透视的位置差与正交的统一观察方向；
binding 1 保存 LightingData（含光源矩阵与阴影参数），binding 2 是按帧槽位绑定的阴影图。
光照 UBO 的 C++／GLSL 使用对应的具名字段；修改字段时须保持 std140 偏移、数组步长与反射契约一致。
`forward.glsl` 使用 nearest sampler 手工 3×3 PCF；正高度阴影视口与投影 UV 一致。

运行 `cmake --build --preset dev-debug --parallel` 和 `ctest --preset dev-debug` 验证。
旧学习头文件与示例已移除，需要参考时可查 Git 历史。

## 架构入口

- **运行时**：`runtime/application` 管初始化与关闭；Engine 管主循环，组合 Scene、SceneRuntime、任务和渲染服务。
  `scene/scene_runtime` 按顺序执行 `scene/systems/system.h` 的固定／普通更新，拥有启动与逆序停止边界；Engine 负责与活动 Scene 绑定。
  生命周期用 Result 传递预期失败，入口报告错误并设置退出码。
- **场景**：Scene 保存组件、UUID 与 AssetHandle；世界矩阵按 TRS 和父级版本更新。
  编辑器在帧准备前执行文件与资产请求，UI/Gizmo 与 System 修改后再提取当帧场景。
- **渲染**：`Scene → SceneExtractor → SceneResolver → SceneRenderer`。
  Renderer 组合帧调度与呈现，SceneRenderer 编排 ShadowPass → RGBA16F 场景 → OutputPass；
  RenderGraph 负责 pass 间同步，FrameSlot 保留在途资源，Presentation 处理交换链恢复。
  MaterialShader 模块定义程序、字节码与固定接口契约，MaterialRenderer 管理 GPU 候选、材质版本发布和绘制。
  Material 保存实例参数，MaterialLayout 独立描述布局；属性描述位于 `scene/property`，不依赖 ECS 注册器。
- **资产**：AssetDatabase 管身份与依赖，ImportService 管导入，AssetManager 管加载与发布。
  AssetRegistry 是唯一 Handle 缓存，RenderResources 只创建设备资源；Worker 不操作 Scene 或 GPU。
  Mesh 加载已发布 Artifact，Texture 暂时直接解码源文件。
- **编辑器**：Editor 装配服务，SceneDocument 管文档与保存点，CommandHistory 管撤销。
  面板产生请求，由统一更新阶段交给 SceneEditor 校验和执行；Viewport 管相机、拾取和 Gizmo，不持有 Engine。
  Inspector 的材质读取交给 EditorAssets，默认值／模板迁移／草稿校验集中在 material_editing。
  简单确认弹窗集中在 `editor/src/ui/dialogs`，只返回选择；有路径和请求状态的 SceneFileDialog 独立保留。
- **Shader**：编译工具独立于 engine。开发编辑器支持内置材质程序后台编译和候选发布，
  失败保留旧画面；辅助线、阴影、天空盒与输出 Shader 修改仍需重新构建。项目 Shader 和复杂接口尚未接入。
- **坐标**：世界 +Y 向上，Vulkan Viewport 负高度转换画面坐标；`flip_y` 仅影响纹理导入。

实现契约与扩展计划分别维护，避免在 README 重复细节：

| 文档 | 内容 |
| --- | --- |
| [架构与所有权](docs/architecture/overview.md) | 运行时与 Lua 边界、帧时序、GPU 生命周期和 Shader 发布 |
| [路线图](docs/engine-roadmap.md) | 阶段、剩余工作与验收条件 |

C++ 遵循根目录 `.clang-format`（100 列），只格式化相关代码，不处理 Shader 和第三方源码。
测试按所属模块放在 `tests/`，公共辅助工具放在 `tests/support/`。
编辑器的纯 CPU 测试位于 `tests/editor/core/`，面板测试位于 `tests/editor/ui/`，无 UI 的图形工作流测试位于 `tests/editor/integration/`。
新增测试须在 `tests/CMakeLists.txt` 明确归入 CPU、集成或独立进程组；配置时检查遗漏和重复，不根据目录自动猜测。
头文件应能独立编译，实现文件直接包含自己使用的类型，不依赖入口头的传递包含。
