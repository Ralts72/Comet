# 022：Lua 模块目录拖放

## 背景

011 已允许模块在同目录改名，但整理模块目录仍只能去外部文件管理器移动。
Project 本来已有目录拖放目标；模块又是源码依赖，不应为了复用资产拖放而补一个 Handle 或 `.meta`。
本项补齐项目内模块整理，继续由作者显式修复 `require`，不扩展外部依赖包导入。

## 代码前后与职责

原文件操作只接受同目录名称变化：

```cpp
AssetScanReport rename_module(AssetDatabase& database,
    const std::filesystem::path& source, const std::filesystem::path& destination);

if(source.parent_path() != destination.parent_path())
    return operation_error(destination, "Lua modules can only be renamed in the same directory");
```

现在统一为 `move_module`，同目录改名只是移动的一种情况：

```cpp
const auto parent_status = std::filesystem::symlink_status(target.parent_path(), error);
if(error || !std::filesystem::is_directory(parent_status))
    return operation_error(destination, "Lua module destination parent must be an existing directory");
```

目的目录必须已经存在，不自动建目录。合法模块名、项目根目录、符号链接别名、普通源文件、目标与 metadata
冲突检查仍由原 `AssetSourceOperations` 负责；没有新增 ModuleManager 或第二条文件移动实现。

原有事务顺序不变：无覆盖硬链接到目标 → 删除旧路径 → 扫描候选数据库 → 成功提交，失败无覆盖恢复。
模块只处理源码文件；资产移动仍处理稳定身份与 metadata 对，二者不混用请求类型。

## Project 与 Editor 调用链

之前模块行只有文本和右键菜单，没有拖放源。现在模块行可以拖动，但点击不改变资产选择：

```cpp
ImGui::Selectable(name.c_str(), false);
const auto absolute = (m_asset_root / source).generic_string();
ImGui::SetDragDropPayload(MODULE_DRAG_TYPE, absolute.data(), absolute.size(), ImGuiCond_Once);
```

这段位于模块行的拖放分支内。私有 payload 由 ImGui 复制路径字节，不传 `filesystem::path` 对象、树节点指针
或伪造资产身份。接收方相对当前项目根转换路径，复用 `Script::module_name` 拒绝非本项目或非法模块路径。
目录节点共用 `accept_internal_drop`，资产 payload 路径不变；模块移动到当前目录直接忽略。

```cpp
if(source != destination && Comet::Script::module_name(source))
    m_pending_module_move = ModuleMoveRequest{source, destination};
```

请求拥有路径值。Editor 在更新阶段消费，完成文件事务后才刷新树，不在 ImGui 遍历中修改目录：

```cpp
if(const auto move = m_project_panel->take_move_module_request()) {
    auto report = m_assets->move_module(move->source, move->destination);
    m_project_panel->complete_move_module(*move, report);
    accept_asset_report(std::move(report));
}
```

右键 Rename 也生成 `ModuleMoveRequest`，继续使用原重命名弹窗与错误恢复。
`EditorAssets` 成功后确认旧／新文件变化，再沿原 `accept_scan` 恢复消费者；额外在 Log 输出新旧 require 引用，
拖放提示也明确说明不会自动改写引用。没有新增回调、事件总线、场景历史或运行时状态。

## 运行时行为

例如把 `shared.module.lua` 移入 `scripts/`，引用从 `require("shared")` 变为 `require("scripts.shared")`。
移动成功只代表文件事务成功，不代表脚本引用已经修好：

- 已加载的两个消费者仍运行旧有效版本。
- 只修好其中一个时，关联组不发布半套结果。
- 两者都修好并通过候选验证后，在运行更新边界一起换版。
- 首次加载没有旧版本可用，仍正常报告缺失模块。

这些语义复用既有模块依赖和整组重载，不把 Editor 文件操作接到 Lua VM。

## 验证

- 修复前：三个 UI 用例因缺少模块拖放源失败；八个参数化后端用例中，同目录四项通过，跨目录四项失败。
- 修复后：79 项文件事务／EditorAssets 定向 CPU 和 26 项 Project UI 通过。
- 同目录与子目录共用参数化测试，覆盖当前源码内容、无身份／metadata、冲突、扫描失败回滚及重试。
- 实际运行两个脚本消费者，验证移动后 last-good、单消费者修复不发布、全部修复后换版和依赖路径更新。
- UI 验证拖入关闭的目录、拖回根、同目录无操作、冲突保留、路径请求只消费一次、原资产拖放及选择／Undo 不变。
- 首次绿测准备阶段有一项夹具错误：无扩展名普通文件使初始扫描失败；改为基线扫描后放置非法父路径，
  使断言真正验证移动拒绝，未改变生产扫描规则。
- Debug 全目标、Release app 构建、完整 998 CPU／177 UI、构建契约及模块边界通过；1 项原生监听平台条件跳过。
  Release 没有运行时代码变化，构建确认无待更新目标。无新增编译警告。

日志前缀 `/tmp/comet-auto3-022-`：red-build、red-ui、red-core-build、red-core、green-build、green-core、
green-ui、core-rebuild、regression、release。自动 UI 不替代真实桌面拖动的人工体验验收。

## 限制与后续

只移动当前项目内合法 `.module.lua` 文件到现有目录，不移动目录、不引入跨项目复制、外部包解析或自动 require 重写。
同卷硬链接和进程内补偿回滚的原限制保留，不声称跨卷支持或崩溃原子性。
模块不参与资产引用槽拖放、Inspector 选择或 Scene Undo；后续更多文件操作应先明确身份和依赖语义。
