# 036 项目默认录键统一到物理输入

## 背景

玩家设置已读取 Input::Frame，但项目默认设置仍从 ImGuiKey 反向转换：

```text
平台键 → Input::Frame → 玩家设置
      → ImGui 事件 → ImGuiKey → physical_key() → 项目默认设置
```

后一路维护了重复的键映射和 macOS Ctrl／Cmd 反转，无法准确区分 ImGui 合并的物理键。
更重要的是，渲染延期期间可以先失焦再恢复，Input 的 interruption 会保留中断事实，
但只看 ImGui 当前焦点的项目面板无法知道这次中断，可能把恢复后的按键录入旧目标。

## 调用链与职责

```cpp
// Editor::on_frame_ready
draw_editor_ui(frame.physical_input);

// draw_editor_ui
m_project_settings.render(m_editor_state.mode == EditorMode::Edit, input);

// ProjectSettings
if(editing)
    m_input_panel.render(input);
else
    m_input_panel.close();
```

帧只在同步调用中借用，不存指针，不重新访问 Engine 或窗口单例，不向面板提供空快照兼容接口。
项目默认草稿仍由 ProjectSettings 保存到 Project，玩家覆盖仍由宿主保存到玩家文件；不合并两种事务。

InputSettingsPanel 没有通用面板注册或多态消费者，因此移除 EditorPanel 继承，保留具体的
request／is_open／close／render 协议。没有为了增加一个参数而扩充所有 EditorPanel 的虚函数。
close 同时丢弃未消费的保存请求，避免切到 Play 后再保存 Edit 草稿。

## 录入逻辑

删除 physical_key 的 ImGui→Engine 映射表。Capture 记录目标身份、物理 serial／interruption 和本次 UI 所有者 ID：

```text
点“录入” → 记录当前采样版本，不消费这个开启帧
后续物理帧
  ├─ 失焦／中断版本改变／serial 回退／目标切换 → 取消
  ├─ 同一 serial → 不重复消费
  ├─ Escape pressed → 取消
  └─ 有效 Key pressed → 使用 InputActions 名称写入当前草稿
```

ImGui 只负责窗口焦点、活动控件与键盘所有权。录入期间及完成当帧，不让相同按键再触发编辑器快捷键；
它的按键身份不再决定写入的物理 Key。F25／World1／World2 在平台提供对应事件时可直接录入。
所有者 ID 在开始时取得，结束时直接释放，不按翻译后的窗口名称查找，也不保留 ImGuiWindow 指针。

## 架构价值

两个实际消费者使用同一物理输入事实，删除重复转换及平台特例；Input 继续拥有失焦／采样中断协议。
项目面板与玩家面板的草稿、关闭和游戏输入阻断仍不同，不为表面复用建立通用录入管理器。

## 验证

- Debug 全目标构建通过；18 项项目设置定向 UI 用例通过。
- 完整 1059 CPU、225 UI、构建契约与模块边界通过；1 项既有轮询回退测试按平台条件跳过。
- 覆盖物理左右修饰键、F25／World1／World2、ImGui-only 合并键不被误录、跨四次未绘制采样的失焦恢复、
  开始帧及同 serial 重绘抑制、S 不泄漏为保存快捷键、Esc 取消、关闭及 Play 切换丢弃保存请求。
- 首轮构建发现 ImGui GetID 不能通过 const 窗口调用；复核后直接持有本次所有者 ID，
  同时避免按窗口名称查找导致的中英文 ID 差异。修正后重新构建和完整相关回归通过。
- 日志：`/tmp/comet-auto3-036-{build,ui,regression}.log`。本项不改 GPU／App 实现，未重复 GPU 冒烟。

## 限制

这不是组合键或键盘布局文本输入系统。一次录入只设置一个物理 Key，Escape 继续作为取消键，
合法 Escape 默认仍可手动输入；项目默认变化仍在下次 App／Play 启动生效。
