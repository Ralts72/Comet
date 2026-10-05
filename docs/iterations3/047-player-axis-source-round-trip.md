# 047 玩家绑定来源往返的死区继承

## 背景与复现

玩家绑定默认是 `LeftX / deadzone=0.2`，没有个人覆盖。在面板把来源切成键盘，再切回手柄轴，
控制已经回到 LeftX，却残留 `{deadzone: 0}`。默认死区被关闭，以后项目将默认值改为 0.3 也不再继承。
这是实际控件路径的红测，不是手工构造一个本来就合法的零值覆盖。

## 前后代码及理由

此前 `change_control` 只处理离开手柄轴：

```cpp
patch.control = control;
if(control == binding.control)
    patch.control.reset();
if(!is_gamepad_axis(control) && effective_deadzone != 0)
    patch.deadzone = 0;
```

零死区保证键盘／鼠标按钮绑定合法，但返回轴时没有退出这条兼容处理。
现在先看切换前的有效控制，再修改控制覆盖：

```cpp
if(std::holds_alternative<Input::GamepadAxis>(control)) {
    if(!std::holds_alternative<Input::GamepadAxis>(
           patch.control.value_or(binding.control)))
        patch.deadzone.reset();
} else if(patch.deadzone.value_or(binding.deadzone) != 0) {
    patch.deadzone = 0;
    if(binding.deadzone == 0)
        patch.deadzone.reset();
}
```

跨来源进入手柄轴时重新继承项目默认死区；同为手柄轴的 LeftX → RightX 不重置死区。
个人显式零值、倍率及其他字段仍可保留。控制也回到默认且没有别的更改时，已有 `commit_binding`
会删除空覆盖，不新增清洗流程。

这里明确的是“来源改变重新初始化其专属参数”，不保存来源历史，也不声称识别零值最初来自用户还是 UI。
跨来源之前的自定义轴死区不会另藏一份供恢复；要精确撤销整笔编辑可以取消面板。
文件载入、保存和 `InputOverrides::resolve` 完全不变，不能为了这个 UI 问题清掉所有合法零值。

## 架构价值与范围

修复留在已有玩家面板的编辑边界，不把操作历史写入运行映射或玩家文件，不新增临时参数缓存、版本或 Manager。
项目默认设置面板有自己的完整值编辑规则，本项不修改它；App 与 Editor Play 共用同一修复。
README 补充来源切换的用户可见规则；路线图没有新增功能阶段。

## 验证

- 红测：来源往返后仍有覆盖，新的默认 0.3 被错误合成为 0。
- 保留测试：来源重选及同轴控制来回选择后，显式零死区和自定义倍率保持原样。
- 46 项玩家面板 UI 回归通过；完整 1071 CPU／238 UI、Shader 构建契约与模块边界通过，1 项既有平台条件跳过。
- Debug 全目标及 app Release 构建通过，无新增警告。

## 其他审查与限制

本次额外复核 Lua 关联重载、辅助方法隔离、通知交付及玩家配置保存／取消，没有确认新的代码缺陷。
没有因此新增补丁或重复性能测量。桌面工具在 09:44 左右报告 Mac 锁定，隔离 App 验收进程已终止；
实际 App／Play 的完整玩家改键与真实设备体验仍未完成，不用内存 UI 测试替代。
