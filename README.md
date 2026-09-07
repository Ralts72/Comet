# Comet 引擎

Comet 是使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与 ImGui 编辑器，目前重点是场景编辑、资产导入和单视口交互。

## 项目结构

| 目录 | 职责 |
| --- | --- |
| `engine/src/` | 引擎库：core、runtime、scene、asset、render、graphics、config、diagnostics |
| `engine/shaders/` | 引擎 Shader；只编译 CMake 显式列表，其余源码保留供学习 |
| `tools/shader/` | 共用 CPU ShaderCompiler 与构建 CLI；不链接进运行时 engine |
| `editor/` | 编辑器入口、面板及 `resources/` 私有字体等资源 |
| `app/` | Runtime 示例入口 |
| `demo/` | app／editor 共用的示例项目组件与原生脚本 |
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

测试构建另提供可复现 forward 测量入口（CSV、对象数、逻辑窗口宽高、采样帧数、Bloom 0/1）：

```bash
cmake --preset ci-release -B build-profile
cmake --build build-profile --target render_profile --parallel
./build-profile/tests/render_profile /tmp/comet-profile.csv 64 640 360 240 1
```

它在临时项目导入内置 cube/PBR 资源，运行三灯、方向光阴影、4×MSAA 和可选 Bloom；跳过 32 帧预热。
报告记录实际 framebuffer 尺寸、CPU/GPU 分段、样本数和 P50/P95；测试进程串行运行，测量期间不要同时构建或运行其他 GPU 测试。
该入口关闭请求的 validation，不修改源资产；它是固定场景基线，不代表编辑器 UI 或真实游戏项目性能。

示例 app：W/A/S/D 在世界水平面移动 Main Camera，Q/E 降低／升高，左 Shift 加速，滚轮前后移动，Escape 退出。
标准映射手柄使用第一个已连接设备的左摇杆移动，左右扳机降低／升高；失焦不移动。它目前是相机演示，不是角色／物理控制器。

## 编辑器使用

- Edit 使用独立编辑器相机；Play 使用克隆场景的 primary Camera，Stop 后返回 Edit，不回写运行时修改。
  Play 工具栏的 `||` 暂停游戏更新，`>` 恢复，`|>` 单步；单步执行一个固定步及一次同等时长的普通更新后保持暂停。
  暂停不冻结 UI、渲染或资产处理；按键边沿／滚轮不会在恢复时补放，单步只采样当前按住状态。
  游戏输入要求 Play 画面取得焦点且鼠标位于可见图像内；文本编辑、弹窗、其他活动控件、拖拽或画面外区域均阻断。
  重新进入画面时，原先按住的键／鼠标／手柄按钮需松开再按，取得输入的点击和位移不传给游戏。
  默认 Editor Cube 的 Spin Script 在 Play 中旋转；Inspector 可调整 Degrees per second／Enabled，暂停后单步观察，Stop 恢复 Edit 原件。
  旧场景不会自动添加脚本，可通过 Inspector 的 Add Component 添加 Spin Script；参数沿用场景保存与撤销链路。
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
  默认示例使用 `materials/pbr.mat` 和 Key Light；调整 Base color、Metallic、Roughness 可观察金属度与高光变化。
  `materials/lit.mat` 保留 Lambert 漫反射，原来的两种 unlit 材质仍不受灯光影响。
  Inspector 的 Add Component 可添加 Light，Type 选择 Directional/Point/Spot；Transform 的本地 -Z 是出光方向。
  Intensity/Color 控制照明，Range 仅用于点光/聚光，Inner/Outer angle 为聚光半锥角且须满足内角小于外角。
  Directional 可勾选 Cast directional shadow；当前为单张 1024² 阴影图，按实体 ID 选择首个有效投影方向光。
  默认 Ground 接收立方体阴影，移动物体或开关投影可直接观察；点光和聚光暂不投影。
  灯光没有隐式环境光，全部关闭时 lit/PBR 物体为黑色；PBR 当前为不透明纯色金属粗糙度模型，尚无 IBL 或材质贴图。
  `config/common.yaml` 的 `render.exposure` 控制曝光，`bloom_strength`／`bloom_threshold` 控制高亮光晕；
  强度为 0 时跳过 Bloom pass。默认阈值为线性 HDR 的 1，可提高 Key Light 强度观察光晕，普通低亮度颜色不会自行发光。
  `.mat` 的 texture/scalar/vector 参数由布局生成 Inspector 控件，实际变化才保存并更新材质，浏览默认值不改写文件。
  缺失纹理槽可逐个补齐，完整后自动发布；未完整的编辑仅保留在当前资产草稿中，切换资产或刷新会丢弃草稿。
- View → Render Stats 打开默认隐藏的渲染诊断面板；Capture 开关整帧 CPU 墙钟、场景图 CPU 录制与延迟 GPU pass 计时。
  CPU 墙钟包含等待／UI／提交，GPU 结果标明已完成的 submission，不应把两者当作同一帧的纯计算耗时。
  `diagnostics.enable_render_diagnostics` 独立于编译期开关控制的 CPU Profiler；dev-debug/editor-dev 默认开启，app-release 默认关闭。
  显存预算至多每秒采样一次，并区分驱动报告与 VMA 估算；Save allocation report 手动生成
  `.comet/editor/diagnostics/gpu-allocations.json`，原子替换上次报告，成功／失败只进入 Log。

## 架构入口

- 平台：Window 只拥有自己的原生窗口；GLFW 在首次创建窗口时初始化，正常进程退出时统一终止。
  engine、宿主与 ImGui 统一链接共享 GLFW，避免静态副本各自维护一份平台状态；分发程序时需携带 GLFW 动态库。
  创建／销毁窗口和平台事件处理必须在主线程，应用及测试不得另行调用 `glfwTerminate()`。
  关闭一扇窗口不会销毁其他窗口，连续启动 Engine 不反复初始化平台；这不代表已支持多窗口多 Renderer 编排。
- 输入：Window 在事件轮询后发布 `Input::Frame`，Engine 提供只读快照；键鼠边沿、位移／滚轮与标准手柄状态均有界保存。
  帧快照可复制，后续平台事件不改写已发布数据；失焦释放控制，ImGui 串接原回调，底层不决定编辑器 Viewport 的游戏输入路由。
  原生窗口 user pointer 归 Window；外部替换输入回调时须保留调用链，不能绕过输入采集。
  `Input::Gate` 为消费者生成独立受控快照，不修改平台帧；Runtime 使用它隔离游戏输入，editor 只决定本帧是否放行。
- 运行时：Engine 拥有 `SceneRuntime`，按注册顺序串行执行 System；启动正序、停止逆序。
  帧准备／UI 后执行有界 Fixed Update，再执行一次普通 Update，最后提取场景；场景替换先停止运行时。
  默认固定步 1/60 秒，每帧最多 8 步、接收最多 0.25 秒，超额整步丢弃并计入 Timing，不无限追赶。
  固定输入的边沿／位移跨零步帧累积、只由首个固定步消费；同一输入 serial 不重复触发边沿。
  app 方块旋转走固定更新、相机走普通更新；编辑器 Play 启动克隆场景的 Runtime，Stop 先停止再恢复 Edit 原件。
  Running／Paused 是 Runtime 自身状态，不增加 EditorMode::Paused。
- 脚本：`NativeScriptSystem` 从已有 ComponentDescriptor 创建每实体的 C++ 实例；参数仍是 Scene 组件，沿用 Inspector／Serializer／Undo。
  `ScriptComponent` 的瞬态代标识区分组件删除重建，字段编辑不重启实例；脚本不缓存组件地址，停止清理不依赖实体仍存活。
  `demo/` 是 app／editor／测试共享的示例项目模块，Spin 参数与行为都在这里；engine 只包含脚本机制，不依赖 demo。
- 编辑器：面板是可见状态的唯一 owner；View 菜单只观察已登记面板并切换状态，关闭按钮和菜单不会各存一份 bool。
  Editor 在释放面板前移除 UI 回调并销毁菜单；业务编辑仍走既有命令历史，不引入全局 EventBus。
- 渲染：`Scene → SceneExtractor → RenderScene → SceneResolver → RenderSubmission → SceneRenderer`。
  帧准备、UI 与运行时修改完成后才提取 Scene；Scene 只保存组件和资产 Handle，GPU 生命周期由渲染层管理。
  交换链创建／图像枚举失败会暂停呈现并间隔重试，不复用已退休图像；设备／surface 丢失仍需专门恢复。
  SceneResolver 不解析材质属性；渲染侧按 MaterialLayout 准备并缓存材质绑定，按对象身份与 revision 失效。
  MaterialRenderer 负责排序和绘制 Mesh：FrameSet 按 slot 更新，MaterialSet 按版本创建并跨 slot 复用。
  LightComponent 经 RenderLight 值快照进入 FrameSet 的 LightingData；每帧最多按 EntityId 选取 32 个有效灯光。
  超限/无效灯光跳过，数量变化时进入 Log；灯光变化不重建材质 descriptor。
  Lambert/PBR 共用灯光衰减与阴影采样；PBR 的相机位置／正交观察方向属于 FrameSet，不进入材质版本。
  app/editor 共用 `Shadow → HDR Scene → 可选 Bloom → tone mapping → SDR output`；场景和 MSAA resolve 使用 RGBA16F，最终目标仍供窗口或 Viewport 消费。
  PostProcessRenderer 拥有半分辨率 Bloom ping-pong 目标，先提取高亮、分离模糊，再在线性 HDR 中合成；不依赖 HDR 线性过滤能力。
  曝光默认 1，采用 `1-exp(-color*exposure)` 映射高亮，输出按 sRGB/UNORM 附件选择硬件或 Shader 编码；画面不再等同于直接写入材质颜色。
  后处理参数在帧边界更新，开关 Bloom 时才重编图；HDR／SDR／Bloom resize 候选就绪后一起切换，旧 GPU 资源由在途帧保留。
  RenderGraph 将有序 pass 的显式资源读写编译为 Barrier2，管理 HDR 附件和后处理采样依赖；最终输出转换仍由 RenderPass 负责。
  图不自动分配资源、不持有全局 Image layout；跨 submission 由调用方传递导出状态，跨队列调度尚未实现。
  RenderDiagnostics 绑定既有 FrameScheduler，逐 slot 保留查询池，只读取已确认完成的提交，不为计时增加 GPU 等待。
  每图最多记录 32 个 pass 明细；设备不支持时间戳时保留 CPU 诊断，超限图继续绘制但跳过该图 GPU 计时。
- Shader：构建 CLI 与工具层 `ShaderCompiler` 共用 stage、entry、defines、target、include 快照契约，
  通过 depfile 跟踪已有共享头文件；失败不覆盖旧字节码。运行时不带源编译器。
  编辑器监控 `engine/shaders/glsl/` 的六个 `material_*` 生产 Shader 和 `debug_line.vert/frag` 及实际 include，
  约 200 ms 检查、150 ms 防抖后后台整组编译；帧边界整组发布，失败仅进入 Log 并保留旧画面。
  已登记材质字段可调整 offset、参数块大小和 binding：同时重建布局及所有驻留材质，再更新 Inspector 的布局快照。
  新增／删除／改类型的属性需显式元数据支持；Frame、阶段输入输出与 push constant 固定契约仍拒绝改变。
  当前材质只支持普通浮点 sampler2D，不能热改成 Cube／数组／整数／深度比较纹理。
  共用顶点 Shader 的所有材质必须整组发布：unlit 三文件一组，Lambert/PBR 三文件一组，拒绝只更新部分消费者。
  Debug 两 Shader 独立成组，固定顶点／push 接口不变时可热更新；两组各自失败保旧，不阻塞另一组。
  SPIRV-Reflect 子模块从实际字节码生成 CPU `ShaderInterface`；创建 Pipeline 前校验绑定及 push constant，
  材质另核对参数块大小、偏移和类型。显示名、默认值、颜色及编辑范围仍由 MaterialLayout 定义，不从反射猜测。
- Pipeline：在当前 Device/RenderPass 内按 Shader 内容、布局及完整配置复用，名称只作标签；
  缓存不强持有 GPU Pipeline，最后一个实际使用者（含 FrameSlot）释放后回收。
  Device 的驱动 PipelineCache 独立保存到 `.comet/cache/vulkan/`，按设备与 UUID 分文件；启动校验版本、长度和校验和，
  损坏或不兼容时回退为空缓存，正常关闭时原子保存。它是可删除的加速数据，不是 Shader 或 Pipeline 资产。
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
- 世界 +Y 向上，场景 Vulkan Viewport 用负高度转换画面坐标；fullscreen 用正高度保持纹理行方向，`flip_y` 仅控制纹理导入。
  Shader 编译产物只进入构建目录，学习源码不作为生产 Shader 的隐式依赖。

详细说明：[资源所有权](docs/architecture/rendering-ownership.md) ·
[资产管线](docs/architecture/asset-pipeline.md) · [场景格式](docs/architecture/scene-format.md) ·
[路线图](docs/engine-roadmap.md)。

C++ 遵循根目录 `.clang-format`（90 列），只格式化相关代码，不处理 Shader 和第三方源码。
贡献约定见 [AGENTS.md](AGENTS.md)。
