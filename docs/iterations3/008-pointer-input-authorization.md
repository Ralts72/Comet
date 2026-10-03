# 008：鼠标授权撤销后丢弃未消费输入

## 真实问题

Editor Play 允许键盘继续控制游戏，同时在鼠标离开 Viewport 画面时屏蔽鼠标。
Viewport 原本已经调用 `Gate::read(input, accepting, image_hovered)`，并不是缺少悬停判断。

问题发生在下游固定步积累：

```text
第 N 帧：点击／滚轮发生 → Update 消费，Fixed Update 尚未执行
第 N+1 帧：鼠标离开画面 → Gate 清零当前鼠标值，但 focused 仍为 true
随后 Fixed Update：RuntimeInput 把此前 pending 的鼠标输入再次交付
```

`focused` 仍为 true 是正确的，因为键盘和手柄还被授权。
真正缺少的是“鼠标没动”和“鼠标已失权”的区别，清零当前值不能表达后者。

## 代码变化

### 输入快照保留授权信息

在现有 Frame 中加入一项状态，不新建输入服务或路由对象：

```cpp
bool focused = false;
bool pointer_enabled = true;
```

原始窗口帧默认允许鼠标，但只有 focused 为真时才可用。
Gate 同时尊重上游帧和本次调用方的授权：

```cpp
const bool pointer_accepting = accepting && source.pointer_enabled && pointer_enabled;
next.focused = accepting;
next.pointer_enabled = pointer_accepting;
```

这样多个 Gate 串联时，后一个不能重新开放上游已经撤销的权限。
默认 true 也保持既有手工构造 `Frame{.focused = true}` 的语义，未改变 app 的正常窗口输入。

### 固定步只清理失权来源

以前物理 pending 是否接收旧鼠标边沿和位移，只取决于 frame.focused。
现在使用整体与鼠标授权的合取：

```cpp
const bool pointer_enabled = frame.focused && frame.pointer_enabled;
if(!pointer_enabled) {
    block_buttons(frame.mouse_buttons, previous.mouse_buttons);
    frame.cursor_delta = {};
    frame.scroll = {};
}
merge_buttons(pending.mouse_buttons, previous.mouse_buttons, pointer_enabled);
```

只有鼠标仍被授权时才累积旧 cursor_delta／scroll；键盘与手柄的合并代码不变。

具名动作也不能从全局 focused 推断鼠标可用：InputActions 对失权的 MouseButton 和 Motion
返回不可用 Sample。RuntimeInput 复用已经存在的 `!source.available → pending = {}`，
不另维护鼠标动作名单，也不全局 rebase。

因此一个同时绑定空格和鼠标左键的动作，只清掉鼠标来源，空格的有效短按仍会交付。
已经交付过的按住状态通过原动作合成产生一次释放；恢复悬停仍沿 Gate 原规则屏蔽已按住鼠标，
直到松开后再次按下才接受新点击。

## 职责与价值

```text
Viewport：判断画面悬停和 UI 归属
Gate：形成授权后的快照
RuntimeInput：积累／消费授权快照
InputActions：按绑定判断来源是否可用
System／Lua：只读取当前阶段结果
```

修复放在共享 Input 模块，因为固定步积累不是 Editor 的职责。
Editor 不需要主动清 RuntimeInput 的私有缓存，SceneRuntime 和 Lua 也没有新增条件判断。
没有改变输入动作组的优先级、公共组或同级共享语义，项目配置文件没有新增字段。

## 验证

生产修复前，公开 Gate → RuntimeInput 回归有 6 条断言失败：
物理点击／CursorX=5／ScrollY=2，以及相应具名动作都错误保留。
同一用例中的键盘、手柄与混合绑定保留断言通过，证明不是笼统的“清空所有输入”问题。
证据：`/tmp/comet-auto3-008-red-{build,targeted}.log`。

新增或扩展测试覆盖：

- 零固定步后仅撤销鼠标授权，鼠标 pending 清空，键盘／手柄及混合动作的键盘来源保留。
- 已按住 → 撤销 → Update／Fixed 各释放一次 → 恢复不伪造按下或位移 → 松开后新按正常。
- 串联 Gate 尊重上游禁用，重复读取不伪造新帧。
- 真实 Viewport 无窗口 UI 路由在鼠标仍位于工具栏时输出禁用标志，但保留键盘移动。

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='Input*:RuntimeInputTest.*:SceneRuntimeTest.*:ScriptSystemTest.NamedActions*:ScriptSystemTest.InputContextChangesWaitUntilTheNextFrameBoundary:ScriptSystemTest.DemoPaletteConsumesSharedControlsAndPreservesGameplayState'
build/tests/editor_ui_testing --gtest_filter='ViewportPlayUiTest.*'
cmake --build --preset app-release --parallel 6
GTEST_FILTER='-EditorAssetsTest.SourceMonitorRestoresBothConsumersAfterExternalModuleCreation' ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure -V
```

- Debug 全目标和 Release app 构建通过，无编译 warning/error。
- 70 项定向 CPU、6 项 Viewport UI 通过，原红测转绿。
- CTest 在命令行自带过滤器，覆盖了上述环境过滤值；实查日志确认实际运行了完整当前工作区，
  包括同步准备的 009 单项回归：962 CPU／159 UI 通过，1 个平台条件用例跳过。
  009 单独进入下一提交，不混入本项输入修复。
- `shader_build_contract`、`module_boundaries` 通过。
- 最终串行真实 GPU 冒烟 3 项通过：Engine 帧输入、暂停／单步及 Viewport 场景切换；
  90 秒外层超时，约 0.8 秒正常结束。三个无主相机警告来自既有空场景用例，没有 Vulkan 错误。
  这些用例不等于实际鼠标人工操作或完整 demo 音画验收。
- 实现代理之外的只读审查核对了授权链、逐绑定积累、释放及既有 Frame 初始化，未发现新的确定性问题。

日志：`/tmp/comet-auto3-008-first-{build,targeted,ui}.log`、
`/tmp/comet-auto3-008-final-{regression,release}.log`、`/tmp/comet-auto3-final-gpu-smoke.log`。

## 手动验证与限制

可把测试项目中一个 fixed_update 的按钮动作绑定到鼠标，操作后立即将鼠标移出 Play 画面，
观察离开后没有延迟点击或滚轮动作；键盘移动不应因此停止。
重新进入时一直按住的鼠标不能直接触发，松开后新按应正常。

零固定步时序由自动测试精确构造，手工不容易稳定命中；不以手动难复现否定已确认的错误。
本项不实现光标锁定、raw motion、多人设备分配或新的窗口焦点策略。
根 README 已检查：现有输入使用约定不变，不需要新增用户配置说明。
