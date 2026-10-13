# Comet 引擎

Comet 是供作者个人学习使用的 3D 游戏引擎与编辑器，使用 C++20、CMake 和 Vulkan。
引擎对外提供一个 `engine` 动态库；ImGui 编辑器与独立 App 共用场景、资产、Gameplay 和 RmlUi 游戏界面。

目前支持场景编辑与撤销、glTF 静态／骨骼模型、基础动画播放与混合、PBR／阴影／IBL／Bloom、Lua 热重载、刚体与角色控制、基础音频和游戏设置。
独立渲染线程与场景更新重叠执行。Prefab、高级动画、粒子、GI、路径追踪及内容导出继续按路线图推进。

## 快速开始

需要 CMake 3.31+、C++20 编译器、Vulkan SDK 和 Git LFS。第三方源码由 Submodule 或仓库内的 vendored source 提供，
Shader 编译器随项目构建，无需另装 `glslangValidator`。

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

| Preset | 用途 | 构建目录 |
| --- | --- | --- |
| `dev-debug` | App、Editor、测试与基准，Debug | `build/` |
| `editor-dev` | Editor，RelWithDebInfo | `build-editor/` |
| `app-release` | App，Release | `build-release/` |

Windows 可使用相同 CMake preset，随后启动构建目录中的可执行文件。开发构建依赖仓库资源，独立发布包仍在规划中。

## 试玩 Demo

默认启动 `demo/assets/scenes/character.scene`。WASD／左摇杆移动，空格／手柄 South 跳跃，R 重开；
移动以相机朝向为参照；Shift 跑步、Ctrl 蹲伏，鼠标右键拖动／手柄右摇杆转动视角，滚轮缩放。
引擎内置人形角色和 7 个基础动作；Inspector 可替换网格和动作片段。沿台阶、缓坡和移动平台收集三个目标，低通道需要蹲伏。
点击“设置”或按 F1 可修改显示、画质、音量和按键。

编辑器恢复上次打开的场景。通过“文件 → 切换场景”可选择角色场景，或打开 `scenes/default.scene` 查看材质、灯光与关节演示。
可选 HDR 环境图通过 `./tools/download_assets.sh` 下载；角色场景不依赖这项下载。

## 常用操作

| 任务 | 入口 |
| --- | --- |
| 新建／打开项目 | 文件菜单，项目包含 `project.json` 和 `assets/` |
| 切换同项目场景 | 文件 → 切换场景；有未保存修改时先确认 |
| 设置 App 启动场景 | 项目 → 启动场景 |
| 运行／暂停／单步／停止 | 视口左侧按钮；Stop 恢复 Edit 内容 |
| 编辑实体与组件 | 层级与 Inspector；Edit 支持撤销 |
| 创建材质、脚本或 Lua 模块 | Project 面板右键 |
| 编辑 Lua、Shader、UI 源码 | Project 右键“打开源码”，编辑保存后按对应重载流程生效 |
| 设置新玩家默认值 | 项目 → 设置 → 显示／画质／音频／输入 |
| 查看渲染数据与日志 | 渲染统计、Log 面板 |

## 配置与资源

根目录 `config.json` 保存三个开发者 Profile 的诊断、底层设备参数与资源预算覆盖；启动入口按 preset 选择分组，缺省值来自 C++。
游戏默认设置保存到项目 `project.json`，玩家选择与编辑器窗口／布局保存在本机用户目录。
项目的 `.comet/` 保存可重建缓存、日志和编辑器会话。

默认角色／基础动作、共用字体和基础 UI 图标位于 `engine/resources/`，沿用仓库的 Git LFS 规则。
引擎还提供默认 PBR／无光照材质，可在 Inspector 的材质下拉框选择；项目材质、页面、主题和玩法素材放在项目的 `assets/`。
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
