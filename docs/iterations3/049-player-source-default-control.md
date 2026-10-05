# 049 来源切回时优先项目默认控制

## 背景

047 修复跨来源返回轴时的死区继承，但来源菜单此前总以固定控制初始化：键盘用 Space，手柄轴用 LeftX。
因此项目默认 RightY 的绑定切成键盘后再返回手柄轴，会被改成 LeftX；默认 K 也会变成 Space。
这仍不是“回到项目默认”，并会留下非预期个人覆盖。

## 前后实现

此前 `first_control(source, reserved_keys)` 只知道目标来源和宿主保留键。
现在沿已有参数传入该绑定的项目默认控制：

```cpp
const auto controls = input_controls(source);
if(std::ranges::find(controls, default_control) != controls.end()
    && !is_reserved(default_control, reserved_keys))
    return default_control;
```

默认控制属于目标来源且可用时优先它；否则继续原来的标准候选及保留键回退。
例如默认 K 被宿主保留时选 Space，Space 也被保留时再选该来源的其他合法键。
不根据默认控制跳过宿主限制，也不扩大引擎的保留键集合。

同来源重选原本不调用 `change_control`，仍不改变个人值；同轴控制切换仍保留死区与倍率。
只有用户真的改变来源时应用上述初始化规则，不恢复此前个人来源历史。
例如默认 K、个人 J 切到手柄再切回键盘，结果是项目 K，不是暗中缓存的 J。

## 架构价值

只扩展已有 UI helper 的显式输入，不增加成员状态、保存字段、回调或平行默认表。
来源包含哪些控制仍由共享菜单提供；项目默认来自已打开的 InputActions 快照，宿主保留键仍由宿主传入。
047 的死区恢复、Overrides 合成和个人文件协议均不改。

## 验证

- 原来源往返用例改为非首项 RightY，覆盖控制与死区同时恢复默认。
- 覆盖默认 K 与个人 J 的来源往返，以及保留 K／同时保留 Space 的两级回退。
- 48 项玩家面板回归通过；1074 CPU／240 UI、Shader 构建契约和模块边界通过，1 项既有平台条件跳过。
- Debug 全目标和 app Release 构建通过，无新增警告；本项与 048 同一工作区验证，分别提交。

## 限制

这不是跨来源历史撤销功能；取消面板仍丢弃整笔草稿，Apply 保存明确的当前结果。
自动内存 UI 验收不替代 App／Play 的真实操作与设备手感。
