# 029 玩家手柄按钮录入

## 背景与前后变化

demo 的 `spin.toggle`、`palette.confirm` 已有手柄按钮绑定，但个人配置面板只能从方向名称下拉选择。
键盘可以直接录入，手柄却需要先知道 South／East 等名称。本项补齐同一个玩家面板的按钮录入，
App 和 Editor Play 自动共用；项目默认设置仍可通过既有下拉框编辑。

之前动作采样在 `InputActions::sample()` 内独自寻找第一个连接的槽：

```cpp
std::size_t gamepad = Input::MAX_GAMEPADS;
for(std::size_t index = 0; index < input.gamepads.size(); ++index)
    if(input.gamepads[index].connected) {
        gamepad = index;
        break;
    }
```

现在只读快照提供共同查询，两处使用相同规则，不复制选择策略，也不引入设备 Manager：

```cpp
std::optional<size_t> Input::Frame::first_connected_gamepad() const {
    for(size_t index = 0; index < gamepads.size(); ++index)
        if(gamepads[index].connected)
            return index;
    return std::nullopt;
}

const auto gamepad = input.first_connected_gamepad().value_or(Input::MAX_GAMEPADS);
```

## 录入生命周期

原来的键盘 Capture 增加开始／最后处理的物理帧序号和可选手柄槽。没有槽表示键盘录入，
不另建平行控制器；按“录入按钮”时无设备会提示，不进入等待设备的隐式录入状态。

```cpp
Capture capture{action.id, binding.id, input.interruption, input.serial, {}};
if(gamepad_button) {
    capture.gamepad = input.first_connected_gamepad();
    if(!capture.gamepad) {
        m_error = "No gamepad connected.";
        return;
    }
}
```

开始帧的 pressed、已经按住的 down、键盘输入和其他手柄均不成为该手柄绑定。
后续帧只查看锁定槽的新 pressed；成功后沿用 `change_control()`，保留动作／绑定 UUID 和已有倍率等稀疏字段。
实际游戏的输入继续经过原 Gate，不因录入成功或关闭菜单而立即触发这个按钮。

```cpp
if(input.serial == m_capture->serial)
    return;
m_capture->serial = input.serial;
// 检查 Escape 后，选择键盘或锁定手柄的 pressed。
```

失焦、采样中断、Esc、已观测到的断连、较低槽接入导致首选设备变化都会取消录入。
取消不更改原草稿；Esc 首先取消录入，再按才关闭面板。宿主继续按每 UI 帧调用一次 render 的原约定运行。
界面新增四条中文文案，App 仍使用其现有英文回退，不为此引入全局语言或设备事件系统。

## 架构价值

- 输入快照负责描述物理事实，动作映射和录入共用选择规则；UI 不访问 GLFW 或设备句柄。
- Capture 只拥有短暂编辑状态，保存、稀疏合成和 Runtime 更新边界完全复用 024–025。
- 同一面板服务 App 和 Play；不增加 Editor 专用录入路径或引擎到 UI 的依赖。

## 验证

28 项定向输入 CPU、31 项玩家面板 UI 通过；新增 1 项首槽查询与 5 项 UI 回归。
覆盖开始帧／held／其他槽忽略、无设备、成功录入保留字段及关系反馈、断连／首槽变化、失焦／采样中断。
成功例串起真实 Input、面板与 Gate：录入帧、关闭帧及重新获权时仍按住都不触发，释放后再按才交付游戏。

Debug App／Editor／CPU／UI 构建无警告；完整 1053 CPU、211 UI、Shader 构建契约与模块边界通过，
1 项既有平台条件跳过。日志前缀 `/tmp/comet-auto3-029-`。本项未改 GPU 后端，没有重复图形回归；
下一项阶段性架构回顾汇总 026–030 并执行 Release 验证。

## 限制与后续

仅录入标准手柄按钮，不录摇杆阈值、组合键或鼠标位移，也不解决多人设备分配。
槽号不是长期设备身份。若断连与同槽重连全部发生在两次发布之间，帧只描述最终连接状态；
现有采样器抑制重连的初始 pressed，但面板不声称能识别未观察到的硬件更换。
真实手柄的映射及手感仍需硬件体验验收，自动测试只验证实际 Input 事件到 UI／Gate 的行为。
