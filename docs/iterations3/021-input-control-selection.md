# 021：非键盘绑定直接选择

## 背景

demo 已同时使用鼠标滚轮、标准手柄按钮和轴，但项目输入面板此前要求手动填写 `ScrollY`、`RightX`、
`South` 等控制名。引擎本来就知道完整的枚举和稳定名称，编辑器不应要求用户记忆，也无需复制第二份名单。

## 代码前后与职责

之前所有来源统一使用文字输入：

```cpp
Ui::input_text("##Control", binding.control);
```

现在只有键盘保留文字／按键录入，其他来源按既有枚举列出选项：

```cpp
render_control(binding.source, binding.control);

// render_control 内：只有展开下拉框才进入选项生成。
control_options<Input::GamepadAxis>(control, int(Input::GamepadAxis::Count));
```

私有模板只做 UI，真实名称仍由引擎决定：

```cpp
const auto name =
    Comet::InputActions::format_binding({static_cast<Control>(index)}).value();
if(ImGui::Selectable(name.control.c_str(), selected))
    current = name.control;
```

鼠标按钮、手柄按钮和手柄轴使用各自 Count；鼠标位移枚举的有效上界是 ScrollY。
没有新公共 API、名单缓存、配置格式、运行时状态或硬件轮询。未连接手柄也能编辑其项目绑定。

## 行为与调用链

- 同一来源换控制，只改 control，保留倍率、死区、上下文及其他绑定。
- 同一项重新选择不改变配置；切换来源仍沿原规则初始化倍率／死区。
- 切到没有默认控制的来源时显示“选择按键／轴”，不偷偷选首项；空值 Save 仍失败。
- 选择只修改草稿，Save 才经 parse_binding／create 校验，交给 Project 原子保存。
- Close 丢弃未保存选择，重开显示项目当前配置；更换动作类型造成的不兼容草稿仍显式保留待修复。
- 键盘文字编辑、录制、Esc 取消以及 020 的 macOS Ctrl／Cmd 还原不变。

新预览文案补齐中文；控制名保持与 project.json 一致的稳定标识，避免翻译成为持久值。

## 验证

- 新增两项 UI 用例，修复前因没有控制选择框而失败，修复后通过。
- 四类来源均实际打开下拉并选择，验证调参保留、同项重选和 Project 保存／读取往返。
- 切源空值拒绝、明确选择后保存、关闭丢弃另一次选择均通过。
- 15 项项目设置 UI、40 项输入／项目 CPU 定向通过；Debug 全目标构建通过，无新增编译警告。
- 完整 175 项 UI 与模块边界检查通过；clang-format、git diff 检查通过。
- 本次只有 Editor 行为与文档变化，未重复上一项刚完成的 Release／完整 CPU／GPU 验证。

日志前缀 `/tmp/comet-auto3-021-`：red-build、red-ui、build、ui、cpu、regression。
UI 自动操作不代表实际手柄手感或物理设备录制验收。

## 限制和后续

这里是项目默认绑定编辑，不是运行中玩家改键或手柄录制。选择手柄轴不会修改其采样、死区计算和时间单位，
App／Play 仍在重新启动后使用保存的配置。玩家覆盖、设备分配和组合键按路线图独立推进，不因本次 UI 完善提前扩张。
