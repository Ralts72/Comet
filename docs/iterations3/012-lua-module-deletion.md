# 012：Lua 模块确认删除与系统回收站

## 背景与范围

005／011 已接通模块创建和同目录改名，但 Project 中的模块仍没有删除入口。
组件脚本是稳定 Handle 资产，模块则是 `require` 按路径加载的源码；不能为复用资产删除接口给模块伪造身份。

本项接通“模块右键删除 → 确认 → 系统回收站”，保留现有模块加载及失败恢复规则。
不新增编辑器回收站、文件撤销、通用 FileSystem、脚本管理器或 Engine API。

## 请求与 UI 的变化

原有资产请求保留 Handle／revision：

```cpp
struct DeleteRequest {
    Comet::AssetHandle handle;
    Comet::AssetRevision revision;
};
```

模块使用明确的路径请求，不填空 Handle：

```cpp
struct ModuleDeleteRequest {
    std::filesystem::path source;
};
```

删除确认框和开关共用，目标由一个 variant 表达。确认前不执行磁盘操作；取消不排队。
模块提示只针对源码及 `require` 引用；普通资产仍提示源码／metadata 对。
成功关闭并刷新文件树；失败显示原因、保留弹窗供重试。
模块行仍不变成资产选择或拖放载荷，也不进入 Scene 的 Undo／Redo。

```text
ProjectPanel::ModuleDeleteRequest
  → Editor::process_asset_requests
  → EditorAssets::remove_module
  → AssetSourceOperations::remove_module
  → 共用文件删除事务／候选数据库扫描／系统回收站
  → acknowledge(source) + accept_scan
  → 既有脚本依赖刷新、Project 完成反馈
```

Editor 只编排。模块路径不进入只适用于 Scene 资产的启动场景保护和文档处理。

## 文件事务：复用什么，保留什么差异

之前 `remove_asset` 为源码和 metadata 分别维护 moved、trash_attempted、restore_error 等状态，
并在函数内执行所有阶段。照搬给模块会复制扫描、回收站和回滚协议。

现在普通资产入口仍检查身份、metadata 和已索引依赖方；模块入口检查名称／路径、
普通文件、无符号链接别名且没有 `.meta`，然后分别把两个文件或一个文件交给私有事务。

```cpp
// 真实资产：保留预期移除的稳定身份。
remove_source_files(database,
    std::array{record.path, metadata_path(record.path)}, handle, move_to_trash);

// 源码模块：没有资产身份或 metadata。
remove_source_files(database, std::array{source}, std::nullopt, move_to_trash);
```

每个文件只维护自己的暂存、回收站尝试和恢复状态，事务按下列顺序运行：

```text
保留唯一暂存目录
  → 源文件移入暂存
  → 扫描候选数据库；失败立即恢复，不调用系统回收站
  → 将暂存文件硬链接回原 assets 路径
  → 系统回收站处理原路径
  → 确认原路径已消失
  → 安装候选数据库，清理暂存
```

为什么不是直接把暂存文件送进回收站：操作系统记录的恢复位置应为原 assets 路径，
而不是隐藏的 `pending-deletions`。暂存只用于当前事务补偿，不是用户可管理的第二个回收站。

失败时用不覆盖目标的独立复制恢复缺失文件，避免恢复到项目的文件与回收站副本共享 inode；
准备回收站硬链接失败时，恢复也不依赖同一个可能不支持的硬链接操作。若原路径已被其他文件占据，不覆盖它，
保留暂存内容并报告所在目录。末态检查使用 `symlink_status`，不会把断开的符号链接当作路径不存在。

## 脚本行为没有再造一套协议

允许删除被 `require` 引用的模块，不自动修改 Lua 源码，也不清场景组件：

```text
A 已加载并运行，B 尚未加载，两者 require 同一模块
  → 删除模块
  → A 的新候选失败，旧 Script／实例继续运行
  → B 首次加载失败，没有占位脚本
  → 补回同路径模块
  → 既有依赖更新重新准备候选
  → A 在 Runtime 更新边界换版，B 可以首次加载
```

这与外部删除／恢复模块使用相同的 AssetManager 逻辑；本项没有改 Engine 的脚本组、
运行实例或文件监听算法。009 已覆盖外部补回文件触发自动恢复，本项补删除入口的真实消费链。

## 验证

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='AssetSourceOperationsTest.*:AssetSourceModuleRemovalTest.*:EditorAssetsTest.*:ScriptModulesTest.*:ScriptModulePathTest.*'
build/tests/editor_ui_testing --gtest_filter='ProjectPanelTest.*'
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure -V
```

- Debug 全目标构建通过，没有编译 warning/error。
- 91 项定向 CPU、24 项 Project UI 通过。过滤器包含独立的 `AssetSourceModuleRemovalTest`，没有漏跑新用例。
- 完整 979 项 CPU、165 项无窗口 UI 通过；原生文件通知平台按既有条件跳过 1 项轮询回退测试。
- Shader 构建契约、模块 include 边界检查通过。
- 新增 7 项文件事务测试覆盖单文件删除、候选扫描失败不调用回收站、拒绝／虚假成功／先移动后失败、
  占位文件／断开 symlink 不覆盖、路径／metadata／alias 校验；现有资产成对删除与部分失败测试继续通过。
- 新增 1 项 EditorAssets 实际运行回归：A 每次移动 1，模块删除后仍移动 1；B 冷加载失败；
  补回 step=4 后 A 在下一次 advance 改为移动 4，B 首次运行同样移动 4。没有以 Registry 指针比较代替执行行为。
- 新增 2 项 UI 组合回归：确认／取消、失败重试、复用弹窗从模块切回资产、保持选择；
  真实 Scene history 的 Undo 只撤销实体创建，不恢复已移入回收站的模块。
- 实现后的双向审查发现并修复“失败回滚也依赖硬链接”的问题；最后源码审查无未解决发现。
  当前测试文件系统支持硬链接，没有声称已在不支持硬链接的文件系统上运行专项故障测试。

日志位于 `/tmp/comet-auto3-012-{build,targeted,ui,regression}.log`。
本项未改 Engine／app／GPU 生命周期，未重复 Release app 或 GPU 冒烟；不把无窗口 UI 和模拟回收站
说成原生操作系统菜单、回收站恢复或跨平台人工验收。

## 架构价值与限制

- 源码路径与资产身份分开表达，但文件事务共用；没有第二套模块 owner 或错误传播体系。
- UI 只采集意图，EditorAssets 确认文件变化并发布扫描，Engine 仍只负责依赖与加载。
- 删除不写 Scene 历史，原有实体撤销仍独立工作。
- 同卷暂存和硬链接沿用现有约束；不宣称多文件崩溃原子性或任意并发外部编辑安全。
- 若回收站只完成部分文件操作，系统中可能留有副本；项目内补偿失败会明确报告，不能假装全部撤销。
- 使用公开 TrashMover 注入进行确定性测试，不在验收时删除 demo 正式文件或操作用户系统回收站。
- 目录移动、外部多文件 Lua 包导入和源码重构工具仍未接通；按真实作者工作流另行验收。
