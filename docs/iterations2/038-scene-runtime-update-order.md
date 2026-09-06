# 038：场景运行时、固定更新与输入消费

## 背景与独立验收项

037 发布了输入帧，但 app 的场景修改仍混在 Application::on_update 中。
本项完成明确的 System 生命周期、Fixed／普通 Update 时序、有界追赶和输入边沿消费，
并让 app 与真实 Engine 渲染链路使用它；不是只为测试增加一个调度器。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| 应用回调同时做资产完成、退出、旋转、相机 | 应用保留宿主事务；DemoMotionSystem 承担场景更新 |
| 方块旋转直接使用墙钟帧增量 | 每次固定步推进，普通更新保留相机控制 |
| 无场景 System 启停顺序 | 正序启动、每阶段注册顺序执行、逆序停止 |
| 同帧多次固定更新可能重复按下操作 | 边沿只在首固定步可见，后续只有按住状态 |
| 无固定步的帧可能丢失短点击 | 累积到下一次固定步，按下／释放同时保留 |
| Scene 替换只交换指针 | 先停止 System，再替换 Scene，显式重新启动 |

## 代码级链路

```text
Window.poll_events → Input::Frame
  → Application 更新（资产完成／窗口操作）
  → Renderer.prepare_frame（UI 允许修改或替换 Scene）
  → SceneRuntime.advance
      → [所有 System.fixed_update] × 本帧固定步数
      → [所有 System.update] × 1
  → SceneExtractor → Renderer.render_frame
```

新增 runtime/scene_runtime.h/.cpp。System 与其唯一编排者在同一头文件，
不再另建 SystemManager、FixedUpdateDispatcher 或通用回调注册层。
SceneRuntime 拥有 System 的 unique_ptr，仅观察活动 Scene；Engine 拥有 Runtime 和 Scene。
独立使用时调用者须保证 Scene 覆盖 Runtime 的活动生命周期，全部入口在主线程执行。

### 时钟与过载

Settings 默认 fixed_delta=1/60、max_frame_delta=0.25、max_fixed_steps=8。
构造时拒绝非有限数、非法步长、无界的步数设置；设置目前是 C++ 构造参数，不增加 YAML 项。
Engine 当前使用默认配置，独立 Runtime 可通过构造参数复现不同步长。

advance 将有效墙钟增量加入 accumulator，每固定步按注册顺序调用所有 System。
达到步数上限后丢弃剩余整步，只保留小于一固定步的余量；避免卡顿后无限追赶。
Timing 同时报告帧序号、固定步序号／数量、普通时间、固定时间、余量比例和本帧 dropped_time。
普通时间累计截断后的帧增量，固定时间只累计真正执行的固定步，二者在过载时有意不同。
step×1e-9 的浮点容差只处理固定步边界取整，不是可变步长。

Context 的 input 是调用期间有效的 const 引用；需要留存的消费者必须复制 Frame。
index 是各阶段自己的序号，不复用平台输入 serial，也不是 GPU submission serial。

### 输入边沿

普通更新消费本次输入；固定更新消费 Runtime 自己持有的累积快照。
多个零固定步帧合并 pressed／released，按住状态和手柄轴取最新值，位移／滚轮求和。
首固定步后清除瞬态，后续固定步仍共享最新按住状态；所有 System 在同一步看见相同输入，
不是第一个 System 抢占后其他 System 无法读取。

重复提交同一 serial 不重复累积边沿或位移；倒退 serial 拒绝，重新 start 后允许新序列。
失焦取消待执行 pressed 和位移但保留 release，手柄断连也取消其尚未消费的 pressed。
这些规则不意味着完整事件次数／顺序回放，也不承诺跨不同采样时刻的输入自动得到相同结果。

### 生命周期与异常

仅停止状态允许增删 System；运行过程中不允许重新 advance、start、stop 或修改系统列表。
start 按注册顺序调用 on_start；若某个启动失败，连同该部分初始化的 System 一起逆序清理。
on_stop 为 noexcept，须支持部分启动，不能从清理过程修改正在执行的 Runtime。
固定／普通更新异常均逆序停止并向上传播，不自动重试可能已经修改了一半的 Scene。
stop 可重复调用；重新 start 清空时间余量、旧输入和序号，System 实例保留供显式再启动使用。

Engine::set_scene／replace_scene 在交换前 stop。replace_scene 不再 noexcept，
因为从 System 内重入替换必须明确拒绝，而不是销毁仍在执行的 Scene。
Engine 析构先销毁 Runtime，保证 on_stop 时 Scene、资产、设备和任务调度器仍然有效，
然后沿已有路径等待任务／GPU 并销毁资源。当前 System 不允许后台异步访问 Scene。

prepare_frame 暂时未得到可绘制目标时，非最小化窗口的运行时仍推进，不把一般 WSI 重试等同游戏暂停。
最小化窗口沿用原先的等待事件和计时重置路径，不累计长时间补帧。
诊断的 update_ms 合计宿主更新和 System 更新，render_submit_ms 不混入 System 执行耗时。

## 架构价值

- Scene 保留数据职责；System 表达一组实际更新行为，Runtime 定义执行和生命周期边界。
- app 的实体标识归 DemoMotionSystem，不再扩大 GameApp 的更新职责；示例 System 留在 app，不塞进 engine。
- 后续脚本、物理和音频可接入同一执行链；没有全局 EventBus，也没有默认并行访问组件。
- 帧准备后的场景修改和 System 更新都在 extraction 前完成，避免 UI／Play 换场景后渲染旧结果。

## 测试结果

- Debug／Release 构建通过，均无本次编译警告；CTest 各 6/6 通过，分别 17.60／13.59 秒。
- 主测试共 561 项／86 suites，560 通过；缺屏障同步对照在普通环境按设计跳过，专门同步测试中运行。
- 新增 10 项 CPU Runtime 测试：阶段顺序、逆序停止、零／多固定步边沿、重复 serial、失焦／断连、
  过载丢弃、时间分段重放、启动失败、更新失败、非法参数与重入保护。
- 扩展 2 项真实 Engine／GPU 顺序测试：UI 将实体移动到 x=10 或替换 Scene，System 再移到 x=0，
  当帧拾取／包围盒必须消费 System 更新后的场景；Scene 替换确实停止 Runtime，重新启动后只推进一帧。
- 037 Linux CI run 34066986088 已成功；038 CI 以本次推送后的实际结果为准。
- 10 项 Runtime 与 2 项帧顺序测试连续 10 轮，共 120 次通过；日志 /tmp/comet-038-repeat.log，耗时 1.32 秒。

本地日志：/tmp/comet-038-build.log、comet-038-release-build.log、comet-038-focused.log、
comet-038-debug.log、comet-038-release.log。未把本机验证冒充人工硬件验收。

## 限制与后续方向

下一项是 Editor Play 的运行控制与暂停／单步。当前不自动启动编辑器 Runtime，
不把编辑器相机、Inspector、资产热更新或渲染生命周期改成游戏 System。
尚未实现 Native Script 字段、游戏 Viewport 输入路由、dirty Transform、物理、音频或角色 demo。
Timing.interpolation 仅提供余量，未实现渲染 Transform 插值；固定序号不等于跨平台浮点严格确定性。
重复性测试只覆盖相同输入快照、相同总步长且无过载丢弃的单线程执行；持久回放格式仍在路线图。
