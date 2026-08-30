# Comet 引擎

Comet 是一个基于 C++20、CMake 和 Vulkan 的引擎/编辑器项目。当前项目重点是图形资源封装、基础渲染流程、ImGui 编辑器面板和
GoogleTest 测试基础。

## 项目结构

- `engine/`：引擎核心库，包含 `asset/`、`core/`、`graphics/`、`render/`、`runtime/`、`scene/` 和
  `common/` 模块；`asset/` 当前提供轻量、不透明的 `AssetHandle` 和带类型校验的最小内存
  `AssetRegistry`，并定义持久化资产身份、类型化 Importer 设置、`.meta` 编解码及源资产索引；`core/project_paths` 定义项目根目录与
  `assets/`、`Library/`、`ProjectSettings/` 的标准路径契约。
- `editor/`：ImGui 编辑器入口和面板；Hierarchy 以父子树展示 Scene 并支持拖拽调整层级，Inspector 通过组件描述符
  编辑 Transform、MeshRenderer 和 Camera，File 菜单可新建、打开和保存 `.scene`；`editor/resources/` 保存不进入
  项目资产数据库的编辑器私有字体等资源。
- `app/`：运行时示例程序入口。
- `assets/`：当前示例项目的源资产及相邻 `.meta`，当前包含 demo Texture 和 Material；资产身份进入版本控制。
- `config/`：`common.yaml` 保存共享运行配置，`profiles/` 保存构建/启动环境覆盖。
- `Library/`：可重建的导入产物与缓存目录，不进入版本控制。
- `tests/`：GoogleTest 测试，覆盖数学、配置、Scene/ECS、场景序列化、资源参数保护和渲染数据链路。
- `engine/shaders/`：引擎自有 GLSL Shader 源码，构建时编译并嵌入引擎。
- `3rdparty/`：第三方依赖目录，部分依赖通过 Git submodule 拉取，Vulkan Memory Allocator 和 EnTT 以 vendored
  源码形式维护；EnTT 只提交 single header。

## 环境依赖

- CMake 3.31 或更新版本。
- 支持 C++20 的编译器。
- Vulkan 开发环境和 `glslangValidator`，用于编译 shader。
- Git LFS，用于拉取 Texture、字体等二进制资源。
- Git submodules，用于拉取 `glfw`、`glm`、`googletest`、`spdlog` 和 `yaml-cpp`；
  `imgui`、`stb_image`、`3rdparty/VulkanMemoryAllocator/` 和 `3rdparty/entt/` 直接随仓库提交。

初始化依赖：

```bash
git lfs install
git lfs pull
git submodule update --init --recursive
```

## 构建与运行

项目通过 `CMakePresets.json` 维护三种常用构建组合：

- `dev-debug`：Debug 构建 app、editor 和 tests，用于开发、调试与测试。
- `editor-dev`：RelWithDebInfo 只构建 editor，用于日常高性能编辑并保留调试符号和可按配置启用的 Profiler。
- `app-release`：Release 只构建 app，用于运行时性能与发布行为验证。

完整构建 app、editor 和 tests（Debug，不启动程序）：

```bash
./build.sh
```

构建并运行 RelWithDebInfo 编辑器：

```bash
./editor.sh
```

构建并运行 Release 示例程序：

```bash
./release.sh
```

脚本分别调用同名用途的 CMake preset，也可以直接使用 `cmake --preset <name>` 和
`cmake --build --preset <name> --parallel`。底层目标仍可通过以下选项独立组合：

- `COMET_BUILD_APP`：构建运行时示例程序，默认 `ON`。
- `COMET_BUILD_EDITOR`：构建 ImGui 编辑器，默认 `OFF`。
- `COMET_BUILD_TESTS`：构建 GoogleTest 测试，默认 `OFF`。
- `COMET_CONFIG_PROFILE`：选择 `dev-debug`、`editor-dev` 或 `app-release` 运行配置层；必须显式指定，三个 preset 已分别绑定同名 profile。
- `COMET_NATIVE_OPTIMIZATION`：增加面向构建机器 CPU 的优化，默认 `OFF`；需要分发的二进制不应开启。

不使用 preset 手动配置时，必须同时传入 profile，例如
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCOMET_CONFIG_PROFILE=dev-debug`。

## 测试

项目测试目标为 `unit_testing`，通过 CTest 运行：

```bash
./build.sh
ctest --preset dev-debug
```

## 开发说明

运行配置位于 `config/`：`common.yaml` 保存窗口、Vulkan 和渲染的共享设置，`profiles/` 下的配置按顺序覆盖
共享层。运行时启动层根据目标的默认 profile 选择配置文件，`ConfigLoader` 只负责合并显式传入的 YAML 并最终校验为
纯数据 `Config`，再由 `Application` 和 `Engine` 消费；yaml-cpp 只存在于加载器实现中，底层渲染资源不直接读取原始配置。

`diagnostics` 配置域负责日志、Profiler 和 Vulkan Validation 的运行时策略。日志在所有构建中保留
Info/Warn/Error/Fatal，Debug/Trace 只在 Debug 中编译，运行时再由 `diagnostics.log_level` 过滤。Profiler 代码只在
Debug 和 RelWithDebInfo 中编译，并由 `diagnostics.enable_profiler` 决定运行时是否采样；默认 `dev-debug` 开启，
`editor-dev` 关闭。`app-release` 不编译 Profiler，因此对应 profile 不提供 `enable_profiler` 字段，并使用安全默认值
`false`。`Application` 持有 `Diagnostics` 生命周期对象，由它统一完成 Logger/Profiler 的能力协商与关闭；`Engine`
销毁后再汇总 Profiler 数据，最后关闭 Logger。Validation 默认只在 `dev-debug` profile 开启，运行期 Debug Utils Messenger 由
`Context` 持有。三个 profile 暂时都关闭文件日志；发布日志迁移到用户数据目录并加入轮转策略后，再为 `app-release`
开启。`render.max_anisotropy` 表达项目期望的过滤倍率，`1` 表示关闭；

诊断开关遵循“宏控制编译期能力，配置控制运行时策略”：宏用于从二进制中移除 Debug/Trace 日志调用点和 Profiler
采样代码，YAML 只能启用当前构建已经具备的能力。Validation 不需要条件编译，C++ 默认关闭，由 profile 明确决定是否
在启动时请求 Validation Layer；Release 即使配置为开启，也只会在系统存在对应 Layer 和 Debug Utils 时生效。

`engine/src/graphics/vk_capability.h` 集中处理物理设备的队列、扩展、Surface、格式和 Feature
能力检查，以及 Swapchain Request 到最终 Config 的选择；设备按类型与可用能力评分，实际值限制在设备上限内。项目要求
Vulkan 1.3，启动时同时检查 loader 与 PhysicalDevice 版本。Vulkan format、color space 和 present mode 在 YAML 中使用
`bgra8_srgb`、`srgb_nonlinear`、`immediate` 等稳定名称，加载后保存为 Comet 强类型枚举。Queue 提交使用
Synchronization 2，每个 wait/signal 显式携带 semaphore value 和 pipeline stage；当前帧同步仍使用 binary semaphore。
Vulkan 内存分配由 `engine/src/graphics/allocator.h`
封装，`Device` 独占持有 `Allocator`，`Buffer` 和 `Image` 通过 `AllocationUsage` 表达显存用途并以 `Allocation` 保存
VMA allocation 句柄；per-frame `CPUBuffer` 使用 persistent mapping 和范围写入。Swapchain 根据实时 Surface capability
和 framebuffer 像素尺寸选择 extent、transform、alpha、usage 与 present mode，窗口最小化时暂停更新并延迟重建。
`engine/src/scene/`
提供基于 EnTT 的 Scene/Entity 和基础组件数据模型；`TransformComponent` 的 Euler 角使用度，局部矩阵顺序为
`T * Rz * Ry * Rx * S`。组件只存储 TRS 数据，并提供不引入额外状态的 `rotate()` 和 `to_matrix()` 值操作；
底层 `Math::wrap_degrees()` 与 `Math::compose_trs()` 会将旋转规范到 `[-180, 180)`，避免长期动画中的角度累积和
大数精度损失。运行时自增 `EntityId` 用于当前 Scene 内查询和 Selection；`UuidComponent` 保存 128-bit
version 4 `EntityUuid`，作为跨保存/加载稳定的实体身份，并支持标准字符串解析、格式化和哈希查询。
`RelationshipComponent` 只保存父实体 ID，子节点由 Scene 查询；Scene 阻止循环层级、递归销毁
子树，并在每次场景提取前按父级优先顺序全量更新 `WorldTransformComponent`。reparent 保持局部 TRS，世界矩阵随
新父节点重新计算；增量 dirty 更新留到 TransformSystem 建立后实现。
`ComponentRegistry` 使用显式 stable ID、pointer-to-member 访问器和属性标记描述 Transform、MeshRenderer 与 Camera；
editor 的 `PropertyEditorRegistry` 按 Bool、Float、Vec3 和 AssetHandle 类型分发控件，Inspector 不再包含组件专用分支。
editor 将同一个 `ComponentRegistry` 提供给 Inspector 和 `SceneSerializer`；serializer 按 stable ID、属性类型和
`serializable` 标记读写带版本字段的 `.scene` YAML，同时显式管理 Name、Entity UUID 与父 UUID 引用；
运行时 `EntityId`、EnTT handle 与派生世界矩阵不会落盘。实体按 UUID 排序，加载时会拒绝重复 UUID、悬空父引用、
层级环、未知字段和不支持的版本；保存时会自动创建缺失的父目录。
`SceneExtractor` 将可渲染实体和 Camera 复制为
`engine/src/render/render_scene.h` 定义的 CPU 侧快照，其中只包含实体 ID、矩阵、Camera 参数和资源 Handle。
`Engine` 使用唯一所有权持有当前活动 Scene 和最小 Asset Registry，并可在保持唯一所有权的前提下交换 Scene。
app/editor 组合根持有 `AssetManager`，它使用 `AssetDatabase` 将稳定 Handle 解析为项目源资产；Texture 由
`TextureImporter` 按 `.meta` 中经过校验的色彩空间和垂直翻转设置解码为 CPU 像素数据，Material 由
`MaterialSerializer` 读取为只含模板名和 Texture Handle 的
`MaterialData`。外部格式的导入器集中在 `engine/src/asset/import/`，Comet 原生资产与元数据的序列化器集中在
`engine/src/asset/serialization/`。`AssetManager` 递归解析材质依赖、组装运行时 Material 并统一发布到 `AssetRegistry`；
`ResourceManager` 只创建 Device 相关的 Texture/Mesh 和维护 Shader/Sampler 等渲染侧共享资源，不再缓存资产 Handle。
Project 面板展示真实索引与扫描问题。app 在初始化阶段只手工注册尚未资产化的 demo mesh，材质及其纹理由项目资产链路加载，并创建
主 Camera 与两个具有不同 Transform 的 cube entity；`SceneResolver` 选择并校验主 Camera、根据渲染尺寸
生成 view/projection，同时将 Handle 解析为运行时 `RenderSubmission`。`Renderer` 负责编排帧流程，
`SceneRenderer` 管理 per-frame UBO、材质 descriptor，并通过
push constant 提交每个 draw 的模型矩阵。editor 初始化由 Engine 持有的 Scene；Hierarchy 可创建、递归删除、选择
实体，并以拖拽方式 reparent；Inspector 直接编辑 Name，并通过描述符编辑基础组件，Selection 仅保存 `EntityId` 并在访问时
向 Scene 重新解析实体。File 菜单通过路径输入执行 New/Open/Save；Open 先完整加载再替换 Engine 中的 Scene，
New/Open 都会重绑定 Hierarchy 与 Selection 并清除旧选择。editor 中的场景按 frame slot 渲染到可采样的离屏目标，再由 ImGui 合成到 swapchain；
Play 时通过 `SceneSerializer` 创建独立 Runtime Scene，并暂存原 Edit Scene；Stop 会丢弃运行时修改、恢复 Edit Scene，
重新绑定面板并清空 Selection，Play 期间禁用场景文件操作。editor 只保留一个 `ViewportPanel`：Edit 时显示编辑工具，
Play 时同一面板切换为运行画面并隐藏编辑工具。当前两种模式仍使用活动 Scene 的主 Camera，独立 editor camera 留在后续视口阶段；
runtime app 仍直接渲染到 swapchain。关闭时先释放引擎资源，
再关闭日志系统。Shader
源文件位于 `engine/shaders/glsl/`，构建时由 CMake 调用 `glslangValidator`
编译。贡献者和智能体协作规范见 [AGENTS.md](./AGENTS.md)。

渲染资源的所有权、析构顺序和非拥有依赖约束见
[渲染资源所有权](./docs/architecture/rendering-ownership.md)；资产索引、导入与运行时发布边界见
[资产管线边界](./docs/architecture/asset-pipeline.md)；场景持久化契约见
[场景文件格式](./docs/architecture/scene-format.md)。
