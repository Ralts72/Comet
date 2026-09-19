# Repository Guidelines

## Project Structure & Module Organization

Comet is a C++20 CMake project. `engine/` builds the shared `engine` library and contains core runtime, graphics,
render, and common utilities under `engine/src/`; engine-owned GLSL sources live in `engine/shaders/`. Runtime config
lives in `config/`, sample project assets in `demo/assets/`, and editor-private resources in `editor/resources/`. `app/`
builds the sample runtime executable from `app/main.cpp`. `editor/src/` groups core logic and panels by feature:
`scene/`, `viewport/`, `assets/`, `inspector/`, `render/`, and `ui/`. `tests/` builds CPU, integration, and isolated recovery tests. `3rdparty/`
contains third-party dependencies; most are submodules, while `3rdparty/VulkanMemoryAllocator/` and `3rdparty/entt/`
are vendored source. EnTT is vendored as a single header under `3rdparty/entt/entt.hpp`.
Avoid editing third-party code unless updating a dependency.

## Build, Test, and Development Commands

- `git submodule update --init --recursive`: fetch submodule dependencies; Vulkan Memory Allocator and EnTT are already
  vendored under `3rdparty/VulkanMemoryAllocator/` and `3rdparty/entt/`.
- `cmake --preset dev-debug`: configure the full Debug development build.
- `cmake --build --preset dev-debug --parallel`: build `engine`, `app`, `editor`, and tests.
- `./build.sh`: configure a full Debug build in `build/` with `app`, `editor`, and tests enabled, then build every target.
- `ctest --preset dev-debug`: run discovered GoogleTest tests from the Debug development build.
- `./editor.sh`: configure/build the RelWithDebInfo editor in `build-editor`, then launch it.
- `./release.sh`: configure/build `build-release`, then launch `build-release/app/app`.

The engine requires the Vulkan SDK. CMake builds `comet_shader_compiler` from the glslang submodule;
an external `glslangValidator` is not required. Linux CI provides Xvfb for GLFW tests.

## Coding Style & Naming Conventions

Use C++20, four-space indentation, and the existing brace style. Classes and fixtures use `PascalCase`; functions and
methods use `snake_case`; member fields use the `m_` prefix. Keep public headers under the owning module's `src/` tree
and pair implementation files as `.h`/`.cpp` where practical. C++ code must follow the root `.clang-format`. Keep
comments short and useful.

## Testing Guidelines

Tests use GoogleTest and collect `.cpp` sources under `tests/`. Name new files `test_<feature>.cpp`
and keep them near their scope. Editor CPU tests belong in `tests/editor/core/`; UI integration tests belong in
`tests/editor/ui/`. Shared fixtures live in `tests/support/`. Prefer small fixtures that preserve independent assertions. Run
`ctest --test-dir build --output-on-failure` before opening a PR.

## Commit & Pull Request Guidelines

Recent history uses short Conventional Commit-style subjects such as `feat: add material` and `fix for ci`; prefer
`feat:`, `fix:`, `test:`, or `docs:` with an imperative summary. PRs should describe the change, list validation
commands, link related issues, and include screenshots or logs for editor, rendering, or UI changes.

## Agent-Specific Instructions

Communicate with users in Chinese. Keep `README.md` in Chinese. After each repository change, review `README.md`; edit
it only when the change affects project description, architecture, setup, usage, commands, or contributor workflow. Do
not use `README.md` as a changelog or temporary document index. This repository is indexed by CodeGraph: use it first
for code questions, then verify exact build/config/script details from files. Do not overwrite an existing `AGENTS.md`
without an explicit user request.
