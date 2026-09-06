# Comet 引擎

Comet 是使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与 ImGui 编辑器，目前重点是场景编辑、资产导入和单视口交互。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：core、scene、asset、render、graphics、config、diagnostics |
| `engine/shaders/` | 引擎 Shader；只编译 CMake 显式列表，其余源码保留供学习 |
| `tools/shader/` | 共用 CPU ShaderCompiler 与构建 CLI；不链接进运行时 engine |
| `editor/` | 编辑器入口、面板及 `resources/` 私有字体等资源 |
| `app/` | Runtime 示例入口 |
| `assets/` | 项目源资产与相邻 `.meta`，进入版本控制 |
| `config/` | `common.yaml` 与各 Profile 配置 |
| `.comet/` | 本机缓存与编辑器布局，不进入版本控制 |
| `tests/`、`3rdparty/` | GoogleTest 测试与第三方依赖 |

## 构建与运行

需要 CMake 3.31+、C++20 编译器、Vulkan SDK、Git LFS 和 Submodule。
Shader 编译器由固定版本 glslang 子模块构建，无需额外安装 `glslangValidator`。

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
- Edit 选中实体后可左键拖动红／绿／蓝手柄，沿 X／Y／Z 轴平移、旋转或缩放；操作手柄覆盖在模型上，不受深度遮挡。
  一次拖动只记一条撤销，Escape 取消；拖动期间不响应相机导航，失焦或隐藏视口会回退未完成的拖动。
  Viewport 的 Tool 菜单选择 Move／Rotate／Scale 和 Snap；Move step 是相对世界距离，Angle step 是相对角度。
  Move／Rotate 可选 World／Local；Scale 固定本地轴，轴末端方块加减对应 scale，中心方块向右上拖动按比例放大。
  Scale step 控制相对 scale 增量／比例增量，可穿过零形成负缩放；中心比例缩放不会恢复本来为零的轴，可改用轴手柄。
  Local 跟随实体旋转及父级变换，不受实体自身负／零缩放反转；非均匀缩放／剪切父级下旋转需选 Local。
  设置不改变 Scene Camera，也不写入场景文件。
- Edit 中 Inspector 的名称、Transform、Camera、Mesh/Material 引用支持撤销／重做，一次编辑手势记一条，Escape 取消。
  Mesh/Material 引用可按路径选择或从 Project 拖入；按资产类型过滤，加载失败保留旧引用，None 清空引用。
  使用 Edit 菜单或 Ctrl+Z / Ctrl+Y（macOS 为 Cmd+Z / Cmd+Shift+Z）；文本框编辑时不抢占输入控件的撤销。
  New/Open 成功及 Edit/Play 切换清空历史；Play 属性仍可实时调试，但不记入 Edit 历史。
  Inspector 底部 Add Component 添加可选组件，右键组件标题可移除；均支持撤销，名称和 Transform 不开放增删。
  Hierarchy 的 + 创建、- 删除子树及拖拽调整父级也支持撤销；拖拽保留本地 Transform，世界位置可能改变。
  右键实体选择 Duplicate 复制整棵子树，生成新 UUID、保留资产引用，整次复制只记一条历史。
  Play 不开放场景结构编辑；资产文件修改和保存暂不纳入撤销历史。
  重新打开场景会按类型化引用加载资源；缺失引用保留以便修复，不阻止打开整个文档。
  缺少 Mesh 缓存时在 Project 补导入，发布成功后场景会重新检查资源，不必重开场景。
- Play 分辨率可选 Free、16:9、HD（1280×720）、FHD（1920×1080）；Fit 等比适应面板，1x 按原尺寸显示并裁切。
- Project 支持刷新、移动与重命名；Inspector 的材质和纹理设置按变化事件提交，更新日志统一进入 Log。
  将 glTF/GLB 及依赖文件放入 assets 后 Refresh，选择模型即可查看 Artifact 状态并 Import/Reimport。
  检查与解码在后台进行，未加载模型只生成缓存；手动删除缓存后 Refresh 可重新检查，错误见 Log。
  Edit 中可将 Mesh 拖入 Viewport：在鼠标对应的相机关注平面创建实体，使用项目 demo 材质，支持一次撤销。
  拖入只加载已发布 Artifact（或已驻留 Mesh），不会隐式导入源模型；缺少缓存时先在 Project 执行 Import。
  选中材质后可把 Project 的 Texture 拖入纹理槽，沿用材质保存／更新流程，不进入场景撤销历史。
  `materials/demo.mat` 使用双纹理混合，`materials/solid.mat` 使用纯色布局；可通过 MeshRenderer 的 Material 引用切换。
  `.mat` 的 texture/scalar/vector 参数由布局生成 Inspector 控件，实际变化才保存并更新材质，浏览默认值不改写文件。
  缺失纹理槽可逐个补齐，完整后自动发布；未完整的编辑仅保留在当前资产草稿中，切换资产或刷新会丢弃草稿。

## 架构入口

- 渲染：`Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
  帧准备与 UI 修改完成后才提取 Scene；Scene 只保存组件和资产 Handle，GPU 生命周期由渲染层管理。
  SceneResolver 不解析材质属性；渲染侧按 MaterialLayout 准备并缓存材质绑定，按对象身份与 revision 失效。
  MaterialRenderer 负责排序和绘制 Mesh：FrameSet 按 slot 更新，MaterialSet 按版本创建并跨 slot 复用。
- Shader：构建 CLI 与工具层 `ShaderCompiler` 共用 stage、entry、defines、target、include 快照契约，
  通过 depfile 跟踪已有共享头文件；失败不覆盖旧字节码。运行时不带源编译器。
  编辑器自动监控 `engine/shaders/glsl/` 的 `material_mesh.vert`、`material_textured.frag`、`material_solid.frag` 及实际 include，
  约 200 ms 检查、150 ms 防抖后后台整组编译；兼容接口在帧边界生效，错误仅进入 Log 并保留旧画面。
  改变绑定、参数布局或阶段输入/输出目前明确拒绝；DebugRenderer Shader 热更新和接口重建仍待接入。
  SPIRV-Reflect 子模块从实际字节码生成 CPU `ShaderInterface`；创建 Pipeline 前校验绑定及 push constant，
  材质另核对参数块大小、偏移和类型。显示名、默认值、颜色及编辑范围仍由 MaterialLayout 定义，不从反射猜测。
- Pipeline：在当前 Device/RenderPass 内按 Shader 内容、布局及完整配置复用，名称只作标签；
  缓存不强持有 GPU Pipeline，最后一个实际使用者（含 FrameSlot）释放后回收。
  顶点/片元 specialization 支持 bool 与 32 位 int/uint/float，按反射校验 ID/类型并实际传给 Vulkan；
  改变数组长度的接口变体使用编译期 defines，不能用 specialization 绕过布局校验。
- 调试绘制：`LineDrawList` 提交单帧世界空间线段/包围盒，`DebugRenderer` 在场景 pass 内绘制，
  使用当前相机和正常深度测试；不依赖 ImGui，编辑器选中框是其中一个调用方。
- 资产：`AssetDatabase` 管身份与依赖，`ImportService` 管导入，`AssetManager` 协调加载与发布，
  `AssetRegistry` 是唯一 Handle 缓存；`ResourceManager` 只创建设备资源。
- Mesh Runtime 只读已发布的 Mesh Artifact；缓存丢失需先导入，不自动回退解析 glTF。
  Texture 暂时直接解码源文件，后续再引入 Artifact。
- 后台任务有容量限制；AssetManager 合并同资产的最新待执行请求，owner 后续处理周期继续派发。
  超过资产等待队列容量的请求会明确拒绝并进入 Log，需重试导入；不会阻塞界面或替换旧 Runtime 资源。
  默认每次处理最多 2 个完成结果、约 2 ms 软预算；失败和过期结果也计数，未处理结果继续占用在途额度。
- 世界 +Y 向上，Vulkan Viewport 用负高度转换画面坐标；`flip_y` 仅控制纹理导入。
  Shader 编译产物只进入构建目录，学习源码不作为生产 Shader 的隐式依赖。

详细说明：[资源所有权](docs/architecture/rendering-ownership.md) ·
[资产管线](docs/architecture/asset-pipeline.md) · [场景格式](docs/architecture/scene-format.md) ·
[路线图](docs/engine-roadmap.md)。

C++ 遵循根目录 `.clang-format`（90 列），只格式化相关代码，不处理 Shader 和第三方源码。
贡献约定见 [AGENTS.md](AGENTS.md)。
