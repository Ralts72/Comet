# 037 Play 修饰键绑定与实际 UI 占用

## 背景与红测

项目和玩家配置允许 LeftControl、LeftAlt、LeftSuper 等物理键，App 可以接收，
但 Viewport 的旧判定把这些键按住视为整个 UI 占用：

```cpp
return io.AppFocusLost || io.WantTextInput || io.KeyCtrl || io.KeySuper || io.KeyAlt
       || ImGui::IsAnyItemActive() || /* 弹窗等 */;
```

这不仅让修饰键绑定无效：按住 W 移动时再按 Alt，Gate 会释放 W，松开 Alt 后 W 仍须松开重按。
生产未改时，连续移动及真实 demo spin.lua 的 Ctrl 改绑两个用例均失败，日志
`/tmp/comet-auto3-037-red-ui.log`。后者运行中把 spin.toggle 从 Space 改为 LeftControl，
保留 Lua 暂停状态，但 Ctrl 无法继续旋转。

## 代码变化与原因

```cpp
return io.AppFocusLost || io.WantTextInput || GImGui->NavWindowingTarget
       || ImGui::IsAnyItemActive() || ImGui::IsDragDropActive()
       || ImGui::IsPopupOpen(
           nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
```

移除三个过宽的修饰键条件，保留实际 UI 占用。没有改 Engine Key、InputActions、玩家文件或新增保留键策略。
MenuBar::collect_shortcuts 原本在非 Edit 时立即返回，Viewport 的 Edit 相机分支也不会在 Play 执行；
所以普通修饰键无需为了编辑命令而全部屏蔽。

但 ImGui 即使关闭 NavEnableKeyboard，Ctrl+Tab／Ctrl+Shift+Tab 仍会启动窗口切换。
因此改为检查它实际的 NavWindowingTarget，而非硬编码更多组合键。
此状态在 NewFrame 中决定，先于 Viewport 的悬停聚焦和游戏输入路由；窗口切换过程中既不把焦点抢回视口，
也不把同一批操作传入游戏。结束后原 Gate 负责旧长按抑制，不新增第二套按键状态。

## 架构价值

UI 与游戏按“谁实际在交互”划分边界，不按一类物理键粗暴划分。
App／Play 的动作语义保持一致，编辑器的真实窗口操作仍受保护；现有 Gate、更新边界重绑定和 Lua 实例无需改动。

## 验证

- 两项确定缺陷先红后绿：macOS／非 macOS 行为下左右 Ctrl／Alt／Super 不再打断持续移动；
  真实 demo Lua 运行中从 Space 改为 Ctrl，原暂停状态保留、长按不重复切换，第二次按下仍能暂停。
- 窗口切换用例通过真实按键启动 ImGui 行为，不注入 NavWindowingTarget；覆盖关闭键盘导航、切换阻断、
  返回视口后旧 W 不回放及重新按下恢复。首轮测试只切换平台行为标志，却没有同步构造时选定的窗口快捷键，
  误发了不匹配的组合；修正用例配置后通过，生产不依赖硬编码组合。
- 23 项定向 UI 通过，包含原文本、弹窗、暂停／单步、Edit 快捷键和 Option 相机；
  完整 1059 CPU、228 UI、构建契约、模块边界及 Debug 全目标通过，1 项既有平台条件跳过。
- 绿色日志：`/tmp/comet-auto3-037-{build,ui,regression}.log`。未改 GPU 或 App 实现，未重复 GPU 验证。

## 限制

操作系统保留快捷键仍由系统处理，不保证所有系统组合键都能到达窗口。
游戏组合键与手势语义仍未新增；本项只是让现有单键映射在 Play 正常工作。
自动 UI 与实际 Lua 联合验证不冒充真实硬件的完整人工玩法验收。
