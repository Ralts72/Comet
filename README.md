# Comet 引擎

Comet 是使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与 ImGui 编辑器，目前重点是场景编辑、资产导入和单视口交互。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：core、scene、asset、render、graphics、config、diagnostics |
| `engine/shaders/` | 引擎 Shader；只编译 CMake 显式列表，其余源码保留供学习 |
| `editor/` | 编辑器入口、面板及 `resources/` 私有字体等资源 |
| `app/` | Runtime 示例入口 |
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
- 编辑器快捷键配置在 `config/profiles/editor-dev.yaml` 的 `editor.shortcuts`，修改后重启生效。
  新建／打开／保存、撤销／重做、聚焦支持多绑定；`Primary` 表示 macOS Cmd／其他平台 Ctrl，`[]` 禁用绑定。
  所有构建的编辑器读取此段，不改变当前 Profile 的诊断配置；缺省项用默认值，绑定错误或冲突会记录日志并回退默认绑定。
- Play 分辨率可选 Free、16:9、HD（1280×720）、FHD（1920×1080）；Fit 等比适应面板，1x 按原尺寸显示并裁切。
- Project 支持刷新、移动与重命名；Inspector 的材质和纹理设置按变化事件提交，更新日志统一进入 Log。

## 架构入口

- 渲染：`Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
  帧准备与 UI 修改完成后才提取 Scene；Scene 只保存组件和资产 Handle，GPU 生命周期由渲染层管理。
- 调试绘制：`LineDrawList` 提交单帧世界空间线段/包围盒，`DebugRenderer` 在场景 pass 内绘制，
  使用当前相机和正常深度测试；不依赖 ImGui，编辑器选中框是其中一个调用方。
- 资产：`AssetDatabase` 管身份与依赖，`ImportService` 管导入，`AssetManager` 协调加载与发布，
  `AssetRegistry` 是唯一 Handle 缓存；`ResourceManager` 只创建设备资源。
- Mesh Runtime 只读已发布的 Mesh Artifact；缓存丢失需先导入，不自动回退解析 glTF。
  Texture 暂时直接解码源文件，后续再引入 Artifact。
- 世界 +Y 向上，Vulkan Viewport 用负高度转换画面坐标；`flip_y` 仅控制纹理导入。
  Shader 编译产物只进入构建目录，学习源码不作为生产 Shader 的隐式依赖。

详细说明：[资源所有权](docs/architecture/rendering-ownership.md) ·
[资产管线](docs/architecture/asset-pipeline.md) · [场景格式](docs/architecture/scene-format.md) ·
[路线图](docs/engine-roadmap.md)。

C++ 遵循根目录 `.clang-format`（90 列），只格式化相关代码，不处理 Shader 和第三方源码。
贡献约定见 [AGENTS.md](AGENTS.md)。
