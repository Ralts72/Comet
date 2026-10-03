# 011：Project 中重命名 Lua 模块

## 背景

005 已能创建 `.module.lua`，模块也显示在 Project 文件树中，但未索引文件的绘制分支直接结束，
没有重命名入口。用户只能切到外部文件管理器，而且容易误以为模块与组件脚本一样拥有稳定 Handle。

本项接通同目录重命名及引用修复流程。模块仍是按路径加载的源码依赖，不变成可挂载资产。

## UI 与请求链路的变化

原有资产请求靠身份定位：

```cpp
struct MoveRequest {
    Comet::AssetHandle handle;
    Comet::AssetRevision revision;
    std::filesystem::path destination;
};
```

模块没有 Handle，不能给这个请求填一个无效值再让后端猜测含义。因此新增明确的源码请求：

```cpp
struct ModuleRenameRequest {
    std::filesystem::path source;
    std::filesystem::path destination;
};
```

复用原有重命名弹窗、动态字符串控件、请求取出与完成反馈流程；弹窗目标改成单一 variant，
而不是维护两套可能同时有效的资产／模块弹窗状态：

```cpp
std::variant<std::monostate, Comet::AssetHandle, std::filesystem::path> m_rename_target;
```

模块行右键 Rename，编辑文件基名，固定保留 `.module.lua` 后缀；输入完整后缀也不会重复追加。
新旧路径统一经 `Script::module_name` 生成可复制的 `require` 引用，并明确提示源码不会自动改写。
同名或取消不产生操作；后端失败保留弹窗供修正，成功后关闭并刷新文件树。
普通资产原有 Handle／revision 检查和目录拖动不变；模块不改变当前选择，也不发布资产拖放载荷。

实际调用链：

```text
ProjectPanel 菜单／弹窗
  → ModuleRenameRequest
  → Editor::process_asset_requests
  → EditorAssets::rename_module
  → AssetSourceOperations::rename_module
  → 文件操作及候选 AssetDatabase 扫描
  → EditorAssets acknowledge 两个源码路径、accept_scan
  → ProjectPanel complete + update_scan_report
```

Editor 仍只编排，不执行文件操作或重载 VM。模块不进入只针对 Scene 的启动场景／会话路径补偿逻辑。
创建与改名共用一个 `render_module_reference` 小函数，避免重复显示代码和嵌套、跨行条件表达式。

## 文件事务与发布边界

后端先检查合法的模块路径、同目录、普通源文件、没有符号链接别名、目标不存在，
且源／目标均没有意外 `.meta`。不增加 mtime 或 revision 版本：作者从打开弹窗到确认之间编辑了模块内容，
正常改名应移动此刻的文件，而不是因为内容变化拒绝。

核心顺序为：

```cpp
AssetDatabase candidate = database;
std::filesystem::create_hard_link(original, target, error);
// 目标已存在时失败，不覆盖；随后移除原路径。
// 候选扫描成功后才将 candidate 安装为当前索引。
```

硬链接方式复用既有文件导入／创建的无覆盖发布策略；同目录保证同卷，但文件系统仍须支持硬链接。
扫描失败会恢复原路径、清理新路径，旧数据库及 revision 保持有效。
回滚也不覆盖新出现的原路径：只有确认两个路径仍指向同一文件，或成功无覆盖恢复原路径后，
才清理目标；无法安全恢复则保留文件并报告不完整回滚。

这里不是崩溃原子事务，也不承诺抵抗任意并发外部文件替换。没有新增恢复日志、通用 FileSystem、
ScriptManager 或第二套文件索引；普通资产成对移动事务未改。

## 为什么不自动修改 require

组件脚本通过 Handle 引用，模块通过 `require("scripts.shared")` 的逻辑路径引用，身份语义不同。
自动替换 Lua 源码字符串无法可靠处理变量、字符串拼接、注释或其他同名文本，因此本项不做字符串猜测。

改名成功只代表文件及索引操作完成，不代表 Lua 引用全部有效：

```text
两个组件 require shared，当前都已加载
  → shared.module.lua 改名 tuning.module.lua
  → 旧 require 找不到文件，关联组拒绝候选，两个旧实例继续运行
  → 只修复一个组件的 require：仍保留整个旧组
  → 两个组件都修复：发布新 Script 组
  → 下一次 Runtime 更新：活动实例整组换版
```

首次加载时没有 last-good 可保留，会正常报告缺失模块；不生成占位脚本。
旧输入依赖与失败尝试路径的并集仍由现有 AssetManager 保存，修好引用后沿原链路恢复。

## 验证

实现前的 UI 红测确认模块右键无法打开重命名弹窗；原始日志保留于
`/tmp/comet-auto3-011-red-{build,targeted}.log`，并非仅依据代码猜测。

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='AssetSourceOperationsTest.*:EditorAssetsTest.*:ScriptModuleTest.*'
build/tests/unit_testing --gtest_filter='ScriptModulesTest.*:ScriptModulePathTest.*'
build/tests/editor_ui_testing
ctest --preset dev-debug -R '^module_boundaries$' --output-on-failure
```

首次命令中 `ScriptModuleTest` 没有匹配项；核对真实测试名后，第二条定向命令补跑模块回归，
不把未匹配的过滤器算成已测。

- Debug 全目标构建通过，无编译 warning/error。
- 67 项文件事务／EditorAssets 定向 CPU 通过，另 16 项模块与路径回归通过。
- 163 项无窗口 UI 通过；原红测转绿，并覆盖真实后端执行、固定后缀、失败重试、取消及资产目标隔离。
- 新增 5 项后端测试用少量表驱动场景覆盖合法改名、路径／目录／文件类型、冲突、符号链接和扫描失败回滚。
- 新增真实双消费者回归：两脚本每次移动 1，改名及半修复时都继续移动 1，全部修复后才一起改为移动 4；
  同时断言 Registry／Database 数量不变、没有 `.meta`、依赖索引转向新路径、实例只在更新边界切换。
- 模块边界检查通过。本项只改 Editor 和测试，engine／app 代码与 010 已验证版本相同，
  不重复 Release app 或 GPU 冒烟；也不声称已完成人工操作与跨平台文件系统验收。
- 两个实现代理分别只读交叉审查 UI／后端，没有确认的未解决缺陷。回滚不完整的系统调用故障、
  改名恰好与在途扫描交错尚无专项注入测试；已核对沿用原有 monitor／database generation 校验，
  不为这些故障新增生产测试钩子，也不把源码审查声称为故障注入验收。

日志为 `/tmp/comet-auto3-011-first-{cpu-build,build,targeted,ui}.log`、
`/tmp/comet-auto3-011-module-regression.log` 和 `/tmp/comet-auto3-011-boundaries.log`。
双消费者测试中的缺失旧模块警告是预期失败路径，修复后已实际运行新代码。

## 价值与后续

新增能力沿既有面板请求、编辑器文件事务、资产依赖更新和 Runtime 生命周期接入，而不是另造模块管理体系。
唯一新增请求明确表示“路径文件”，与“稳定资产身份”保持区别；已有选择、场景历史及元数据规则不被污染。

手动验证可在测试项目创建模块及两个消费者，Play 后从 Project 改名，观察旧行为仍运行，
然后逐个修复 `require`，最后一次修复后整组采用新行为。操作前可备份测试项目，不以 demo 的正式文件做破坏性验收。

模块内容编辑、删除、目录移动、包导入和源码重构工具仍不在本项中；下一步按作者工作流继续，
不因为一个菜单入口就开放所有未知文件的任意操作。
