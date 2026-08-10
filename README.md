# Comet 引擎

Comet 是一个基于 C++20、CMake 和 Vulkan 的引擎/编辑器项目。当前项目重点是图形资源封装、基础渲染流程、ImGui 编辑器面板和
GoogleTest 测试基础。

## 项目结构

- `engine/`：引擎核心库，包含 `asset/`、`core/`、`graphics/`、`render/`、`runtime/`、`scene/` 和
  `common/` 模块；`asset/` 当前提供轻量、不透明的 `AssetHandle` 和带类型校验的最小内存
  `AssetRegistry`。
- `editor/`：ImGui 编辑器入口和面板；Hierarchy 以父子树展示 Scene 并支持拖拽调整层级，Inspector 通过基于
  `EntityId` 的 Selection 编辑实体，SceneView/GameView 已显示离屏场景纹理。
- `app/`：运行时示例程序入口。
- `tests/`：GoogleTest 测试，覆盖数学、配置、导出、Scene/ECS、场景序列化、资源参数保护和基础集成行为。
- `engine/assets/`：配置、纹理和 GLSL shader 资源。
- `3rdparty/`：第三方依赖目录，部分依赖通过 Git submodule 拉取，Vulkan Memory Allocator 和 EnTT 以 vendored
  源码形式维护；EnTT 只提交 single header。

## 环境依赖

- CMake 3.31 或更新版本。
- 支持 C++20 的编译器。
- Vulkan 开发环境和 `glslangValidator`，用于编译 shader。
- Git submodules，用于拉取 `glfw`、`glm`、`googletest`、`spdlog` 和 `yaml-cpp`；
  `imgui`、`stb_image`、`3rdparty/VulkanMemoryAllocator/` 和 `3rdparty/entt/` 直接随仓库提交。

初始化依赖：

```bash
git submodule update --init --recursive
```

## 构建与运行

配置并构建 Release：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

默认只构建引擎和运行时示例程序。可通过以下选项按需启用其他目标：

- `COMET_BUILD_APP`：构建运行时示例程序，默认 `ON`。
- `COMET_BUILD_EDITOR`：构建 ImGui 编辑器，默认 `OFF`。
- `COMET_BUILD_TESTS`：构建 GoogleTest 测试，默认 `OFF`。

完整构建 app、editor 和 tests（Debug，不启动程序）：

```bash
./build.sh
```

运行 Debug 编辑器：

```bash
./debug.sh
```

运行 Release 示例程序：

```bash
./release.sh
```

## 测试

项目测试目标为 `unit_testing`，通过 CTest 运行：

```bash
./build.sh
ctest --test-dir build --output-on-failure
```

## 开发说明

主配置文件位于 `engine/assets/config.yaml`。运行入口在 `engine/src/runtime/runtime.h` 中通过局部 `ConfigLoader` 将 YAML
解析为纯数据 `Config`，再把窗口、Vulkan、渲染和日志配置传入对应模块；yaml-cpp 只存在于加载器实现中，底层渲染资源
不直接读取原始配置。Vulkan Validation 在 Debug 构建中默认开启、Release 和 CI 构建中默认关闭；需要临时覆盖时可在
`debug.enable_validation` 中配置，运行期 Debug Utils Messenger 由 `Context` 持有。`render.max_anisotropy` 表达项目期望的
过滤倍率，`1` 表示关闭；`engine/src/graphics/vk_capability.h` 集中处理物理设备的队列、扩展、Surface、格式和 Feature
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
`SceneSerializer` 使用带版本字段的 `.scene` YAML 保存 Name、Transform、MeshRenderer、Camera 和父 UUID 引用；
运行时 `EntityId`、EnTT handle 与派生世界矩阵不会落盘。实体按 UUID 排序，加载时会拒绝重复 UUID、悬空父引用、
层级环、未知字段和不支持的版本。
`SceneExtractor` 将可渲染实体和 Camera 复制为
`engine/src/render/render_scene.h` 定义的 CPU 侧快照，其中只包含实体 ID、矩阵、Camera 参数和资源 Handle。
`Engine` 使用唯一所有权持有 Scene 和最小 Asset Registry。app 在初始化阶段注册 demo mesh/material，并创建
主 Camera 与两个具有不同 Transform 的 cube entity；`SceneResolver` 选择并校验主 Camera、根据渲染尺寸
生成 view/projection，同时将 Handle 解析为运行时 `RenderSubmission`。`Renderer` 负责编排帧流程，
`SceneRenderer` 管理 per-frame UBO、材质 descriptor，并通过
push constant 提交每个 draw 的模型矩阵。editor 初始化由 Engine 持有的 Scene；Hierarchy 可创建、递归删除、选择
实体，并以拖拽方式 reparent；Inspector 直接编辑选中实体的 Name 与 Transform，Selection 仅保存 `EntityId` 并在访问时
向 Scene 重新解析实体。editor 中的场景按 frame slot 渲染到可采样的离屏目标，再由 ImGui 合成到 swapchain；
runtime app 仍直接渲染到 swapchain。SceneView 和 GameView 当前共享场景主 Camera 的输出。关闭时先释放引擎资源，
再关闭日志系统。Shader
源文件位于 `engine/assets/shaders/glsl/`，构建时由 CMake 调用 `glslangValidator`
编译。贡献者和智能体协作规范见 [AGENTS.md](./AGENTS.md)。

渲染资源的所有权、析构顺序和非拥有依赖约束见
[渲染资源所有权](./docs/architecture/rendering-ownership.md)；场景持久化契约见
[场景文件格式](./docs/architecture/scene-format.md)。
