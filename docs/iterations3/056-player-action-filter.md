# 056 玩家动作筛选与共享文本控件

## 背景

demo 已有移动、相机、调色、重开等多个动作。此前玩家面板只能滚动完整下拉列表，
051 的真实 Lua 改键链路也需要滚到列表末尾才能选择 `palette.confirm`。
本项改善现有操作，不扩展 Runtime API 或玩家文件格式。

## 前后代码与行为

之前 `render_actions` 直接遍历全部默认动作。现在选择区单独由短函数绘制：

```cpp
void PlayerInputPanel::render_action_selector(const Text& translations) {
    // 动态字符串输入；之后遍历原始 actions，不建立另一份可写列表。
    input_text(label(translations, "Filter Actions").c_str(), m_action_filter);
    // 清除按钮及下拉选择……
}
```

候选判断只跳过绘制，继续使用原始索引和 UUID：

```cpp
if(!m_action_filter.empty()
    && !ImStristr(actions[index].name.c_str(), nullptr, m_action_filter.c_str(), nullptr))
    continue;
ImGui::PushID(actions[index].id.to_string().c_str());
if(ImGui::Selectable(actions[index].name.c_str(), m_selected_action == index)) {
    m_selected_action = index;
    m_capture.reset();
}
```

英文字母不区分大小写，其他 UTF-8 字节按子串匹配；不支持正则或语言学模糊搜索。
没有匹配项时显示说明，但继续保留当前动作及其详情，不自动选择第一个结果或修改绑定。
清除筛选恢复全部候选；关闭、取消后重开从空筛选开始。该字符串不进入个人文件。
Editor 使用既有中文词表，App 沿用英文回退，控件身份仍由 `###English` 固定。

## 文本输入为何移动

之前动态字符串适配位于 `editor/src/ui/widgets`，现在玩家面板也需要它。
把同一对文件移动到 `ui/src/widgets`，命名空间改为 `CometUi`：

```cpp
bool input_text(const char* label, std::string& value, ImGuiInputTextFlags flags);
```

Editor 原有的名称、路径、属性、快捷键和项目输入设置同步使用它。
底层 resize callback 与 ImGui 调用完全不变，没有固定长度截断、重复实现、旧位置转调空壳，
也没有让 App 依赖 Editor。CMake 只把源文件从 `editor_ui` 移到 `comet_ui`，文件总数不增加。

## 架构与生命周期复核

- 面板仍只拥有临时筛选、当前选择和草稿；保存、错误处理及运行边界应用没有搬进控件。
- 输入搜索框时复用原来的文本输入优先规则，结束键不会同时成为录入的新绑定。
- 新一行在已有可滚动正文内，底部应用／取消按钮不随正文滚走。
- 通用文本控件与动作关系展示仍分属 `widgets`、`input_widgets`；没有为复用把普通文本控件耦合到动作配置。
- 051–056 的变化分别落在既有 UI、Editor 文件工作流、Script 解析层；055 清理当前操作错误，
  不扩成全局错误状态。未新增 Manager、生产测试访问口或宿主专用输入映射。

## 验证

- 两项新 UI 用例通过：混合大小写、过滤隐藏项、原始 UUID 对应、180 字符扩容、无结果、清除和重开。
- 已有两个动作的个人字段保留，选择过滤后非零索引动作只修改其禁用状态，不影响另一个动作。
- 真实 ImGui 鼠标激活搜索框，同批物理键／字符及 Enter 收尾：取消录入但 Apply 草稿仍为空；翻译不改变 ID。
- 完整 **1078 CPU／245 UI** 通过，1 项既有平台条件跳过；Shader 构建契约与模块边界通过。
- Debug 全目标和 app Release 构建通过，增量编译无新增警告。
- 独立只读复核没有发现阻塞问题；README 用法／控件位置和路线图已同步。

## 限制与后续

只筛选动作名，不搜索绑定、动作组或翻译后的用户内容；输入配置资产化仍按真实项目需求推进。
这不替代 App／Play 的完整真实指针与手柄验收；本项不重复执行无关 GPU 或性能测试。
