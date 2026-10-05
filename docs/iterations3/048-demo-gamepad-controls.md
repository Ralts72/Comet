# 048 demo 手柄玩法绑定闭环

## 背景

046 补上了手柄相机转向，但 demo 的玩法只给 `spin.toggle` 和 `palette.confirm` 配了 South。
玩家仍须用键盘移动方块、施加冲量、进入调色和重开；能控制相机不等于能走通玩法。

## 改动与数据流

不修改 Lua 或引擎 API，只在现有具名动作中追加项目默认绑定，各绑定有独立 UUID，已有身份保持不变：

| 动作 | 新增手柄控制 |
| --- | --- |
| demo.move_x | DpadRight +1／DpadLeft -1 |
| demo.impulse | West |
| demo.restart | Start |
| palette.toggle | North |
| palette.previous／next | DpadLeft／DpadRight |
| palette.reset | West |

例如移动脚本仍然只有一条消费路径：

```lua
local direction = comet.action_value("demo.move_x")
if direction ~= 0 then
    comet.translate(direction * self.parameters.speed * dt, 0, 0)
end
```

键盘 Right 或 DpadRight 都由 InputActions 转为正方向，不在脚本里判断 `is_gamepad`。
调色时，高优先级 palette 按具体控制消费方向键／十字键、J／West、Space／South：换色不移动方块，
恢复颜色不施加冲量，确认不切换旋转。公共的模式开关和重开、独立 camera 组仍保留。
得分后 gameplay 停用，所有设备上的对应游戏动作一起失效，而不是只屏蔽键盘。

## 架构价值

项目默认配置表达设备适配，System／Lua 继续只消费动作语义；个人覆盖按原身份合成，新增绑定可自然继承。
没有新增播放器、手柄管理器、回调或特殊场景分支，也没有为 demo 修改引擎默认按键。
使用首个已连接标准手柄的范围未扩大，多玩家与设备分配仍是独立规划。

## 验证

- 复用原来三段真实 demo 用例，通过一个小型 Keyboard／Gamepad 参数夹具输入对应物理控制，未复制场景装配和断言。
- 六项定向用例通过：旋转切换、调色消费／退出与重载释放、真实场景收集得分／暂停／单步／重开。
- 补充左右换色不移动、退出调色后左移、得分后右摇杆仍转向且不恢复 gameplay、不请求光标锁定。
- 与独立 049 改动联合工作区的 1074 CPU／240 UI、Shader 构建契约、模块边界及 Debug／Release 构建通过；1 项既有平台条件跳过。

## 限制与后续

合成标准手柄快照可验证真实脚本／物理／运行状态链路，但不替代具体设备的按钮映射和手感。
App 的 Input 按钮与 Editor 工具栏仍使用鼠标；本项不是手柄菜单导航或完整控制台 UI。
真实 App／Play 完整操作验收仍待桌面可用后继续。
