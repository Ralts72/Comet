# Comet 引擎

Comet 是一个使用 C++20、CMake 和 Vulkan 开发的实验性 3D 引擎与编辑器。项目目前围绕 Scene/ECS、资产导入、
Vulkan 渲染资源管理和 ImGui 编辑器工作流持续演进。

## 项目结构

- `engine/`：共享引擎库；源码位于 `engine/src/`，GLSL Shader 位于 `engine/shaders/`。
- `editor/`：ImGui 编辑器入口、面板和编辑器私有资源；Project 会自动刷新资产源文件树，并提供资产移动与重命名入口。
- `app/`：Runtime 示例程序。
- `assets/`：项目源资产及其 `.meta`；资产身份随源码进入版本控制。
- `config/`：共享配置与 `dev-debug`、`editor-dev`、`app-release` Profile。
- `.comet/`：本机缓存和编辑器状态，不进入版本控制。
- `tests/`：GoogleTest 单元测试和集成测试。
- `3rdparty/`：Submodule 与仓库内维护的第三方依赖。

## 环境依赖

- CMake 3.31 或更新版本。
- 支持 C++20 的编译器。
- Vulkan SDK 与 `glslangValidator`。
- Git LFS 与 Git Submodule。

初始化依赖：

```bash
git lfs install
git lfs pull
git submodule update --init --recursive
```

## 构建与运行

| Profile | 构建类型 | 目标 | 常用入口 |
| --- | --- | --- | --- |
| `dev-debug` | Debug | app、editor、tests | `./build.sh` |
| `editor-dev` | RelWithDebInfo | editor | `./editor.sh` |
| `app-release` | Release | app | `./release.sh` |

脚本分别使用对应的 CMake Preset。也可以直接执行：

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug --parallel
ctest --preset dev-debug
```

手动配置时，可通过 `COMET_BUILD_APP`、`COMET_BUILD_EDITOR` 和 `COMET_BUILD_TESTS` 组合目标，并必须显式指定
`COMET_CONFIG_PROFILE`。`COMET_NATIVE_OPTIMIZATION` 仅适合本机构建，不应为可分发二进制启用。

C++ 代码格式由根目录 `.clang-format` 统一，默认列宽为 90；多参数声明和调用在需要换行时会继续成组排列，不会强制
每个参数独占一行。提交前只需对本次改动的 C++ 文件运行 `clang-format -i <files...>`，不要求为单次功能改动重排整个仓库。

## 开发说明

- app 与 editor 共同链接 `engine`。app 直接运行 Runtime；editor 额外管理 Edit/Play Scene、Selection、面板和离屏 Viewport。
- Viewport 通过纯值 `RenderView` 传递可见性、稳定后的物理像素目标尺寸和 Camera 选择方式。Edit 使用不进入 Scene 与序列化的
  editor camera，Play 使用 Runtime Scene 的 primary Camera；`SceneRenderer` 不感知 `EditorMode` 或 ImGui 状态。`ViewPanel`
  通过纯布局计算分离 ImGui 逻辑内容区、结合当前窗口 framebuffer scale 的渲染分辨率，以及保持纹理宽高比的屏幕显示矩形；
  Play 可独立选择 Free、16:9 或固定像素分辨率，并以 Fit 或 1x 显示。最终物理尺寸会按比例限制在设备
  `maxImageDimension2D` 和 editor 4096 软上限以内，Renderer 只接收约束后的结果。Viewport resize 会先完整创建新的
  离屏目标再切换；旧目标和对应 ImGui 绑定按 frame slot 保留到 fence 完成，不再等待全部在途帧。
- 场景渲染主链路为 `Scene -> SceneExtractor -> RenderScene -> SceneResolver -> RenderSubmission -> SceneRenderer`。
  Scene 只保存组件和 `AssetHandle`，不持有 GPU Resource。
- 资产主链路为 `assets + .meta -> AssetDatabase -> ImportService -> Artifact -> AssetManager -> AssetRegistry`。
  glTF 只由导入链路读取并原子发布 Mesh Artifact，`AssetManager::load_mesh()` 只消费 Artifact，不会回退解析源文件；
  `.comet/cache/` 中的导入产物可以重建，不属于源资产。GPU 创建失败时不会发布不完整的 Runtime 资产，
  已加载 Mesh/Texture 的扫描刷新会在 Worker 生成 CPU 候选，再由 Owner Thread 验证 revision、创建 GPU 资源并发布；
  后台处理期间及刷新失败时都会保留上一份有效对象。Editor 低频监视 `assets/` 文件树，只有快照变化时才触发同一个
  `AssetManager::scan()`，不会在监视器中直接导入或修改 Registry。
  Handle 可随资产移动保持稳定，但对应的 AssetType 不可改变；类型转换必须分配新 Handle，否则数据库拒绝候选快照。
  `AssetManager::move_asset()` 会把源文件与相邻 `.meta` 作为同一事务移动，扫描无法形成可信快照时回滚文件和数据库候选状态。
  Project 面板只提交选中的 Handle 和 `assets/` 相对目标路径；成功后保持选中状态，并同步 Inspector、Log 和资产源监视器。
  Mesh Artifact 保存主 glTF 和外部 buffer 的内容快照，导入服务据此判断是否需要重建；Runtime 加载不检查源文件。
- Graphics 后端使用 Vulkan 1.3、VMA、Synchronization 2、Timeline Semaphore、FrameScheduler 和 UploadManager。
- Swapchain 重建当前保留 Device idle 安全基线，但会先释放 runtime/ImGui framebuffer target。
  core handle、borrowed images、config 和 current index 会先组成完整 `Swapchain::Generation` 候选，
  成功后才整体替换 active owner；创建失败或窗口最小化延期时保留旧 core 并恢复 dependent。
  Vulkan 类型应限制在 Graphics 后端及明确需要底层能力的 Render 实现中。
- 世界与编辑器坐标约定 `+Y` 向上；Vulkan Viewport 使用负高度完成画面坐标转换。Texture 是否翻转仅由对应
  `.meta` 的 `flip_y` 导入设置决定，不用于修正世界坐标。
- CMake 只编译 `engine/shaders/CMakeLists.txt` 中显式列出的 Shader；`.spv` 和嵌入式 C++ Header 都生成到构建目录。
- 运行配置采用“编译期能力 + Profile 运行时策略”：Logger 在所有构建中保留基础级别，Profiler 只在 Debug 和
  RelWithDebInfo 中编译，Vulkan Validation 由配置按运行环境启用。

## 设计文档

- [长期开发路线图](./docs/engine-roadmap.md)
- [渲染资源所有权](./docs/architecture/rendering-ownership.md)
- [资产管线边界](./docs/architecture/asset-pipeline.md)
- [场景文件格式](./docs/architecture/scene-format.md)

贡献约定见 [AGENTS.md](./AGENTS.md)。
