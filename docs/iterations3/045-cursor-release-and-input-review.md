# 045 解锁首帧点击与输入边界回顾

## 背景及修复

044 能在静止解锁后恢复 UI 坐标，但回顾发现：恢复位置使用 `AddMousePosEvent` 追加到队尾。
如果用户在下一帧前已经点击，队列是“按下 → 恢复位置”；ImGui 的事件分帧机制会延后位置处理，
使第一次点击仍以无效坐标命中。已用真实共享 ImGuiContext 用例确认，而非假设。

之前：

```cpp
const auto position = window.get_cursor_position();
io.AddMousePosEvent(position.x, position.y);
```

现在在 ImGui NewFrame 前直接恢复输入状态基线：

```cpp
const auto position = window.get_cursor_position();
io.MousePos = {position.x, position.y};
```

这是 `ClearInputMouse` 后的基线恢复，不是伪造一次新移动。随后队列里的真实事件仍由 ImGui 正常处理；
不重排点击、不修改 `MousePosPrev`，也不引入光标位置缓存或新的状态 owner。
该保证针对静止解锁的可达问题，不声称从原生最新位置重建未被平台提供的历史坐标。

## 041–045 架构回顾

| 范围 | 结论与处理 |
| --- | --- |
| 目录与依赖 | 项目默认面板留在 Editor，个人面板与平台 UI 适配由 App／Editor 共用；没有新增目录或捕获管理器 |
| 录入与控件所有权 | 041／043 让录入把鼠标交给新控件，仍保护键盘快捷键；不把默认配置与个人覆盖合成同一保存协议 |
| 公共逻辑 | 042 只共用弹窗边界计算，打开／关闭／失败状态仍归各面板；没有为少量状态造基类 |
| 运行捕获 | System 提供当前意图，Runtime 检查生命期与授权，Engine 执行 Window 能力；没有向 App／Editor 复制物理绑定判断 |
| 失效状态 | Runtime 输入准备标志保护状态切换边界，不复制动作历史；Window 直接读原生锁定模式，不维护影子布尔值 |
| UI 适配 | 鼠标队列过滤局限在 ImGui 适配层，保留原生采集和键盘／焦点事件；本次修复解锁位置与点击顺序 |
| 测试去重 | 删除 Viewport 夹具中复制的 NoMouse 策略与对应自证断言；Viewport 测授权，实际 ImGuiContext 测队列／焦点／恢复 |

`wants_cursor_capture` 是现有 System 的可选查询，无平台类型或回调注入，不需另建 EventBus。
前置授权检查与消费端相机检查服务不同边界，未机械合并；也未给 System 保存重复的窗口或 Scene owner。

## 验证

- 在原实现加入“静止解锁、没有位置回调、已有左键按下”后，坐标仍为无效值且 WantCaptureMouse 为 false，红测成立。
- 修复后的同一实际窗口用例通过；236 项 Editor UI 回归通过。
- Debug 全目标、独立 UI 目标和 app Release 构建通过，无新增警告。
- 未改 Engine／Runtime／文件协议，不重跑未受影响的性能测量。
- README 已复核：044 已描述解锁恢复和捕获边界，本项无需追加用户用法或变更日志。

## 后续与限制

手柄右摇杆转向作为下一独立项，不与本次修复混在同一提交。
完整 App／Play 玩家改键及真实硬件体验仍待验；自动队列回归不能替代系统鼠标、触控板和手柄测试。
