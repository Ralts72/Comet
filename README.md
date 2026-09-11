# Comet 引擎

Comet 是使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与 ImGui 编辑器，目前重点是场景编辑、资产导入和单视口交互。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：core、scene、asset、render、graphics、config、diagnostics |
| `engine/shaders/` | 引擎 Shader；只编译 CMake 显式列表，其余源码保留供学习 |
| `editor/` | 编辑器入口、面板及 `resources/` 私有字体等资源 |
| `app/` | Runtime 示例入口及 `resources/` 私有图标 |
| `assets/` | 项目源资产与相邻 `.meta`，进入版本控制 |
| `config/` | `common.yaml` 与各 Profile 配置 |
| `.comet/` | 本机缓存与编辑器布局，不进入版本控制 |
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

## 编辑器使用

- Edit 使用独立编辑器相机；Play 使用克隆场景的 primary Camera，Stop 后返回 Edit，不回写运行时修改。
- 画面内右键或 Alt/Option+左键环绕，中键或 Alt/Option+Shift+左键平移，滚轮/双指垂直滚动缩放。
- 2D/3D 切换编辑器相机的正交/透视投影，不修改 Scene Camera；Play 中不可切换。
- Edit 画面内左键选择最近的模型包围盒，空白点击清空；视口获得键盘焦点后按 F 聚焦选中 Mesh。
  选中 Mesh 显示随实体变换的橙色包围盒，受场景深度遮挡；清空选择或进入 Play 后不显示。
  当前是包围盒粗拾取，不是三角形级拾取或模型轮廓描边。
- Edit 选中实体后可左键拖动红／绿／蓝箭头，沿世界 X／Y／Z 轴平移；操作手柄覆盖在模型上，不受深度遮挡。
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
- 模型及外部 buffer 放入 assets 后，编辑器扫描事件会自动触发后台导入；有效 Artifact 直接复用。
  未加载模型只生成缓存，不创建 GPU 对象。选中模型不显示额外状态栏；右键 Reimport 可强制重建或重试，错误进入 Log。
  手动删除缓存后用右键 Refresh 或重启编辑器触发补建；不在每帧检查磁盘缓存。
- Edit 中将 Project 模型拖到 Viewport 图像，可在相机关注平面上创建并选中根实体，支持一次 Undo/Redo。
  使用启动示例材质，不读取 glTF 材质；放置只加载已发布 Artifact，首次导入未完成时需等待后重试。
  当前材质方案为 `unlit_texture_blend`：无光照、两张纹理等比例混合；暂不支持在材质中切换项目 Shader。
- Inspector 的 Mesh、Material 引用和材质纹理槽按资产相对路径下拉选择，按类型过滤，底层仍保存 Handle。
  Mesh/Material 选择成功前先导入／加载，失败保留原引用；丢失引用显示 Missing，不自动清空。
  日常面板和资源悬停提示不显示内部 Handle，底层引用及诊断日志保留。
- View 菜单直接读取面板开关，关闭窗口后一次点击即可重新打开；暂未实现的菜单项显示为禁用。

## 架构入口

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
