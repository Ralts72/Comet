# 005：在 Project 中创建 Lua 模块

## 背景

002 已接通 `require("scripts.demo_score")`、传递依赖与关联重载，但模块只能由外部工具创建。
Project 的 New Script 始终创建可挂载组件及 `.meta`，还会明确拒绝 `.module.lua`。
缺少的不是第二套脚本运行时，而是把已有源码模块语义接入现有文件创建流程。

本项增加 New Lua Module，不增加资产类型、模块 Manager、脚本 VM 或场景命令。

## 前后调用链

之前：

```text
New Script → CreateScriptRequest{destination}
           → EditorAssets::create_script
           → AssetSourceOperations::create_script
           → 发布 .lua 与 .meta → 候选扫描 → 选择 Script 资产

.module.lua → 只能在外部源码编辑器创建
```

现在：

```text
New Script / New Lua Module
  → CreateScriptRequest{destination, kind}
  → EditorAssets::create_script(destination, kind)
  → 同一个文本文件创建事务
      Component：发布源码 + metadata，验证并选择 Script 资产
      Module：仅发布源码，验证候选扫描，不改变原资产选择
  → 原有依赖更新与资源恢复流程
```

组件仍然是可分配给实体的资产；模块是组件代码的源码依赖。模块创建不能伪造 Handle，也不能
调用 `Script::create` 将返回 table 的模块冒充脚本组件。它的合法性在实际入口加载时和依赖组一起验证。

## 代码变化

### 1. 创建请求携带源码种类

原请求只有路径，默认 `.lua` 及 Script 身份隐含在后端：

```cpp
struct CreateScriptRequest {
    std::filesystem::path destination;
};
```

现在显式携带种类，原调用默认值不变：

```cpp
struct CreateScriptRequest {
    std::filesystem::path destination;
    AssetSourceOperations::ScriptKind kind = AssetSourceOperations::ScriptKind::Component;
};
```

`ProjectPanel` 仍只生成请求与显示结果；`Editor` 消费请求；`EditorAssets` 拥有索引和监听器，
完成文件操作后接回既有扫描结果处理。新增菜单没有直接操作运行 VM 或渲染资源。

同一个脚本弹窗根据种类显示后缀和标题。模块预览显示可复制的 `require` 表达式，并说明不可挂载。
失败保留弹窗和错误；成功关闭弹窗。只有数据库中确有资产记录时才改变资产选择，因此模块创建不会
清除当前实体或资产，也不会在 Inspector 出现一个虚假的 Script 资产。

### 2. 路径规则由运行时与编辑器共用

原来名称校验和点分路径转换仅在 VM 的 `prepare_module` 内。现在：

```cpp
const auto relative = Script::module_path(name);
if(!relative) {
    module_error = relative.error();
    return nullptr;
}
const auto& path = relative.value();
```

编辑器反向调用：

```cpp
const auto module_name = Script::module_name(destination);
```

例如 `scripts/shared/score.module.lua` 对应 `scripts.shared.score`。
不仅检查后缀，还将反向结果再映射为路径并比较，避免把 `scripts/foo.bar.module.lua`
误认为 `scripts/foo/bar.module.lua`。目录和名称都使用 ASCII 标识符，点分名称不超过 256 字节。
这两个函数只负责词法映射，不执行磁盘访问；事务和实际加载各自在自己的文件边界检查符号链接。

### 3. 扩展现有事务，而非复制一套模块创建器

原文本事务始终需要 `AssetType`，生成 metadata 并验证扫描后的 Handle。
现在资产类型可选：有类型时原组件／材质行为保留，没有类型时仅发布源码。

```text
检查名称、路径、父目录和同名冲突
  → 独占暂存目录，写入模板
  → 有资产类型时写入并发布 metadata
  → 不覆盖地发布源码
  → 对候选数据库进行完整扫描
  → 验证对应资产记录，或验证模块没有资产记录
  → 提交数据库；失败则回滚本次发布
```

模块和相邻同名 `.meta` 任一已存在都拒绝创建。父目录必须存在，且模块路径不能经过内部或外部
符号链接别名。暂存目录仍用独占标识，不把暂存标识登记为模块资产身份。

模块模板为：

```lua
local module = {}

return module
```

空 table 可由已有入口 require，但不会自动提供业务函数。需要什么函数由项目源码决定。

`EditorAssets` 对模块只确认源码变化，不确认不存在的 `.meta`；候选扫描仍交给 `accept_scan`，
所以先前因缺少模块而失败的组件可以沿原有依赖和引用恢复流程重新加载，而不是由 UI 特殊补救。

## 架构价值

- Engine 只新增既有模块语义的双向路径入口，没有引入编辑业务。
- Editor 的请求、文件事务、索引与资源恢复仍使用各自原有 owner。
- 组件／模块差异显式表达，不由 UI 临时猜后缀后伪造资产身份。
- 创建事务仍为材质、组件和模块共用；测试保留其冲突、扫描失败和回滚约束。
- 本项未引入新生产文件，也没有为了一个菜单增加 Manager 或回调层。

## 验证记录

本项首次构建与定向测试均通过，没有用重跑掩盖失败。

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='Script*:*ScriptAsset*:AssetSourceOperationsTest.*:EditorAssetsTest.*'
build/tests/editor_ui_testing --gtest_filter='ProjectPanelTest.*:AssetEditingUiTest.Project*'
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure -V
cmake --build --preset app-release --parallel 6
```

- Debug 全目标和 Release app 构建通过，无编译 warning/error。
- 定向 150 项 CPU、25 项 Project UI 全部通过。
- 完整 CPU 949 项通过，1 项 `AssetSourceMonitorTest.UnchangedFallbackDoesNotInvalidateDatabaseCandidate`
  因 macOS 使用原生监听而跳过；无窗口 UI 159 项通过。
- 同一次 CPU 标签回归包含 `shader_build_contract` 和 `module_boundaries`，均通过。
- 新增回归验证词法映射、冲突与符号链接、三类创建的失败回滚，以及缺失模块的两个消费者
  经 `EditorAssets::restore_references` 恢复后真实执行 Runtime，且模块可变 table 仍按实例隔离。
- UI 回归通过实际 ImGui 请求与响应验证两个菜单、显式后缀、修正输入、失败保留草稿、成功关闭，
  以及模块不改变资产选择、不产生资产拖拽数据。

阶段性 GPU 冒烟使用独立进程，外层 90 秒超时，串行执行：

```sh
build/tests/integration_testing \
  --gtest_filter='EngineRunTest.FrameContextRoutesInputWithoutCarryingItIntoTheNextFrame:EngineRunTest.PausedRuntimeKeepsHostAndRenderingAliveWhileStepRunsOnce:ViewportTest.RendersAcrossSceneChangesAndIgnoresPlayPicking' \
  -NSAutomaticWindowAnimationsEnabled NO
```

3 项通过、正常退出（测试主体约 1.3 秒），没有 Vulkan 校验错误。三个空场景的“无主相机”警告来自
这些测试的既有设置，不是加载失败。此证据验证 Engine 输入交付、暂停／单步仍呈现，以及 Viewport
跨场景呈现的生命周期；不代表 demo 的真实键鼠体验、声音或颜色已经人工确认。

原始日志：`/tmp/comet-auto3-005-first-{build,targeted,ui}.log`、
`/tmp/comet-auto3-005-final-{regression,release}.log`、`/tmp/comet-auto3-005-gpu-smoke.log`。

## 001–005 阶段性架构回顾

核对了脚本定义／实例、依赖索引、关联候选发布、阶段末组件命令、Runtime 输入 prepare，以及
Project 文件事务之间的 owner 和依赖方向。没有新增反向依赖、全局事件总线或平行资源缓存。
Script 的源码与字段定义不可变；VM 和模块可变状态按实例持有；文件事务不直接替换活动实例；
输入消费在 Runtime 边界进行，不由 demo 手动关闭其他组冒充优先级。

本轮没有为减少文件数量或包装成员再加新层。修正了旧测试／文档中把“返回 table 的组件模板”
称为“模块”的歧义，保留独立的行为断言。下一项应核对实际编写脚本时的辅助方法调用：
模板使用 `script:method()` 定义，但当前 `self:helper()` 没有方法查找关系，004 的 demo 曾遇到此问题。
这属于脚本编写契约，不是继续添加不相关 API 的理由。

## 手动验证

1. 在 demo 的 Project/scripts 目录右键 New Lua Module，输入 `shared_value`。
2. 查看引用为 `require("scripts.shared_value")`；创建后应出现 `shared_value.module.lua`，没有 `.meta`。
   原来选中的实体／资产保持不变，模块不能拖进实体 Script 槽位。
3. 在外部源码编辑器为模块加入函数，并在一个组件脚本顶层 require 后调用；运行 app 或 Play。
4. 在 Editor Play 修改模块，确认沿已有关联重载流程生效；保存语法错误应保留旧版。
5. 尝试使用包含空格、额外点、数字开头的名称，或重复创建同名模块，应失败且不覆盖已有内容。
6. New Script 仍创建普通组件与 `.meta` 并自动选择；模块不取代该入口。

## 限制与后续

- 模块内容编辑、重命名、删除暂由外部工具完成；不自动改写 require 字符串。
- Finder 仍不导入 Lua 多文件依赖包；此项没有改变其导入范围。
- 文件发布沿用同卷硬链接和进程内回滚，不承诺进程崩溃时的整批原子性。
- 不新增 IDE、调试器或发布态 app 源码监听；真实窗口与完整交互须和自动测试区分记录。
