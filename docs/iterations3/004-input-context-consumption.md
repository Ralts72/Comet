# 004：输入组优先级与按绑定消费

## 背景与验收目标

原来的具名输入组只能独立启停。一个交互模式若想占用方向键和空格，必须在项目脚本中
手动禁用 gameplay；退出时再打开可能错误地覆盖“已经得分所以禁止移动”的游戏状态。
同时，RuntimeInput 只有 action 级的固定步积累，无法在同一动作的一个绑定被阻挡后，
仍保留另一个绑定尚未消费的短按。

本项把仲裁放进已有 input 模块，System／Lua 继续只查询 InputState。项目默认配置、编辑器草稿、
Runtime 和 demo 使用同一套规则，不增加 InputManager、全局事件总线或回调链。

## 1. 项目配置前后

原来：

```cpp
struct Context {
    std::string name;
    bool enabled = true;
};
```

现在增加两个有默认值的字段：

```cpp
struct Context {
    std::string name;
    bool enabled = true;
    int priority = 0;
    bool consume = false;
};
```

```json
"input_contexts": [
  {"name": "gameplay", "enabled": true},
  {"name": "camera", "enabled": true},
  {"name": "palette", "enabled": false, "priority": 100, "consume": true}
]
```

省略新字段继续是 0／false，不需要转换旧项目。只有 enabled 且 consume 的组参与屏蔽，
同一个物理 control 的最高消费优先级屏蔽严格低优先级组；同级共享，与 JSON 数组顺序无关。
绑定的 scale／deadzone 不改变 control 身份，按键、鼠标按钮、手柄按钮／轴和位移都使用同一规则。
消费组占用自己已绑定的控制，不要求当前按键处于按下状态。

无 context 的公共动作始终独立：既不被消费，也不阻挡其他组。没有绑定的组不占用任何输入。
这不是“优先级高就抢走整个键盘”，也不是自动互斥的模式栈。

Project 的读取／保存新增字段；优先级允许负数并检查 int 范围，公共 JSON 工具补有符号整数读写，
不另写输入专用 JSON 序列化器。名称、启动场景等其他项目设置保存仍完整保留输入配置。

## 2. 仲裁与采样共用同一实现

```text
Project::input_actions
  → SceneRuntime::set_input_actions
  → RuntimeInput
       活动 Context → InputActions::resolve_routes → 每个 action 的 BindingMask
       Gate 授权帧 → InputActions::sample → 每个绑定的 Sample
       Sample + 阶段历史 → evaluate_samples → 只读 InputState
  → System／Lua
```

InputActions 的新路由和采样类型都是私有实现；公开新增的只有 Context 配置字段。
公开 evaluate 和 RuntimeInput 共用采样／合成逻辑，不在 Editor、CameraController 或 Lua 中重复判断模式。
路由只在 Context 开关改变后重算，普通帧直接复用。当前采样使用可复用工作缓冲并逐槽覆盖，
避免每帧重新分配各绑定 vector；pending 仍是唯一跨帧历史，不新增第二份动作状态。

原来 RuntimeInput 记录 changed_contexts，仅能处理显式启停的那个组。
现在比较前后每个绑定是否可用，因此也能处理“palette 开启使仍启用的 gameplay 间接失权”。
组变化继续由现有 `comet.set_input_context → Scene 请求 → 下一次 advance` 生效，
不在同帧的多个 Fixed Update 或不同 Lua 回调之间更换规则。

## 3. 为什么固定步积累改成逐绑定

原来的合成顺序是：

```text
多个物理绑定 → 一个 action 值 → pending action
```

此时 action 的历史已经丢失来源。比如跳跃绑定 Space 和手柄 South，Space 被菜单接管后，
既不能把整个 action pending 清空（会丢 South），也不能全部保留（会重放菜单里的 Space）。

新的顺序是：

```text
每个绑定的采样 → 各自 pending → Fixed 阶段合成 action
```

prepare 按实际路由变化处理：

- **失去路由**：清掉该绑定积累；动作最后一个按住来源丢失，Update／Fixed 各产生一次必要释放。
- **新获路由**：取得当前 down／axis 电平，清 pressed／released 和 Delta，不把启用前的点击补发。
- **保持路由**：保留该绑定的短按与 Delta 积累，包括零固定步期间尚未交付的输入。
- **设备失效**：清该设备的待消费数据；首个可用手柄换槽时不把旧槽边沿交给新槽。

因此持住确认键退出调色模式后，游戏动作可以看到 held 电平，但不会误认为又按了一次空格。
只有重新松开再按下才切换旋转。多次固定更新也不会重复收到同一点击。

原始物理 pending 仍保留，供 InputState 的现有只读物理查询使用；动作不再从它重新映射，
避免把已被屏蔽的历史输入重新放回动作流。暂停／恢复／单步沿用既有电平基线协议。

## 4. 编辑器与真实消费者

InputSettingsPanel 沿用 Context 草稿，新增“优先级”和“消费输入”。保存继续走 ProjectSettings，
UI 不写文件也不操作活动 Runtime；项目默认值在重新启动 app／Play 后使用。新增显示文本含中文映射。

demo 的旋转脚本使用局部辅助函数和已有材质覆盖接口：

```lua
local function set_palette_active(self, active)
    self.palette_active = active
    comet.set_input_context("palette", active)
    if active then
        apply_palette(self)
    end
end
```

这里没有 `set_input_context("gameplay", false/true)`。
Tab 是公共动作，开关 palette；左右选择颜色，J 重置颜色，空格／South 确认退出。
这些控制与 gameplay 的移动、冲量和旋转开关重合，实际由优先级消费区分。
WASD 相机不与该组绑定重合，R 是公共动作，二者仍可用。

得分已禁用 gameplay 时，调色退出也不重新启用它。脚本成功重载会重建 self，并请求关闭 palette；
下一输入边界恢复路由，不回滚已经写入 Scene 的颜色，也不迁移任意 Lua 状态。
源码候选失败不执行新版 on_start，因此仍保留原实例和模式。

## 5. 架构回顾

- 配置归 Project／InputActions，活动组和阶段历史归 RuntimeInput；Scene 只保留既有待提交请求。
- 动作组仲裁留在 input，不扩张 Engine／Editor／SceneRuntime 的上层组合代码。
- 按绑定历史替换原 action pending，不同时维护两份动作积累。
- Lua 规则和调色素材属于 demo，不增加引擎内置“调色 System”。
- 沿现有文件与消费接口扩展，未增加新的业务类或目录。

## 6. 验证

执行：

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='InputActionsTest.*:RuntimeInputTest.*:ProjectTest.*:ScriptSystemTest.*:SceneRuntimeTest.*'
build/tests/editor_ui_testing --gtest_filter='ProjectInputUiTest.*:ProjectSettingsUiTest.*'
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure
cmake --build --preset app-release --parallel 6
```

Debug 全量／Release app 构建通过且无编译警告；106 项定向和 7 项 UI 定向通过；
完整相关回归为 940 CPU、156 无窗口 UI 通过，Shader 构建契约和模块边界通过。
原生通知平台下的轮询回退测试按条件跳过 1 项。

重点覆盖：同级顺序无关、公共动作独立、全部控制类型、partial gain／lost 保留其他绑定、
零／多固定步、开关往返、暂停／rebase、设备换槽、失焦和 serial 回退；项目输入整数边界及保存往返，
UI 编辑／保存／关闭的草稿保留。实际 demo 集成覆盖方向键被消费、J 不施加冲量及退出后恢复、
确认键持住不切换旋转、源码重载退出模式、已禁用 gameplay 不被重新打开，以及模式内公共 R 重开。
最终日志为 `/tmp/comet-auto3-004-second-{targeted,ui}.log` 和
`/tmp/comet-auto3-004-final-{build,regression,release}.log`。

本项未进行真实窗口、GPU 和音画交互验收；上述结果只证明配置、UI 控件和运行逻辑链。

首轮定向 104／105 通过，UI 7／7 通过。失败来自 demo 辅助函数误用：生命周期定义表不是 self 的原型，
`self:set_palette_active` 在运行中不存在。改为本地 Lua 函数显式接收 self，保留原实例协议；
修复后实际调色流程的定向测试通过，而非放宽测试或跳过该功能。
初次失败和错误详情分别保留在 `/tmp/comet-auto3-004-first-targeted.log` 与
`/tmp/comet-auto3-004-demo-diagnostic.log`。

## 7. 手动使用与限制

1. 启动 demo 的 app 或 Editor Play，按 Tab：旋转方块变为蓝色。
2. 按左右切换蓝／橙／紫，玩家方块不应移动；按 J 回到蓝色，冲量方块不应因此弹起。
3. 按住空格确认退出，旋转不应被这次确认切换；松开再按空格才切换。
4. 调色中仍可用 WASD 控制相机、用 R 重开；得分后再调色并退出，玩家仍保持停止。
5. 在输入项目设置修改 priority／consume，保存并重新 Play，检查实际仲裁遵从配置。
6. 暂停时不执行调色脚本，单步不重放点击；源码重载成功退出调色，错误候选保留旧实例。

尚未支持运行中修改 priority、消费手势的一部分、玩家设备分配、游戏内改键 UI 或通用菜单栈。
无窗口测试不代表实际音画与完整交互已经手工验收。
