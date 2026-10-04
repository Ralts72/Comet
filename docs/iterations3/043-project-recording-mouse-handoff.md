# 043 项目录键期间的鼠标交接

## 背景与实际问题

项目默认输入面板使用专用 KeyCapture 活动项阻断编辑器快捷键。
它每帧重新设置 ActiveID，默认也会阻止其他控件的鼠标命中。因此录键后点击倍率框或“关闭”没有反应；
继续输入数字仍会变成新绑定，而非修改倍率。旧测试通过 ActivateItemByID 操作控件，绕过了实际鼠标命中。

本次先用真实鼠标 down／up 加物理数字和 ImGui 字符事件复现：
倍率输入未激活、绑定误改；关闭按钮也无法关闭。修复后同样事件走通，不增加测试专用生产接口。

## 代码前后变化

之前专用活动项同时挡住键盘与鼠标；现在只在它自己的生存期允许鼠标切换控件：

```cpp
ImGui::SetActiveID(owner, ImGui::GetCurrentWindow());
ImGui::GetCurrentContext()->ActiveIdAllowOverlap = true;
ImGui::SetActiveIdUsingAllKeyboardKeys();
```

SetActiveID 每次都会重置 overlap，所以这条声明必须紧随其后。
它不放开键盘路由；用户点击参数框后，原有 `ActiveID != owner` 判定取消录入，保留新输入框的活动身份。
原来的渲染顺序已经是先控件、后采集，无需复制玩家面板的另一套判断。

取消录入时原先会把本帧所有新按下的 ImGui 键锁给旧 owner，其中包括 MouseLeft 别名。
现在排除鼠标键，保留普通键、聚合修饰键等原有结束帧保护：

```cpp
if(!ImGui::IsMouseKey(key) && ImGui::GetKeyData(key)->DownDuration == 0.0f)
    ImGui::SetKeyOwner(key, owner, ImGuiInputFlags_LockUntilRelease);
```

这样不会从新倍率框抢回鼠标，也不会在录入 S 后的同一帧触发编辑器快捷键。
不能简单枚举 Keyboard_BEGIN 到 Keyboard_END：该范围不含 ImGui 内部聚合修饰键。

允许点击后还发现重按录入按钮的边界：按住 Press Key 会结束旧录入，文字变回 Record Key；
旧代码的按钮 ID 跟着文字变化，松开不再触发。改用固定身份，显示文字仍按状态翻译：

```cpp
const auto caption = std::string(Ui::text(capturing ? "Press Key" : "Record Key"))
                     + "###Record Key";
```

无需专门缓存“正在按住录入按钮”。鼠标松开沿原按钮回调建立新的 serial／interruption 基线即可。

## 架构价值

修复局限在现有项目输入面板：键盘独占不等于冻结整个窗口。
不引入事件总线、Input Manager、跨面板基类或额外 owner；Engine、个人配置、RuntimeInput 和文件格式不变。
项目面板仍是非模态设置窗，玩家面板仍是整体阻断游戏输入的模态窗，不强行合并两者策略。

## 测试结果

- 原代码两项真实鼠标红测失败，修复后通过：录入→倍率（分帧／同批数字）、录入→关闭。
- 重按 Press Key 的 down／hold／up 红测另确认按钮 ID 变化丢失松开；固定身份后可继续录 K。
- 18 项项目输入 UI、完整 1060 CPU／234 UI、Shader 构建契约与模块边界通过；
  1 项既有 CPU 平台条件跳过。Debug 全目标构建通过，App／Release 源码未受本项影响。
- 独立审查核对真实鼠标所有权、Enter、快捷键路由和窗口移动，没有增加生命周期状态。
- 日志：`/tmp/comet-auto3-043-{red-ui,restart-red,final-ui,final-build,final-regression}.log`。

## 限制与后续

README 补充录入时可直接转去编辑／关闭的行为。自动 UI 回归不替代真实 App／Play、系统 IME 或手柄体验验收。
本项不建立通用焦点框架，继续沿各面板已有的交互所有权处理。
