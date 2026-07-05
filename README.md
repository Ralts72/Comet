# Comet 引擎

Comet 是一个基于 C++20、CMake 和 Vulkan 的引擎/编辑器项目。当前项目重点是图形资源封装、基础渲染流程、ImGui 编辑器面板和
GoogleTest 测试基础。

## 项目结构

- `engine/`：引擎核心库，包含 `core/`、`graphics/`、`render/`、`runtime/` 和 `common/` 模块。
- `editor/`：ImGui 编辑器入口和面板，包括层级、检查器、项目、视图和日志窗口。
- `app/`：运行时示例程序入口。
- `tests/`：GoogleTest 测试，覆盖数学、配置、导出和基础集成行为。
- `engine/assets/`：配置、纹理和 GLSL shader 资源。
- `3rdparty/`：第三方依赖目录，部分依赖通过 Git submodule 拉取，Vulkan Memory Allocator 和 EnTT 以 vendored
  源码形式维护；EnTT 只提交 single header。

## 环境依赖

- CMake 3.31 或更新版本。
- 支持 C++20 的编译器。
- Vulkan 开发环境和 `glslangValidator`，用于编译 shader。
- Git submodules，用于拉取 `glfw`、`glm`、`googletest`、`imgui`、`spdlog`、`stb_image` 和 `yaml-cpp`；
  `3rdparty/VulkanMemoryAllocator/` 和 `3rdparty/entt/` 直接随仓库提交。

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
ctest --test-dir build --output-on-failure
```

## 开发说明

主配置文件位于 `engine/assets/config.yaml`。运行入口在 `engine/src/runtime/runtime.h` 中创建局部 `Config` 对象，调用
`config.load_runtime_config()` 将 YAML 解析为 `Config::Runtime`，再把窗口、Vulkan、渲染和日志配置传入对应模块；`Config`
对外只作为运行时配置加载入口，底层渲染资源不直接读取原始配置。Vulkan 内存分配由 `engine/src/graphics/vk_allocator.h`
封装，`Device` 独占持有 allocator，`Buffer` 和 `Image` 只通过该封装创建、映射和释放 VMA 资源。关闭时先释放引擎资源，再关闭日志系统。Shader
源文件位于 `engine/assets/shaders/glsl/`，构建时由 CMake 调用 `glslangValidator`
编译。贡献者和智能体协作规范见 [AGENTS.md](./AGENTS.md)。
