# 013：脚本退出时释放消费输入组

## 背景与真实复现

004 的 demo 用 Tab 开启 `palette`，高优先级消费 Right／Left、J、Space 等输入。
原测试覆盖确认退出、重复开关和同一 spin 脚本源码重载，但新实例的 `on_start` 恰好会关闭 palette，
掩盖了另一条合法路径：Play 中在 Inspector 将 Script 换为其他脚本或清空。

旧实例会停止，新脚本却不一定处理 Tab，palette 仍启用。结果是玩家的方向键一直被消费，
只有 Stop／重新开始才能恢复。新增 4 个真实 demo 用例在修复前均以玩家位移仍为 0 失败，
日志见 `/tmp/comet-auto3-013-red.log`；不是把“提示不够完整”包装成运行错误。

## 为什么不能只改 demo

原停止调用刻意没有 Scene：

```cpp
entry.instance->invoke(Script::Phase::Stop, {}, entry.parameters);
```

而 `comet.set_input_context` 通过 Scene 排队。因此仅增加 Lua `on_stop` 会报错，不能释放组。
也不能直接把 Scene 传进去：实例可能已经失去实体，且这会一起开放创建实体、会话、通知等无关能力。
自动重置所有组同样不对：收集目标主动关闭 gameplay，目标随后销毁，终局状态应继续保留。

## 受限输出与宿主应用

在现有 Invocation 上增加一个借用的、只供 Stop 使用的输出：

```cpp
std::vector<std::string>* disabled_input_contexts = nullptr;
```

容器由宿主持有、位于 Lua 保护调用之外，不是长期队列或第二个 Scene 指针，也不是回调。
Lua 仍调用同一接口，但停止阶段只接受关闭：

```lua
function script:on_stop()
    comet.set_input_context("palette", false)
end
```

绑定复用 `InputActions::valid_name` 和 `MAX_CONTEXTS`，去重并限制最多 32 个名字；true 会明确报错。
`Script::Instance::invoke` 在 Stop 清空 Entity、Scene、Input 和材质服务，即使宿主误传宽上下文也不会开放它们。
普通阶段不接收该输出，仍走原 `Scene::request_input_context`。

ScriptSystem 区分两种停止原因：

```cpp
enum class StopReason { LiveChange, Shutdown };
```

- `LiveChange`：运行中的组件移除、换绑、成功源码换代；回调后把记录的组名以 false 交给现有 Scene 队列。
- `Shutdown`：完整 Runtime Stop、失败清理、析构；运行 Lua 清理但不向即将退出的场景发新请求。

```text
旧实例 on_stop
  → 记录关闭组名（没有 Scene 权限）
  → ScriptSystem 按停止原因处理输出
  → 新实例 on_start（若有）
  → 下一次 advance 输入准备
  → RuntimeInput 更新路由
```

不改变同一帧所有 fixed/update 消费同一输入快照的协议。旧 Stop 的 false 先于新 Start 的请求，
相同组仍按既有 last-write-wins 生效，不在脚本回调中即时切换路由。
Stop 后续出错时继续记录错误并应用此前已接受的关闭项，防止一个无关清理错误留下输入消费。
未声明的组名仍由原 RuntimeInput 边界报错，不再维护一份可漂移的配置副本。

## 验证

- 修复前：4 个使用 demo 原脚本与项目输入配置的 CPU 用例均失败；清空与换绑后，玩家仍被 palette 消费。
- 修复后：同样 4 项通过；脚本调用、System 生命周期与 RuntimeInput 共 89 项定向测试通过。
  同一 advance 的多固定步仍不提前释放，下一次 prepare 恢复；显式停用的 gameplay 不被重开。
- 清理边界覆盖：有效组名／数量上限、重复去重、禁止开启、失败前输出保留、无 Scene／实体权限、
  普通阶段仍走 Scene；旧 Stop 后新 Start 的同名请求覆盖、逆序清理及完整 Stop 后再次运行均通过。
- `cmake --build --preset dev-debug --parallel 6` 全目标通过；`app-release` 构建通过，无新增编译警告。
- `ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure -V` 通过：987 CPU、165 UI；
  1 个既有轮询回退用例因原生平台条件跳过，Shader 构建契约与模块边界检查通过。
- 两份只读交叉审查未发现阻塞问题。短期输出无长期所有权，Lua 保护调用外持有容器，
  记录 helper 不调用 Lua；本项没有扩展 Runtime、Scene 队列或增加输入配置副本。
- 日志：`/tmp/comet-auto3-013-{red,build,targeted,regression,release}.log`。
  未重复 GPU 冒烟或声称已人工验证真实窗口操作；本项直接证据是实际 demo 脚本、输入与实体位移的 CPU 链路。

## 价值与限制

- 修复真实消费者的生命周期缺口，没有增加全局输入 owner、自动互斥栈或事件总线。
- 公共动作和其他组不被重置，项目继续决定哪些状态应保留；只有声明了清理的脚本释放相应组。
- 不为停止回调开放任意世界访问；宿主明确接收输出并选择应用时机。
- 这不是通用 Lua 状态迁移、脚本资源所有权框架或完整 Gameplay UI；多个脚本共同控制同一组仍需项目协调。
- 手动验收可在 demo Play 按 Tab，然后把 Editor Cube 的 Script 清空／换绑，下一输入准备后检查玩家操作恢复；
  该交互尚不能由 CPU 通过声称为真实窗口人工验收。
