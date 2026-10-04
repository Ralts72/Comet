# 035 玩家保留键边界与阶段性架构回顾

## 背景

玩家下拉框原来可选择 Escape 并保存成功，但 App 会先处理 Esc 退出，Play 会先处理 Esc 停止。
动作没有机会收到这个按键。这不是按键枚举或文件格式错误，而是设置界面不知道宿主的输入占用。

## 代码变化

共享面板不硬编码“所有游戏都禁止 Esc”，由宿主明确提供自己的保留集合：

```cpp
constexpr Comet::Input::Key reserved_keys[]{Comet::Input::Key::Escape};
m_player_input_panel.open(project_defaults, player_overrides, reserved_keys);
```

```cpp
void open(const InputActions& defaults, const InputOverrides& current,
    std::span<const Input::Key> reserved_keys = {});
```

open 当场复制这些小型值，不保留 span，也不增加 policy 类或策略回调。
已经打开时仍不覆盖当前草稿；关闭后重新打开使用新集合，空集合不会继承上一次限制。

- 根窗口列出保留键；控制下拉禁用对应项，录入保留键给出提示并继续等待其他键。
- Escape 原先的取消录入／关闭面板优先级不变。
- 切换到一种来源时优先原默认控制；若该控制被保留，选择同来源首个可用控制；没有可用控制则不修改草稿。
- 已有有效默认或个人绑定使用保留键时显示警告，仍可原样 Apply、禁用、调参或 Restore；不能自动清掉它。

限制只在新控制选择／录入处检查，不塞进 store_binding、InputOverrides 或 PlayerInputSettings。
因此“键本身是否合法”和“这个宿主是否占用它”保持独立。
Editor 的项目默认面板明确说明当前 Esc 用途，但仍允许保存合法 Escape，未来其他宿主可以有不同策略。

## 031–035 架构回顾

| 范围 | 结论 |
| --- | --- |
| 无状态重复 | 031 将来源／控制菜单数据收敛到原 input_widgets；保留两种面板的草稿语义，不合并保存协议 |
| 数据与状态 | 032 的 disabled 与个人字段共存，033 的精确删除都使用原 InputOverrides；没有 UI 备份、自动清洗或第二套配置 |
| 生命周期 | 034 在 Input 保留失焦事实，原 Gate／录入跨跳帧识别；不在 App、Editor 各追加焦点回调 |
| 能力边界 | 035 由宿主传保留键，Engine 不知道退出按钮或 ImGui；UI 仅持值并生成候选 |
| 存储与 Runtime | 原文基线、草稿、待应用候选和有效映射有各自寿命；保留保存成功后更新边界应用，不为理论失败扩事务框架 |
| 目录与依赖 | 本组没有新增生产类／文件；共用 UI 不反向依赖 Editor，输入层不包含 UI；构建边界检查继续执行 |
| 后续收敛 | 项目默认面板仍以 ImGui 键转换录入，无法利用物理 interruption；下一项统一物理输入来源，删除重复键映射，不强行合并两个 UI 状态机 |

测试通过实际公开行为与 ImGui 测试上下文验证，不新增生产测试访问器。
README 保留使用说明，路线图仅同步能力状态；迭代细节留在本目录，不把临时日志目录变成公共接口。

## 验证

- Debug 全目标和 Release App 构建通过，未出现新增编译警告。
- 57 项定向 UI 通过：临时策略副本、空策略重开、保留选项不可选、录入拒绝后继续、
  已有记录完整保留与恢复，以及项目默认的合法 Escape 仍可保存。
- 完整 10 个 CTest 入口通过：1059 CPU、223 UI、159 常规 GPU、14 交换链恢复、
  43 渲染图同步与 2 后处理恢复用例，以及构建契约、模块边界、管线缓存和基准冒烟。
  CPU 轮询回退及 GPU 各向异性精度用例各有 1 项按现有平台条件跳过。
- 独立复核确认保留键策略未进入 Engine、文件协议或既有记录的 Apply／Restore 路径。
- 日志：`/tmp/comet-auto3-035-{build,ui,final,release}.log`。

## 限制

保留集合描述当前宿主的直接按键占用，不是操作系统组合键或多人控制方案框架。
App 的 Esc 退出和 Play 的 Esc 停止行为本身不改。已有文件不会因新增 UI 提示而被重写。
真实 App／Play 手工改键闭环仍未完成，自动回归不替代该验收。
