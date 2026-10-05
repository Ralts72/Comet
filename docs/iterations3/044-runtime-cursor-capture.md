# 044 运行相机的光标捕获

## 背景与目标

App 与 Editor Play 已共用 CameraController，但原先仅读取普通窗口坐标。
按住转向时鼠标仍能到达屏幕边缘；Play 中一旦离开图像范围，鼠标授权就会被撤销。
本项接通路线图中的自由相机捕获，不扩展角色控制、Edit 相机或 Lua 光标 API。

## 从动作意图到窗口能力

之前只有消费链：

```text
Window → Frame → 宿主 Gate → RuntimeInput → CameraController → Transform
```

现在在同一授权之上增加一条窄的反馈链：

```text
CameraController::wants_cursor_capture(Scene, InputState)
  → SceneRuntime 汇总活动 System 的意图
  → Engine 在 advance 成功后应用
  → Window::set_cursor_locked
```

控制器使用 `camera.look`，不判断右键常量，所以玩家重绑定、动作组消费和项目配置仍统一生效。
当前主相机、组件启用与父级姿态有效性由同一个 cpp 内 helper 校验；捕获查询和 Transform 更新不各写一套选择逻辑。
查询读取当前已提交 Scene，相机在本轮更新中销毁或移除组件后，不保留上一轮捕获请求。

```cpp
if(m_executing || !is_active() || m_state != State::Running || !m_input_prepared)
    return false;
const auto& input = m_input.update();
if(!input.focused() || !input.physical().pointer_enabled)
    return false;
```

`m_input_prepared` 只标识本次运行／输入边界是否已有新快照：start、Stop、状态切换和 discard 使其失效。
它不是捕获缓存。没有清空 RuntimeInput 的历史电平，因为下一次 prepare 仍需据此生成正确的 released。
暂停和单步不捕获；Resume 前不沿用旧请求，后续重新授权的输入按既有电平协议处理。

## Window 与鼠标基线

Window 对外只暴露 Comet 类型：设置／查询锁定，以及即时读取窗口逻辑坐标。
内部使用 GLFW disabled cursor；平台支持时开启 raw motion，macOS 当前不支持 raw 也可正常捕获。

```cpp
if(is_cursor_locked() == locked)
    return;
// 切换原生模式及可用的 raw motion……
m_input.reset_cursor_baseline();
```

重复设置不丢弃位移；真实转换清空待发布 cursor delta，下一次位置只建立基线。
不能复用 `discard_pending()`，否则会递增整体 interruption、吞掉键沿，Gate 还可能把按住的转向键屏蔽。
已发布 Frame、滚轮、键盘和手柄不受基线重置影响。

Window 在失焦和关闭请求时释放；Engine 在暂停、Stop、最小化、运行失败和主循环退出时释放。
失焦不保存一个“恢复后自动重锁”的状态，也不把 GLFW 类型传进 Scene 或 System。

## Play 和共享 UI

Play 的首次捕获仍须在图像内获授权，捕获期间续持不依赖无界虚拟坐标的位置：

```cpp
const bool pointer_enabled = m_play_image_hovered || m_runtime.wants_cursor_capture();
return m_runtime_input.read(input, accepting, pointer_enabled);
```

Viewport 焦点、可见性、弹窗和活动控件守卫保持不变；只扩大已获捕获期间的鼠标范围。

实际回归确认，单设 ImGui `NoMouse` 不够：它只清悬停，左键仍会在 EndFrame 清窗口焦点，
也可能让 App 得到 `WantCaptureMouse=true` 而撤销游戏输入。共享 ImGui 适配层现在在 NewFrame 前：

```cpp
io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
io.ClearInputMouse();
// 从 ImGui 队列移除 Source == Mouse，保留键盘、文字和焦点事件。
```

原生回调继续完整传给 Window，所以游戏不丢鼠标事件；只隔离 UI 的副本。
解锁转换时重新读一次 Window 当前坐标，解决静止解锁没有位置回调、UI 必须等下一次移动才恢复的问题。
没有替换 GLFW 回调链、修改三方后端或为自动化鼠标限制改变生产逻辑。

## 验证

- 66 项 Input／CameraController／SceneRuntime 定向 CPU 通过。
- 9 项原生 Window、ImGui 与 Engine 接入用例通过，包括失焦、最小化、真实模式往返、
  不重复吞位移、回调串接、共享 UI 鼠标隔离和授权撤销解锁。
- 新 UI 用例覆盖捕获后离开图像、玩家重绑定、模态阻断、暂停／单步／恢复；
  完整 1066 CPU／236 UI、Shader 构建契约和模块边界通过，1 项既有 CPU 平台条件跳过。
- `NoMouse` 初版由真实 ImGui 队列测试复现失败，再验证修复；不是只检查新布尔值。
- Debug 全目标、Release App 构建通过，无新增编译警告；最终 9 项原生回归再次通过，
  包括左键初始 MoveId 在捕获后释放，以及静止解锁后的坐标与 UI 命中恢复。
- 日志前缀 `/tmp/comet-auto3-044-`。

本轮开始曾在隔离 App 中用实际键盘触发默认 Space，日志确认脚本切换；
自动鼠标仍未能打开 Input 面板，所以完整玩家改键人工闭环不算通过。
另在隔离副本临时把转向绑定为左键，用桌面拖动检查；截图没有确认视角变化，不能记为转向体验通过。
随后通过 Esc 正常退出，并恢复隔离副本的默认右键；仓库 demo 和用户个人配置未修改。
上述平台回归也不能证明真实硬件的加速手感、连续屏幕边缘移动与系统切换体验；这些仍需设备验收。

## 架构价值与后续

输入采样、授权、动作消费和平台执行各有唯一职责；App／Play 不新增设备分支、捕获 Manager 或状态队列。
新增 System 可选查询是实际消费者到宿主的窄意图，不是回调注入或全局事件总线。
README、架构和路线图同步真实行为，未修改文件格式或用户个人设置。
后续右摇杆转向应独立使用每秒角速度，不能把手柄轴伪装成鼠标像素位移。
