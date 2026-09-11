# Comet 引擎

Comet 是使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与 ImGui 编辑器，目前重点是场景编辑、资产导入和单视口交互。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：core、scene、asset、render、graphics、config、diagnostics |
| `engine/shaders/` | 引擎 Shader；只编译 CMake 显式列表，其余源码保留供学习 |
| `editor/` | 编辑器入口、面板及 `resources/` 私有字体等资源 |
| `app/` | Runtime 示例入口及 `resources/` 私有图标 |
| `demo/` | 随仓库提供的完整示例项目，与引擎／编辑器源码分开 |
| `demo/assets/` | 示例项目场景、源资产及相邻 `.meta`，进入版本控制 |
| `demo/project.yaml` | 示例项目描述：版本、名称和启动场景 |
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
`COMET_NATIVE_OPTIMIZATION` 只适合本机构建。配置与诊断采用“编译期能力 + Profile 运行时策略”。

macOS 和 Windows 下 app/editor 分别使用橙色、蓝色彗星静态图标，资源位于各自的 `resources/icons/`，不参与项目资产扫描。
macOS 在构建目录内生成 `app/Comet.app` 和 `editor/CometEditor.app`，内含静态 ICNS 图标；启动脚本自动使用 bundle 内的新入口。
Windows 通过 `.rc` 将 ICO 编译进 exe，GLFW 自动用作初始窗口图标；不在运行时加载 PNG，Linux 暂不配置图标。
开发构建仍依赖仓库资源与开发动态库，不是独立分发包。

## 打开项目

```bash
./editor.sh                         # 打开仓库 demo/ 中的示例项目
./editor.sh ./demo                  # 显式打开同一示例项目
./editor.sh /Projects/MyGame        # 读取该目录的 project.yaml
./editor.sh /Projects/MyGame/project.yaml
```

编辑器可执行文件也接受同样的可选路径参数；相对路径以调用者的工作目录为基准。`--help` 显示用法。
项目需要 `project.yaml` 和 `assets/`；资源及相邻 `.meta` 一起迁移，`.comet/` 是可重建的本地数据。
示例项目根目录是仓库的 `demo/`，不是仓库根；可完整复制该目录作为外部项目。
旧仓库根 `.comet/` 不自动迁移，新位置缺少缓存／布局时会重新生成，旧数据保留。

```yaml
version: 1
name: My Game
startup_scene: scenes/main.scene
```

`startup_scene` 相对项目 `assets/`；省略或空字符串表示空场景。项目描述不配置默认材质，场景保存自己的材质引用。
项目描述错误或缺少 assets 时启动失败，不回退仓库项目；启动场景缺失／损坏则记录错误并打开空场景，不覆盖原文件。
引擎 Profile、编辑器快捷键仍读取开发构建自带的 `config/`，字体／图标／Shader 不需要复制到每个项目。
当前支持启动时选择一个项目，尚不支持运行中切换项目、最近项目列表、项目创建向导或独立打包。

## 编辑器使用

- 启动打开项目描述指定的场景，仓库示例为 `demo/assets/scenes/default.scene`；与 File → Open 共用资源准备流程。
  Open/Save 的相对路径以当前项目 assets 为基准，只接收该目录内的 `.scene`，拒绝越界和指向外部的符号链接。
  保存写回当前场景文件；重启仍打开配置的启动场景，尚不恢复上次打开的其他场景。
- Edit 使用独立编辑器相机；Play 使用克隆场景的 primary Camera，Stop 后返回 Edit，不回写运行时修改。
- 打开场景及切换 Edit/Play 时，按组件描述收集资源引用并加载；坏引用保留并记录 Log，不阻止打开整个场景。
  Mesh 只加载已有 Artifact；后台导入发布、扫描或显式纹理重导入成功后，在 UI 结束处合并重查当前场景，不每帧遍历引用。
  大量资源首次加载仍可能同步停顿；尚未提供增量需求索引和上传预算。
- 画面内右键或 Alt/Option+左键环绕，中键或 Alt/Option+Shift+左键平移，滚轮/双指垂直滚动缩放。
- 2D/3D 切换编辑器相机的正交/透视投影，不修改 Scene Camera；Play 中不可切换。
- Edit 画面内左键选择最近的模型包围盒，空白点击清空；视口获得键盘焦点后按 F 聚焦选中 Mesh。
  选中 Mesh 显示随实体变换的橙色包围盒，受场景深度遮挡；清空选择或进入 Play 后不显示。
  当前是包围盒粗拾取，不是三角形级拾取或模型轮廓描边。
- Edit 选中实体后可左键拖动红／绿／蓝箭头，沿世界 X／Y／Z 轴平移；操作手柄覆盖在模型上，不受深度遮挡。
  工具栏 Tool 可切换 World／Local；Local 跟随实体旋转及父级变换，不受实体自身零／负缩放影响。
  Snap 按 Step（默认 0.25 世界单位）吸附相对拖动起点的距离，不对齐绝对网格；工具设置仅保留在当前会话。
  一次拖动只记一条撤销，Escape 取消；拖动期间不响应相机导航，失焦或隐藏视口会回退未完成的拖动。
- Edit 中 Inspector 的名称、Transform、Camera、Mesh/Material 引用支持撤销／重做，一次编辑手势记一条，Escape 取消。
  使用 Edit 菜单或 Ctrl+Z / Ctrl+Y（macOS 为 Cmd+Z / Cmd+Shift+Z）；文本框编辑时不抢占输入控件的撤销。
  New/Open 成功及 Edit/Play 切换清空历史；Play 属性仍可实时调试，但不记入 Edit 历史。
  资产文件修改和保存暂不纳入撤销历史。
- Edit 中 Inspector 可用 Add Component 添加 Camera／Mesh Renderer，右键组件标题移除并支持撤销／重做。
  Name 和 Transform 不开放增删；Play 只允许调试现有属性，组件增删禁用。
- Hierarchy 的创建、删除子树和拖拽改父级支持撤销／重做，在 UI 绘制结束后执行；Play 禁用结构操作。
  右键空白处或 Scene 选择 Create Entity 创建根实体；右键实体选择 Create Child 创建其子实体，并展开父节点。
  新实体使用默认本地 Transform，创建及父子关系是同一条撤销命令；Delete 删除右键实体及其子树。
  撤销恢复实体 UUID 和组件值，不恢复原 EntityId 或选择状态；改父级保留本地 Transform，世界位置可能改变。
- Edit 中右键 Hierarchy 实体选择 Duplicate，可复制整棵子树并选中新根节点；一次撤销移除整个副本。
  副本使用新 UUID，保持原外部父级与资产引用，根名称追加 ` Copy`；不自动偏移，不保证名称唯一。
- 编辑器快捷键配置在 `config/profiles/editor-dev.yaml` 的 `editor.shortcuts`，修改后重启生效。
  新建／打开／保存、撤销／重做、聚焦支持多绑定；`Primary` 表示 macOS Cmd／其他平台 Ctrl，`[]` 禁用绑定。
  所有构建的编辑器读取此段，不改变当前 Profile 的诊断配置；缺省项用默认值，绑定错误或冲突会记录日志并回退默认绑定。
- Play 分辨率可选 Free、16:9、HD（1280×720）、FHD（1920×1080）；Fit 等比适应面板，1x 按原尺寸显示并裁切。
- Project 自动监视资产变化，右键菜单的 Refresh 可主动重扫；拖动资产到已显示的目录或 assets 根节点可移动。
  右键资产选择 Rename 修改名称，扩展名保持不变；源文件和 `.meta` 成对移动，冲突不覆盖，暂不支持整目录移动。
  Inspector 的材质和纹理设置按变化事件提交，更新日志统一进入 Log。
- 可从 Finder／系统文件管理器将 PNG/JPEG、glTF/GLB 拖入 Project：文件夹行是目标目录，资产行取所在目录，空白处取 assets 根目录。
  复制外部文件而非移动，glTF 连同相对路径的 buffer／图片复制；不沿用外部 `.meta`，不覆盖同名文件或元数据。
  整批准备与校验失败时不发布，发布／扫描失败时回滚；成功后自动扫描并排队生成 Mesh Artifact，不自动创建实体。
  暂不接收整文件夹、独立 `.bin`、网络或含 `..` 的依赖路径；复制／校验同步执行，大文件可能短暂阻塞 UI。诊断进入 Log。
- 模型及外部 buffer 放入 assets 后，编辑器扫描事件会自动触发后台导入；有效 Artifact 直接复用。
  未加载模型只生成缓存，不创建 GPU 对象。选中模型不显示额外状态栏；右键 Reimport 可强制重建或重试，错误进入 Log。
  手动删除缓存后用右键 Refresh 或重启编辑器触发补建；不在每帧检查磁盘缓存。
- Edit 中将 Project 模型拖到 Viewport 图像，可在相机关注平面上创建并选中根实体，支持一次 Undo/Redo。
  材质引用暂留空，需要在 Inspector 手动指定后才绘制；后续接入引擎内置基础材质，不再由项目配置默认材质。
  不读取 glTF 材质；放置只加载已发布 Artifact，首次导入未完成时需等待后重试。
  当前材质方案为 `unlit_texture_blend`：无光照、两张纹理等比例混合；暂不支持在材质中切换项目 Shader。
- Inspector 的 Mesh、Material 引用和材质纹理槽按资产相对路径下拉选择，按类型过滤，底层仍保存 Handle。
  Edit 中也可把 Project 资产拖到对应引用框；拖动不切换当前选中对象，下拉选择仍保留。
  组件引用在 UI 后加载成功才赋值，支持 Undo/Redo；Mesh 只加载已有 Artifact，首次导入未完成时等待后重试。
  材质纹理槽沿用文件更新流程，不进入场景撤销；失败保留原值。丢失引用显示 Missing，不自动清空。
  Play 保留下拉方式调试 Runtime 引用，不记录场景历史，不接受 Inspector 资产拖放。
  日常面板和资源悬停提示不显示内部 Handle，底层引用及诊断日志保留。
- View 菜单直接读取面板开关，关闭窗口后一次点击即可重新打开；暂未实现的菜单项显示为禁用。

## 架构入口

- 启动：app/editor 共用 `RUN_APP` 和 `Comet::launch`，统一参数传递、`--help`、错误退出和 Application 所有权。
  各入口显式提供创建函数，负责自己的参数校验和依赖准备；`Editor` 只接收已加载的 `Project`，不解析命令行。
  之后仍由 `Comet::run` 初始化引擎并进入主循环，不通过构造函数签名推断启动行为。
- 渲染：`Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
  帧准备与 UI 修改完成后才提取 Scene；Scene 只保存组件和资产 Handle，GPU 生命周期由渲染层管理。
- 调试绘制：`LineDrawList` 提交单帧世界空间线段/包围盒，`DebugRenderer` 在场景 pass 内绘制，
  使用当前相机和正常深度测试；不依赖 ImGui，编辑器选中框是其中一个调用方。
- 资产：`AssetDatabase` 管身份与依赖，`ImportService` 管导入，`AssetManager` 协调加载与发布，
  `AssetRegistry` 是唯一 Handle 缓存；`ResourceManager` 只创建设备资源。
- 编辑器：`Editor` 装配依赖与帧阶段，`EditorAssets` 管引用选择／模型放置的资源准备、源监视和写入确认，
  `SceneFileDialog` 管路径弹窗；属性控件显式返回手势状态，`SceneDocument` 与 Play 会话仍保持独立。
- Mesh Runtime 只读已发布的 Mesh Artifact；缓存丢失需先导入，不自动回退解析 glTF。
  Texture 暂时直接解码源文件，后续再引入 Artifact。
- 世界 +Y 向上，Vulkan Viewport 用负高度转换画面坐标；`flip_y` 仅控制纹理导入。
  Shader 编译产物只进入构建目录，学习源码不作为生产 Shader 的隐式依赖。

详细说明：[资源所有权](docs/architecture/rendering-ownership.md) ·
[资产管线](docs/architecture/asset-pipeline.md) · [场景格式](docs/architecture/scene-format.md) ·
[路线图](docs/engine-roadmap.md)。

C++ 遵循根目录 `.clang-format`（90 列），只格式化相关代码，不处理 Shader 和第三方源码。
贡献约定见 [AGENTS.md](AGENTS.md)。
