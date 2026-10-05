# 046 相机持续转向与手柄右摇杆

## 背景与前后变化

此前 demo 手柄能移动和升降，相机转向却只消费鼠标 `Delta`。不能把右摇杆塞进
`camera.look_x/y`：摇杆值表示持续方向和幅度，鼠标值表示这一帧已发生的位移，单位不同。

此前：

```cpp
if(look && look->down && !look->pressed) {
    rotation.x -= value("camera.look_y") * controller.look_sensitivity;
    rotation.y -= value("camera.look_x") * controller.look_sensitivity;
}
```

现在先分别换算成角度，再沿用同一俯仰限制和偏航环绕：

```cpp
auto turn = Math::Vec2(value("camera.look_rate_x"), value("camera.look_rate_y"))
            * controller.look_speed * delta_time;
const bool mouse_look = look && look->down && !look->pressed;
if(mouse_look)
    turn += Math::Vec2(value("camera.look_x"), value("camera.look_y"))
            * controller.look_sensitivity;
```

持续转向满量程默认 120 度／秒；0.2 秒一次更新和 0.1 秒两次更新得到相同转角。
鼠标仍不乘时间，按下转向动作首帧只跳过鼠标位移，不丢掉同帧手柄输入。
相机仍只在普通 `update` 中执行一次，不因本帧有多个物理固定步而重复转向。

## 逐块代码与职责

### 组件与编辑器

`CameraControllerComponent` 新增 `look_speed`，`look_sensitivity` 的语义和存储名不变。
组件注册表为新字段提供“持续转向速度（度／秒）”编辑标签和数值提示；鼠标字段明确标为鼠标灵敏度。
字段走已有属性事务、撤销／重做、组件快照和场景序列化，没有新增 Inspector 分支或专用命令。

`look_speed` 是可选字段，场景未写时使用组件默认值；没有另建版本兼容解析器。
运行控制器与原有速度／灵敏度一样拒绝非有限值或负数，避免非法数据传播到 Transform。

### 输入契约与 demo

```text
project.json: camera.look_rate_x/y（Axis，RightX/RightY，deadzone=0.15）
    → 项目默认 + 个人稀疏覆盖
    → RuntimeInput / 动作组 / Gate
    → CameraControllerSystem
    → 当前主相机 Transform
```

新增动作和绑定各自有独立 UUID，不修改已有身份；旧个人文件自然继承新增默认项。
持续轴也能绑定普通数字按键，控制器不检查 GLFW、手柄型号或物理来源。
类型不匹配沿用明确的运行错误；缺少动作仍等于未绑定，因此其他项目不必配置手柄。

只保留键盘／手柄授权而关闭鼠标授权时，持续转向仍正常；完全失焦／拒绝输入时停止。
右摇杆转向无需 `camera.look` 按钮，不请求光标捕获；044 的鼠标捕获契约不变。
暂停不推进相机，单步使用本次固定步时间，恢复继续使用同一动作配置。

## 架构价值

- 扩展既有输入消费者与组件，不新增 System、Manager、设备判断或宿主回调。
- 保持位移和速率的量纲边界，避免帧率依赖和暂停期间累积一大段转角。
- App 和 Editor Play 使用同一路径；Edit 相机不变，停止 Play 不回写场景。
- 个人配置、动作组和运行中重绑定不需要相机专用适配。

## 测试与验收

- 相机回归：帧时间拆分、可调角速度、非法数值、鼠标与手柄混合、零时间、死区、反向、断连／失焦、指针单独撤权。
- Runtime 回归：暂停／单步、不重复积分多个固定步、暂停中重绑定、保留已禁用动作组、空输入停止转向。
- 既有属性事务用例补充新速度的撤销／重做、组件删除恢复和场景保存读取，不为属性另建夹具。
- 首轮 3 项测试把浮点转角当成字节精确相等；带死区的 23.999998° 与 24° 出现差异，改为 0.00001° 容差后通过，没有修改运行算法。
- 55 项定向 CPU 回归通过；完整 1071 CPU／236 UI、Shader 构建契约和模块边界通过，另有 1 项既有平台条件跳过。
- Debug 全目标和 app Release 构建通过，无新增警告；独立只读审查未发现新的职责或生命周期问题。

## 限制与后续

没有连接真实标准手柄进行手感验收；合成输入只证明映射和运行边界，不能证明设备映射或摇杆手感。
仍使用首个标准手柄、独立单轴死区与现有倍率；不扩径向死区、加速曲线、多玩家分配或角色控制器。
真实 App／Play 的玩家改键完整操作仍单独待验，不因本项自动测试通过而标记完成。
