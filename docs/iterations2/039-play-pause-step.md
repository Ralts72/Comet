# 039：Play 运行控制、暂停与单步

## 背景与验收项

038 提供了 System 更新时序，但 Editor Play 仍只有场景克隆和相机切换。
本项让 Play 启停真实 SceneRuntime，并支持暂停／恢复／单步；不把暂停做成第三种 EditorMode。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Play 只替换为克隆场景 | 同时启动克隆场景的 Runtime |
| Stop 只恢复 Edit 场景 | 先停止 System，再恢复原件 |
| 没有运行状态 | SceneRuntime::State 的 Running／Paused 与 Edit／Play 正交 |
| 想看下一步只能继续跑 | 单步推进一个固定步及一次同等时长普通更新，然后保持暂停 |
| 未定义暂停期间输入语义 | 不积累时间和输入边沿，恢复／单步只取当前按住状态 |
| UI 没有运行控制 | 读取 Runtime 状态，输出一次性 Pause／Resume／Step 请求 |

## 代码级变化

### SceneRuntime：冻结游戏，不冻结宿主

State 嵌套在 SceneRuntime，只有 Running 和 Paused；活动与否继续由已绑定 Scene 表示，
不额外添加 EditorMode::Paused，也不在 EditorState 保存第二份运行状态。

set_state 要求活动、非执行中的 Runtime；状态变化清空 accumulator、待处理单步和固定输入瞬态。
普通暂停帧仍读取当前输入 serial／电平作为新基线，但不调用任何 System，也不推进游戏时间／帧号。
暂停时间不算过载 dropped_time，因为它从来没有被接收为游戏时间。

request_step 只在活动且 Paused 时有效；重复请求合并为一条，不积累无界的单步队列。
下一次 advance 忽略墙钟增量，使用 fixed_delta 执行原有更新链：恰好一次 fixed_update，
再一次同等 delta 的 update。执行后仍 Paused，下帧无请求则不推进。
显式单步不受 max_frame_delta 的墙钟截断影响；resume／stop／start 会取消旧单步请求。

暂停、恢复首帧和单步都清除 pressed／released／cursor_delta／scroll，只采样当前 held／axes。
这避免暂停期间的编辑点击、滚轮和旧固定步输入在恢复时突然执行；恢复后的新输入帧仍正常产生边沿。
即使暂停与恢复之间没有 advance，也清除暂停前尚未消费的边沿和时间余量。
不允许 System 在执行中切换状态或申请单步，沿用 038 的重入保护。

### EditorSceneSession：克隆与运行生命周期对应

构造时注入 Engine 所有的 SceneRuntime，不新增 start／stop 回调链。
enter_play_mode：clone → 替换活动 Scene、保留 Edit 原件 → 标为 Play → Runtime.start。
exit_play_mode：Runtime.stop → 恢复 Edit 原件 → 标为 Edit。
Session 销毁时也停止 Runtime；Engine 自身仍保留最终清理保障，重复 stop 不重复执行 on_stop。

如果 System.on_start 抛异常，Runtime 逆序清理，Session 保留 Play 副本与 Edit 原件，
允许用户 Stop 恢复；不假装启动成功，也不丢弃原件。Editor 捕获错误后仍重新绑定当前 Scene，
避免面板保留上一个场景的引用。运行中 System 异常的上层交互恢复并未在本项扩展。

### ViewPanel：只读状态与一次性请求

ViewPanel 持有 const SceneRuntime&，不复制 Paused bool，不直接推进 System。
新增 RuntimeCommand（Pause／Resume／Step）和 take_runtime_command，延续既有 UI 请求出口。
Editor 在完成面板绘制后应用请求，随后 Engine 在 extraction 前 advance，因此本帧按钮操作即生效。
原先 Play／Stop 仍在宿主帧边界应用，保持场景替换与面板重新绑定的顺序。

按钮继续使用统一短宽度，不添加悬停说明：`||` 暂停，`>` 恢复，`|>` 单步。
单步仅暂停时启用；Edit 不显示运行控制，活动失败时禁用并标示 Play (Stopped)。
普通 Play、Paused 的模式标签仍在最左侧，原分辨率和 Fit／1x 下拉框保留。
UI／渲染／资产回调不在 SceneRuntime 内，因此暂停不阻止这些宿主工作。

## 架构价值

- 运行状态归 Runtime，场景克隆归 Session，UI 只表达请求；状态与生命周期各有单一 owner。
- 单步复用生产固定／普通更新链，不创建另一个测试专用执行通道。
- Play 只修改克隆场景，Stop 可以恢复 Edit 原件；运行时更新不进入编辑撤销历史。
- 没有 EventBus，也没有把渲染、资产后台任务或编辑器相机混入游戏 System。

## 测试结果

新增 9 项，主测试共 570 项／87 suites：

- 5 项 Runtime：暂停冻结两阶段；单步次数／时长；无中间暂停帧的恢复；请求取消／非法状态；显式步不受墙钟上限截断。
- 2 项 Session：固定更新只修改 Play 克隆，Stop 原件不变；启动失败后仍可 Stop 恢复。
- 1 项 ImGui UI：真实按钮发出一次性请求、运行中 Step 禁用、Edit／inactive 不执行，面板不自行改 Runtime 状态。
- 1 项真实 Engine／GPU：4 个宿主帧依次暂停、单步、保持暂停、恢复，UI／绘制全部继续，游戏帧号为 0、1、1、2。

首轮构建发现异常断言忽略 nodiscard 返回值的测试警告，改为显式丢弃后重新构建和验证。
未将这一警告隐藏为无问题首轮；无新增依赖或 Shader 修改。

最终 Debug／Release 构建均通过且无本次警告；完整 CTest 各 6/6 通过，耗时 14.48／13.25 秒。
主测试 569 通过，1 个缺屏障对照按设计留给专门同步验证；其余 GPU／WSI／构建契约／profile 项均通过。
15 Runtime、4 Session、1 UI 和 1 Engine 项合计 21 项连续 10 轮，共 210 次通过。
日志：/tmp/comet-039-verified-build.log、comet-039-verified-release-build.log、
comet-039-verified-debug.log、comet-039-verified-release.log、comet-039-repeat.log。
038 Linux CI run 34067499081 提交前仍运行中；039 CI 以推送后实际结果为准。

## 限制与后续方向

编辑器当前默认场景没有游戏脚本，因此暂停／单步不会凭空让静态对象运动；已有集成测试用真实 System 验证修改链。
下一项先补 Viewport 游戏输入路由并进行第 040 次定期架构回顾，再接 Native Script 字段和生命周期。
当前 Engine 的原始输入仍是整个窗口输入，不能宣称已隔离 Inspector／工具栏和游戏控制。
没有时间缩放、反向单步或状态快照回滚；暂停时 Inspector 调试仍可改变 Play 副本，Stop 不回写 Edit 原件。
