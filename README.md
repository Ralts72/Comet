# Comet 引擎

Comet 是使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与 ImGui 编辑器，目前重点是场景编辑、资产导入和单视口交互。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：core、scene、asset、render、graphics、config、diagnostics |
| `engine/shaders/` | 引擎 Shader；只编译 CMake 显式列表，其余源码保留供学习 |
| `editor/` | 编辑器入口，`src/` 按 scene、viewport、assets、inspector、ui 组织，`resources/` 保存私有字体等资源 |
| `app/` | Runtime 示例入口及 `resources/` 私有图标 |
| `demo/` | 随仓库提供的完整示例项目，与引擎／编辑器源码分开 |
| `demo/assets/` | 示例项目场景、源资产及相邻 `.meta`，进入版本控制 |
| `demo/project.json` | 示例项目描述：版本、名称和启动场景 |
| `config/` | `common.yaml` 与各 Profile 配置 |
| `demo/.comet/` | 示例项目本机缓存与编辑器布局，不进入版本控制 |
| `tests/`、`3rdparty/` | GoogleTest 测试与第三方依赖 |

## 构建与运行

需要 CMake 3.31+、C++20 编译器、Vulkan SDK（含 `glslangValidator`）、Git LFS 和 Submodule。

```bash
git lfs install
git lfs pull
git submodule update --init --recursive
cmake --preset dev-debug
cmake --build --preset dev-debug --parallel
ctest --preset dev-debug
```

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
队列满时明确拒绝新请求，可通过 Reimport 重试。已加载资源在刷新失败时继续保留。
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
  Play 仅实时调试已有属性，不记录 Edit 历史；New/Open 成功和 Edit/Play 切换会清空历史。
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
  数值参数目前通过 `.mat` JSON 配置，Inspector 暂只编辑纹理；尚不支持动态指定项目 Shader。
- View 菜单与面板关闭按钮共享显隐状态；菜单只展示已接通的操作。

## 架构入口

- 启动：app/editor 共用 `RUN_APP` 和 `Comet::launch`，统一参数传递、`--help`、错误退出和 Application 所有权。
  各入口显式提供创建函数，负责自己的参数校验和依赖准备；`Editor` 只接收已加载的 `Project`，不解析命令行。
  `Comet::run` 读取配置，再由 `Application::run` 统一驱动初始化、更新和关闭；异常在生命周期边界处理，具体契约见资源所有权文档。
- 渲染：`Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
  帧准备与 UI 修改完成后才提取 Scene；Scene 只保存组件和资产 Handle，GPU 生命周期由渲染层管理。
  SceneResolver 只解析 Mesh/Material 引用；SceneRenderer 编排目标与 pass，MaterialRenderer 准备并绘制材质队列。
  MaterialRuntimeCache 按材质版本和不可变布局准备纹理与参数快照；同一布局驱动 descriptor 和参数打包。
  相机 FrameSet 按 slot 更新，MaterialSet 按材质版本跨 slot 复用，物体矩阵使用 push constant；在途版本由 FrameSlot 保活。
- 窗口：Window 管 GLFW 初始化与最后一个窗口释放后的终止；上层通过窗口接口请求关闭、查询最小化状态。
  GLFW 是 engine 的私有依赖，原生句柄仅供 Vulkan／ImGui 后端及底层测试对接，不用于普通业务操作。
- 调试绘制：`LineDrawList` 提交单帧世界空间线段/包围盒，`DebugRenderer` 在场景 pass 内绘制，
  使用当前相机和正常深度测试；不依赖 ImGui，编辑器选中框是其中一个调用方。
- 资产：`AssetDatabase` 管身份与依赖，`ImportService` 管导入，`AssetManager` 协调加载与发布，
  `AssetRegistry` 是唯一 Handle 缓存；`ResourceManager` 只创建设备资源。
  导入、资产序列化和数据库更新用 `AssetResult<T>` 返回预期失败，调用方决定如何报告；GPU 错误仍保留 Vulkan 结果码。
- 编辑器：`Editor` 装配依赖与帧阶段，`EditorAssets` 管引用选择／模型放置的资源准备、源监视和写入确认，
  `SceneFileDialog` 管路径弹窗；属性控件显式返回手势状态，`SceneDocument` 与 Play 会话仍保持独立。
  Project 消费资产操作返回的扫描结果并更新目录树；Inspector 按 Handle/revision 管理自己的资产缓存，不依赖入口手动失效。
  `viewport/viewport.h` 是视口功能入口，拥有 ViewPanel 与 TransformGizmo，负责纹理同步、相机更新和拾取／选中反馈；
  活动 Scene 按调用传入，不持有 Engine。相机状态及算法集中在 `viewport/camera_controller`，资产引用控件与载荷集中在 `assets/asset_reference`。
- Mesh Runtime 只读已发布的 Mesh Artifact；缓存丢失需先导入，不自动回退解析 glTF。
  Texture 暂时直接解码源文件，后续再引入 Artifact。
- 世界 +Y 向上，Vulkan Viewport 用负高度转换画面坐标；`flip_y` 仅控制纹理导入。
  Shader 编译产物只进入构建目录，学习源码不作为生产 Shader 的隐式依赖。

详细说明：[资源所有权](docs/architecture/rendering-ownership.md) ·
[资产管线](docs/architecture/asset-pipeline.md) · [场景格式](docs/architecture/scene-format.md) ·
[路线图](docs/engine-roadmap.md)。

C++ 遵循根目录 `.clang-format`（90 列），只格式化相关代码，不处理 Shader 和第三方源码。
头文件应能独立编译，实现文件直接包含自己使用的类型，不依赖入口头的传递包含。
引擎 PCH 仅预编译常用标准库头，不包含 Vulkan、ImGui 或项目业务头；PCH 不是隐式依赖来源。
排查 include 可用 `cmake --preset dev-debug -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON` 关闭 PCH，
验证后用同一命令将该选项设回 `OFF`。
贡献约定见 [AGENTS.md](AGENTS.md)。
