# 051 调色模式中的玩家改键完整链路

## 背景与前后对比

此前 037 证明直接重绑定不会重建 Lua，048 证明默认按键能驱动调色和重开；
玩家面板与个人文件分别有回归，但没有把这些接口连成同一次玩法操作。
本项不增加生产 API，而是补一条有真实消费者的跨模块回归。

```cpp
// 之前：分别验证 Runtime::rebind_input_actions、面板草稿、个人文件和 demo。
// 现在：真实 demo Lua 实例一直运行，面板真实录入后沿宿主顺序交付。
auto resolved = request->resolve(defaults);
settings.value().save(*request);
runtime.rebind_input_actions(std::move(resolved).value().actions);
panel.complete(Result<void>::success());
```

测试先暂停中心方块的旋转，进入调色并选中橙色；将 `palette.confirm` 从 Space 改为 K，
点击 Apply 后继续操作，而不是重新创建 Scene 或 ScriptSystem 来“证明”新键有效。

## 关键断言及架构价值

- 旋转保持暂停、材质覆盖快照保持不变：同一 Lua 实例及其 self 状态没有被改键重置。
- 再按右方向键得到下一种紫色，移动方块不移动：palette_index 和动态消费组都保持。
- K 确认后方向键才恢复移动；关闭弹窗那一帧仍被 Gate 阻断。
- 真实 R 操作生成重开请求；随后停止 Runtime、从 Edit 重克隆、重读个人文件并启动，K 仍有效。
- 重开后 Space 不再退出调色；Edit 序列化、项目默认动作与共享 Material revision 均未变化。

只增加既有 `PlayerInputPanelTest` 中的一个用例，复用真实 `spin.lua`／`move_cube.lua` 和既有夹具，
不复制物理／音频完整场景，不增加私有访问、测试生产接口或第二份运行编排 owner。
README 已复核：既有玩家改键用法不变，不加入测试变更日志。

## 验证与限制

定向用例 `DemoPaletteRecordingSurvivesApplyAndRestartWithoutResettingLua` 通过。
在包含下一项源码打开入口的共同工作区，Debug 全目标与 app Release 构建通过；
1076 CPU／242 UI、Shader 构建契约及模块边界通过，1 项既有平台条件跳过。

桌面验收尝试中，Tab 能改变真实 App 中方块颜色。Input 点击未能打开菜单，临时日志显示：
按下与松开都进入物理／ImGui 输入，但点击帧 MousePos 为 `(462,454)`，并非按钮位置；
NoMouse 为 false，按钮未取得 ActiveId。短拖动也表现为按下位置仍旧、随后才更新到按钮处。
共享 Context 的正常未锁定路径没有覆盖位置，不能据此认定普通鼠标有引擎故障。
临时诊断日志和源码已撤除，隔离 App 正常退出；首次临时诊断误用访问器导致的构建失败已修正。

这条无窗口 UI 回归证明接口之间的状态契约，不替代 App／Play 的实际点击、声音及手柄设备体验。
后续仍需在可提供真实指针位置的环境验收，不为自动化工具改变正常 UI 输入规则。
