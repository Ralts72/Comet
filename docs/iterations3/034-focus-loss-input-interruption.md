# 034 跨过未绘制帧也能识别失焦

## 背景与复现

Engine 在交换链延期时仍发布物理输入，但不会调用当帧 UI。
原本 Input 的 focused 只保存最终状态，focus_event 不推进 interruption。
因此下面的真实调用序列会漏取消录入：

```text
开始录键 → 失焦并发布（无 UI）→ 恢复并发布（无 UI）→ 新按 K → 绘制 UI
```

最后一帧 focused 已是 true、interruption 没变，玩家面板把 K 当作先前录入的答案。
Gate 若同样没有消费中间失焦帧，也无法知道授权曾被打断。

## 代码变化

Input 已有 interruption，用于跨过被丢弃的采样；现在把真实失焦也纳入这个标记：

```cpp
if(m_pending.focused == focused)
    return;
// 原焦点状态、位移、恢复手柄基线逻辑保持。
if(focused) {
    m_gamepad_baseline.fill(true);
    return;
}
++m_pending.interruption;
release_buttons(m_pending.keys);
```

仅从 focused=true 变成 false 时推进；重复 false、重复 true 和恢复焦点不推进。
失焦原先生成的释放事件、鼠标位置基线与手柄清理保持，不调用会清掉边沿的 discard_pending。

玩家录入已有 `input.interruption != capture.interruption` 取消条件；Gate 已按中断版本撤销授权，
并要求恢复后先释放旧按住输入。两个消费者不增加代码，也不各自补一个 focus 回调。
同一物理帧内先失焦再恢复，也会留下可见的中断版本。

## 架构价值

失焦是物理输入流的一次中断，信息由 Input 负责保留；UI／Runtime 不应猜测两次绘制之间是否发生过失焦。
复用现有版本协议，没有新增事件系统、状态 owner、UI 特例或平台分支。

## 验证

先补测试、生产未改时，2 个 CPU 用例与 1 个键盘／手柄双分支 UI 用例稳定失败：
版本没有变化、Gate 放行恢复输入、个人草稿被 K／East 错误替换。
原始失败日志为 `/tmp/comet-auto3-034-red-{cpu,ui}.log`。

修复后 38 定向 CPU、40 定向 UI 全部通过；完整 1059 CPU／220 UI 与构建契约／模块边界通过，
1 项既有平台条件跳过。Debug App／Editor 与 Release App 构建通过，无新增警告。
断言还覆盖重复焦点事件、同批失焦恢复、释放事件、恢复时位移清零、旧长按抑制以及释放后重新按下可用；
UI Apply 保持原个人字段。修复后日志前缀 `/tmp/comet-auto3-034-`（不含 `red`）。

## 限制与后续

本项保留输入中断事实，不做焦点事件队列或恢复期间按键次数回放。
不修改 App 的 Esc 退出或 Play 的 Esc 停止策略，保留键选择冲突作为下一项处理。
