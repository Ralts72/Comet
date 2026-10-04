# 031 输入设置共用菜单数据

## 背景与边界

项目默认设置和玩家个人设置已经共用有效绑定关系，但各自重复了动作类型名、来源过滤和控制枚举范围。
今后添加一个控制时容易只更新一个面板。本项只收敛这些无状态菜单数据，不合并两种编辑模型。

## 代码变化

之前两个面板分别维护 `type_name()`、`source_allowed()` 和按 source 分派的枚举循环。
现在现有 `input_widgets` 提供：

```cpp
const char* input_type_name(InputActions::Type type);
std::span<const std::string_view> input_sources(InputActions::Type type);
std::span<const InputActions::Control> input_controls(std::string_view source);
```

来源列表是有限的静态数据，按 Button／Axis／Delta 返回合法子集。
具体控制由 Engine 枚举的连续范围生成静态数组，名字仍使用 `InputActions::format_binding()`，
不复制按键或手柄名称表。界面遍历同一范围：

```cpp
for(const auto& option : CometUi::input_controls(source)) {
    const auto name = Comet::InputActions::format_binding({option}).value();
    // 当前面板决定标签、选中状态、控件 ID 与写回。
}
```

项目面板继续使用 `##Source`／`##Control` 与英文选项身份，键盘仍可直接输入名称；
玩家面板继续用绑定 UUID 和 `###` 稳定身份，并保留自己的翻译及物理帧录入。
没有为几行 Selectable 引入回调参数或 ID 策略类。

来源改变后的写回不能机械统一：项目草稿是完整默认定义；玩家草稿是字段覆盖，
需要保留其他字段并只清理不兼容死区。解析、保存、Capture、布局和恢复默认均留在原职责中。

## 架构价值

已有共享 UI 目录成为两种面板的菜单数据入口，Engine 仍只定义控制语义与校验。
没有新增文件、类、Manager、存储状态或事件；纯重复枚举分派被删除，控件行为不变。
README 已复核，使用方式不变，不增加用户文档条目。

## 验证

复用既有项目输入、玩家输入与错误 UI 测试，48 项定向全部通过；未增加只测封装的孤立用例。
完整 1053 CPU、214 UI、Shader 构建契约及模块边界通过，1 项既有平台条件跳过；
Debug App／Editor／UI 构建无警告。日志前缀 `/tmp/comet-auto3-031-`。
控制末尾枚举选择、键盘文本、来源重选不变、倍率／死区保存、拒绝补丁回退和翻译稳定 ID 均由原消费者回归覆盖。

## 桌面验收记录与限制

继续 030 的隔离项目验收时，临时日志确认同一次自动点击的物理位置是 `(912.5,31)`，
处于按钮 `(888,20)..(940,42)` 内，但 ImGui 本帧位置是 `(1192,359)`，因此不命中。
GLFW 后端在未获得 mouse-enter 窗口时会回读系统指针；自动输入的事件位置与该回读不一致。
拖动式自动输入也出现同一差异，不能用这个结果断言真实鼠标失效。

临时诊断日志已从源码移除，测试窗口已正常关闭。未绕过原生输入或为自动工具修改生产后端；
保留“App／Play 真正 Apply—生效—重开尚未完成”的验收限制。没有写入原项目或玩家个人设置。
日志前缀 `/tmp/comet-auto3-031-app-probe`，隔离项目在 `/tmp/comet-auto3-player.s9mjpH`。
