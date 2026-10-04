# 041 录入让位于数值编辑

## 背景与复现

玩家面板的录键原先在控件绘制前处理，只确认弹窗仍有焦点。
轴动作开始录键后，用户转点倍率框再输入 2，同一按键既被录成 Digit2，也进入 InputFloat 修改倍率。
此前数值测试只向 ImGui 发送字符，没有同时发送物理按键，因此没有暴露这条真实输入链路。

本次红测同时发送物理输入和 ImGui 事件；普通分帧点击，以及点击和数字同批到达，都确认了误绑。
不是新增配置能力，而是让已有两种交互明确交接输入。

## 代码与时序

之前：

```text
capture_input：看到 Digit2 pressed，改绑定
  → render_actions：数值框接收字符 "2"，改倍率
```

现在：

```text
本帧开始：已有文本编辑先取消旧录入，保留 Esc 的两级规则
  → render_actions：控件处理点击、文本及显式重新录入
  → capture_input：若实际文本编辑占用则取消，否则只接收新物理帧
  → 录入提示与有效绑定关系
```

```cpp
// 数值框在处理 Enter 后会解除 ActiveID，因此在绘制前也检查旧占用。
if(ImGui::GetInputTextState(ImGui::GetActiveID()))
    m_capture.reset();

render_actions(input, translations);
if(!m_waiting)
    capture_input(input); // 内部再次检查本帧新取得的文本占用。
```

两次检查都是本帧局部判定，没有缓存控件状态。第一次防止文本结束键误入旧录入；
第二次识别本帧才点击的输入框。只清录入，不清 ImGui ActiveID、字符队列或倍率草稿。

不能以“任意控件 active”代替文本占用：正在按住 Press Key 按钮并不是编辑数字，
误取消会改变按钮标签和 ID，吞掉松开时的重新录入操作。
也不使用上一帧的 WantTextInput 标记：从数值框明确点击 Record 后，应立即开始新录入。
start_capture 原有 serial 基线仍拒绝开始当帧的旧按键，Esc 仍先取消录入、再次才关闭。

## 架构价值

沿原面板顺序处理 UI 意图，再读取原始物理按键；没有新增 Owner、全局事件、录入服务或平台键转换。
项目默认面板已有自己的编辑快捷键所有权，不把那一套机制复制到已整体阻断游戏输入的玩家弹窗。
Engine、RuntimeInput、玩家文件格式、宿主保存流程和暂停状态均不改变。

## 验证

- 红测在分帧／同批事件两种情况下都把 Space 误换为 Digit2；修复后只改变倍率为 2，保留其他覆盖和显式死区。
- 物理 Enter 与 ImGui Enter 同步结束数值编辑后不产生绑定；旧 WantTextInput 仍为 true 的帧中，
  明确切回 Record 也能开始新录入。再次按住／松开 Press Key 的真实鼠标路径仍有效。
- 47 项定向玩家面板／错误界面、完整 1060 CPU／232 UI、构建契约及模块边界通过；
  1 项既有 CPU 平台条件跳过。Debug 全目标与 Release App 构建通过，无新增警告。
- 独立审查确认 Esc、子窗口焦点、等待保存及手柄连接版本检查保持原语义。
  日志：`/tmp/comet-auto3-041-{red-ui,final-build,final-ui,regression,release}.log`。

## 限制与后续

本项处理当前面板的数值输入与录入交接，不提供完整输入焦点框架、文本／IME 游戏接口或组合键录入。
实际 App／Play 和真实手柄整体验收限制保持，不能由无窗口 UI 测试代替。
