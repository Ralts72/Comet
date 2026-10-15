# Comet 引擎

Comet 是供作者个人学习使用的 3D 游戏引擎与编辑器，使用 C++20、CMake 和 Vulkan。
引擎对外提供一个 `engine` 动态库；ImGui 编辑器与独立 App 共用场景、资产、Gameplay 和 RmlUi 游戏界面。

目前支持场景编辑与撤销、glTF 静态／骨骼模型、参数化动画状态机、1D／2D 混合、独立动画库、分层与骨骼遮罩、动画事件与骨骼挂点、纹理 Mipmap、PBR／阴影／IBL／Bloom、Lua 热重载、刚体、凸包／静态网格碰撞、碰撞层与角色控制、导航网格与 NPC 寻路避障、射线／形状扫掠／范围查询、游戏内关卡切换与检查点存档、空间音效／监听者、流式 WAV 音乐、CPU billboard 粒子和游戏设置。
独立渲染线程与场景更新重叠执行。Prefab 子树复用、基础覆盖和 Lua 生成已接入；嵌套 Prefab、高级动画／粒子、GI、路径追踪继续按路线图推进；macOS 已支持独立 App 导出。

## 快速开始

需要 CMake 3.31+、C++20 编译器、Vulkan SDK 和 Git LFS。第三方源码统一由 Git Submodule 固定版本，
Shader 编译器随项目构建，无需另装 `glslangValidator`。macOS 编辑器导出与打包测试还需要 Python 3.9+。

构建适配集中在 `3rdparty/CMakeLists.txt`，stb／VMA 的实现入口也由主仓维护；各依赖子仓保留上游原貌。

在仓库根目录执行：

```bash
git lfs install
git lfs pull
git submodule update --init --recursive
cmake --preset dev-debug
cmake --build --preset dev-debug --parallel
ctest --preset dev-debug
```

macOS／Linux 可用脚本构建并启动：

```bash
./editor.sh                       # 打开示例项目
./release.sh                      # 运行示例 App，启动前自动准备资产
./editor.sh /Projects/MyGame       # 打开自己的项目
./release.sh /Projects/MyGame      # 运行自己的项目
```

脚本也接受 `project.json` 路径；相对路径以调用者的工作目录为基准。可执行文件的 `--help` 显示用法。
源码入口为 `app/main.cpp` 和 `editor/main.cpp`；共用命令行入口后，分别由 `run_game`、`run_editor` 加载项目并运行会话。

| Preset | 用途 | 构建目录 |
| --- | --- | --- |
| `dev-debug` | App、Editor、测试与基准，Debug | `build/` |
| `editor-dev` | Editor，RelWithDebInfo | `build-editor/` |
| `app-release` | App，Release | `build-release/` |

Windows 可使用相同 CMake preset，随后启动构建目录中的可执行文件。开发构建依赖仓库资源；macOS 可通过“项目 → 导出 App”生成可搬移程序，其他平台的完整打包后续接入。

## 试玩 Demo

默认启动 `demo/assets/scenes/character.scene`。WASD／左摇杆移动，空格／手柄 South 跳跃，R 重开；
移动以相机朝向为参照；Shift 跑步、Ctrl 蹲伏，鼠标右键拖动／手柄右摇杆转动视角，滚轮缩放。
引擎内置人形角色和 7 个基础动作；Inspector 可替换网格和动作片段。第一关沿台阶、缓坡和移动平台收集三个目标，完成后点击“进入第二关”；第二关绕过隔墙收集目标，可返回第一关。
角色与收集目标使用项目 Prefab，终点门由 Lua 生成，收集完成后打开。
收集后自动保存，也可点击“保存进度”；“继续存档”回到保存关卡起点并保留两关收集状态。App 与 Editor Play 使用各自的存档。
两个关卡的起点附近共用一个多材质货车模型；在层级中选中“牛奶货车”可查看来源关联和实例覆盖。
点击“设置”或按 F1 可修改显示、画质、音量和按键。两关带流式背景音乐；脚步和“空间音效示例”声源可随相机位置、朝向试听方向与距离衰减；收集目标会爆发粒子并自然消退。
第一关起点旁的烟雾与透明面板用于观察粒子和玻璃的前后遮挡，也可在 Edit 中选中烟雾发射器预览。
起点旁的“动画分层示例”引用 `animations/layered.ani`：二维混合控制步态与蹲伏，上半身由独立层覆盖，动作来自共享 `.anilib`。双击控制器可调整试播参数；具体入口见[动画使用说明](docs/usage.md#2d-混合动画库与分层)。
收集反馈读取 `demo/assets/data/` 中的 CSV／JSON 数值资产；脚本属性选择数据表，Inspector 可查看记录，字段与 Lua 用法见[游戏数值资产](docs/usage.md#游戏数值资产)。

编辑器恢复上次打开的场景。通过“文件 → 切换场景”可选择角色场景，或打开 `scenes/default.scene` 查看材质、灯光与关节演示；`scenes/pbr_surfaces.scene` 展示 PBR 贴图、树叶裁切阴影与透明层叠。
`scenes/navigation.scene` 是独立导航示例：NPC 绕墙巡逻，靠近后追逐玩家，远离后返回巡逻点；移动障碍用于观察局部避障。配置与使用见[导航说明](docs/usage.md#npc-导航)。
可选 HDR 环境图通过 `./tools/download_assets.sh` 下载，成功后同时生成保留场景引用 ID 的 `.meta`；角色场景不依赖这项下载。

## 常用操作

| 任务 | 入口 |
| --- | --- |
| 新建／打开项目 | 文件菜单，项目包含 `project.json` 和 `assets/` |
| 切换同项目场景 | 文件 → 切换场景；有未保存修改时先确认 |
| 设置 App 启动场景 | 项目 → 启动场景 |
| 运行／暂停／单步／停止 | 视口左侧按钮；Stop 恢复 Edit 内容 |
| 编辑实体与组件 | 层级支持 Ctrl/Cmd 多选、Shift 连选；Inspector 批量编辑共同属性，Edit 支持整组撤销 |
| 资源预览 | 项目面板显示纹理、模型、网格与材质球缩略图，悬停可放大；支持模型内嵌贴图 |
| 批量搭建 | 层级右键“批量放置”；多选共用变换手柄、包围框与 F 聚焦，整组操作一次撤销 |
| 查看碰撞形状 | 视口 → 视图 → 碰撞体；支持基础形状、凸包和静态网格线框 |
| 配置动画状态机 | Project 右键新建，双击 `.ani` 打开状态图；Inspector 编辑选中项，现有视口预览角色 |
| 配置骨骼挂点 | 将物体放到角色模型下，添加“骨骼挂点”并选择骨骼；本地变换调整偏移 |
| 预览模型动画 | Edit 中选中角色模型，Inspector → 动画 → 动画预览；支持播放与时间定位 |
| 预览粒子 | Edit 中选中发射器，Inspector → 粒子预览；支持播放、爆发、暂停与单步，可选择纹理与加法／Alpha 混合 |
| 放置 glTF／GLB 模型 | 文件拖入 Project 后等待导入，再拖入视口；实例根右键可从模型更新并保留本地覆盖 |
| 保存／复用 Prefab | 层级右键保存子树，Project 拖入视口；实例根右键更新、写回源、恢复覆盖或解除关联 |
| 创建材质、脚本或 Lua 模块 | Project 面板右键 |
| 编辑 Lua、Shader、UI 源码 | Project 右键“打开源码”，编辑保存后按对应重载流程生效 |
| 设置新玩家默认值 | 项目 → 设置 → 显示／画质／音频／输入 |
| 查看渲染数据与日志 | 渲染统计、Log 面板 |
| 导出 macOS App | 项目 → 导出 App；选择额外关卡与新的 `.app` 路径，自动构建并打包 |
| 仅导出关卡资源 | `comet_prepare_project <项目> --export-assets <新目录> [其他场景 ...]` |

## 配置与资源

根目录 `config.json` 保存三个开发者 Profile 的诊断、底层设备参数与资源预算覆盖；启动入口按 preset 选择分组，缺省值来自 C++。
游戏默认设置保存到项目 `project.json`，玩家选择与编辑器窗口／布局保存在本机用户目录。
项目的 `.comet/` 保存可重建缓存、日志和编辑器会话。

默认角色／基础动作、共用字体和基础 UI 图标位于 `engine/resources/`，沿用仓库的 Git LFS 规则。
引擎还提供默认 PBR／无光照材质，可在 Inspector 的材质下拉框选择；项目材质、页面、主题和玩法素材放在项目的 `assets/`。
示例货车来自 Cesium 的 [Cesium Milk Truck](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMilkTruck)，采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。
资源路径、设置存储和脚本接口的具体用法见[使用手册](docs/usage.md)。

## 目录导航

| 目录／文件 | 内容 |
| --- | --- |
| `engine/src/` | 运行时、Gameplay、物理、音频、渲染及通用能力 |
| `engine/shaders/`、`engine/resources/` | 引擎 Shader、默认角色／动作、字体与 UI 图标 |
| `editor/` | 场景／资产工作流、面板与 ImGui 适配 |
| `app/` | 通用项目运行入口 |
| `demo/` | 随仓库提供的示例项目 |
| `tools/` | Shader 编译、资产准备、下载与性能基准 |
| `tests/` | CPU、UI、GPU 集成及故障回归 |
| `config.json` | 开发者 Profile |
| `docs/` | 使用手册、架构与演进路线图 |

## 深入阅读

- [使用手册](docs/usage.md)：项目与场景操作、输入／设置、Lua／UI、Shader、诊断与性能测量。
- [架构与所有权](docs/architecture/overview.md)：模块依赖、执行阶段、线程交接和资源生命周期。
- [路线图](docs/engine-roadmap.md)：待实现能力、优化取舍和验收条件。

C++ 遵循根目录 `.clang-format`；新增源码与测试同步维护所属 CMake 清单。完整回归使用 `ctest --preset dev-debug`。
CI 完整构建并测试 Release，使用编译缓存与预编译头。
