# 017：重选输入来源不再改写绑定

## 背景与实际影响

项目输入面板的 Source 下拉将“选中一个选项”当成“来源改变”，无条件重置 control、scale 和 deadzone。
demo 的 `camera.zoom` 原本是 `motion/ScrollY`；再次点击唯一可选的 motion 后，
它会变成可合法保存的 `motion/CursorX`。重开 App／Play 后，横向鼠标位移取代滚轮缩放。
键盘和手柄轴重选则会清空 control，造成原本合法的草稿无法保存。

## 前后代码

原来只判断 ImGui 返回了点击：

```cpp
if(ImGui::Selectable(source.data(), binding.source == source)) {
    binding.source = source;
    binding.control = source == "motion" ? "CursorX" : "";
    binding.scale = 1;
    binding.deadzone = 0;
}
```

现在加上实际值改变的条件，初始化逻辑保持不变：

```cpp
if(ImGui::Selectable(source.data(), binding.source == source)
    && binding.source != source) {
    binding.source = source;
    binding.control = source == "motion" ? "CursorX" : "";
    binding.scale = 1;
    binding.deadzone = 0;
}
```

只有明确选择另一来源才清除旧设备的 control，并恢复新绑定倍率和死区。
没有把另一设备的键名强行复用，也没有修改 Engine 的严格输入校验。

## 设计理由与架构边界

这是 UI 草稿编辑的幂等性问题，应在发生误写的位置修复，而不是在 Project 保存、
InputActions 或 RuntimeInput 中猜测用户意图并修复。与既有 Type 下拉保持相同的“值变化才转换”规则，
无需新增类、通用变更事件、配置备份或回调。使用流程没有变化，README 复核后保持不变；
路线图已有配置保持验收，不为一个缺陷新增功能阶段。

## 验证与限制

- 新增一个表驱动 UI 用例，覆盖 motion/ScrollY 的 -2 倍率、key/Left 的 -1 倍率、
  gamepad_axis/RightY 的 -0.5 倍率与 0.2 死区。通过实际 combo 选项激活后 Save，
  比较完整 InputActions（含动作组），不访问面板私有草稿。
  最后从手柄轴实际切到 key，确认须补填 control，且倍率／死区正常重置；不是禁止所有 Source 修改。
- 修复前：motion 保存了错误配置；key 与 gamepad_axis 因空 control 无法保存，红测确认。
- 修复后：11 项定向项目设置 UI 通过；完整无窗口 UI 169 项通过。
- Debug 全目标构建、38 项输入／项目 CPU 定向回归及模块边界检查通过。
- 日志：`/tmp/comet-auto3-017-{red-build,red,build,ui,ui-all,cpu,boundaries}.log`。
- 本项不改渲染、引擎输入、项目格式或生效时间；不重复 GPU／Release 构建或性能测量。
  真实设备手感仍与 UI 配置回归分开验收。
