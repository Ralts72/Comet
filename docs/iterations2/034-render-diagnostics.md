# 034：CPU/GPU 帧诊断与低频显存观察

## 背景与验收项

033 已形成 Shadow、Scene、Bloom、tone mapping 的实际多 pass 链路，但原先只有 FPS 与可选 CPU scope 累计日志。
无法区分主线程等待、场景图录制和 GPU 执行，更没有按需查看 VMA 分配明细的入口。
本项完成阶段 5 的必要诊断验收，为下一项线程／生命周期回顾提供数据入口；不是优化结论或完整 GPU profiler。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| FPS 只反映帧间隔 | Engine 发布上一完整循环的 events/update/prepare/render-submit CPU 墙钟分段 |
| CPU scope 汇总依赖编译开关 | 独立运行开关控制 RenderDiagnostics，Release 也能采样 |
| 不知道图中 pass 的 CPU/GPU 时间 | CPU 回调计时、GPU 时间戳及完整图总时间，明确标注 submission |
| 没有 GPU query 生命周期协议 | 查询池绑定 FrameScheduler slot，完成 serial 后才读回 |
| VMA 预算主要用于上传预算决策 | 诊断至多每秒获取一份预算快照，显示驱动／估算来源 |
| 没有详细分配报告入口 | 手动请求原生 VMA JSON，编辑器负责原子保存，日志区反馈 |
| 所有新增面板默认可见 | Render Stats 默认隐藏，View 菜单支持初始可见状态 |

## 代码级逻辑

### 整帧 CPU 与图录制不是同一个指标

`Engine::FrameTiming` 位于 Engine 类内，是对外只读的上一循环快照，没有另起 data 文件。
events 包含 GLFW 事件处理／窗口尺寸检查，update 包含 Timer 与更新回调，prepare 包含上传完成处理、frame slot 等待和 UI，
render-submit 包含 Scene 提取／解析／绘制录制／提交及呈现调用。总时间为这些墙钟区间之和，包含等待而非只计 CPU 运算。
prepare 失败仍发布一条 rendered=false 的快照；最小化等待不混入正常帧样本，关闭采样后清除整帧快照。
on_update 改为非 const，与记录运行状态的事实一致；原有更新→准备→提取→渲染顺序不改变。

`RenderDiagnostics::record` 包围现有 RenderGraph::Plan::record，记录完整图的 CPU 录制时间及每个回调耗时。
它不包含 SceneRenderer 在调用前准备 bindings/LightingData 的时间，也不包含编辑器 overlay 的 GPU 绘制。
CPU pass 明细之和不必等于图总时间，后者还包含图 barrier 录制、循环和计时命令开销。
这里没有替换原有 Profiler scope 汇总，也没有新增全局单例。

### QueryPool 与完成证据

RenderDiagnostics 构造时绑定一个既有 FrameScheduler，而不是每次接受一个可混淆的调度器；Device 必须比它及在途帧活得更久。
检查该 graphics family 的 timestampValidBits 和设备 timestampPeriod，支持时才允许 GPU 采样。
每个 slot 懒创建一个 timestamp QueryPool，容量固定为 33：图开始一个时间戳，每个 pass 结束一个时间戳，最多 32 pass。

第一次写入前录制 resetQueryPool，开始使用 TOP_OF_PIPE，pass 后使用 ALL_COMMANDS 时间戳。
GPU pass 时间取相邻边界之差，包含执行依赖／等待，并不是各 pass 独占硬件的 busy time；最后减最开始是图总时间。
只有整个图录制成功后才为这个 slot 保存 pending serial 和 owned pass 名称，异常不会发布半条完成样本。

读取条件是 `FrameScheduler::is_frame_serial_complete(pending.serial)`；使用 64-bit + availability 结果，不设置 WAIT 位。
这很重要：新一轮 reset 命令尚未执行时，查询池可能仍保留上一轮 available 结果，不能仅凭 availability 判断当前帧完成。
具体语义核对了 [Vulkan 查询规范](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/chapters/queries.adoc#queries-wait-bit-not-set)。
FrameScheduler 只有既有 slot/fence 完成确认后才推进完成 serial；计时模块不调用 wait_idle 或额外 fence wait。

时间差按 valid bits 做无符号模运算，再乘 timestampPeriod 换算毫秒，避免 64 位移位未定义行为并处理单次计数器回绕。
先读最新完成的旧样本，再为当前 slot 录制 reset；当多个 slot 同时完成时只用更大的 serial 更新 GPU 快照，不倒退到旧帧。
GPU 快照与当前 CPU 快照可能相差数帧，面板分别显示序号，不伪装成同一帧的同步样本。

### 降级、边界与生命周期

关闭时直接执行原图，不查询预算或创建计时池；设备不支持时间戳、显式 allow_gpu=false 时仍保留 CPU 采样。
超过 32 pass 的图继续执行全部 pass，CPU 保留前 32 条明细并标记截断，该图不做 GPU 时间戳采样。
QueryPool 创建／查询的普通诊断错误关闭 GPU 计时并发出一次警告；device lost 继续抛出，不当作普通降级吞掉。
录制中修改开关、收集查询或递归录制均拒绝；有时间戳的同一帧不允许重复使用同一批查询槽位。

QueryPool 的实际 owner 由 FrameSlot retain，销毁 RenderDiagnostics 不会提前销毁已录制命令引用的池。
slot 回收遵循已有 fence 协议，没有新建通用退休队列、后台渲染线程或跨队列调度。
一次查询还未 available 时不阻塞；后续 slot 复用可能跳过该条样本，采样从来不是渲染正确性的前置条件。

### 内存预算与按需报告

`poll_memory` 使用 steady clock，开启时首次采样，此后间隔至少一秒；getter 与面板 render 本身不触发驱动/VMA 查询。
MemoryBudgetSnapshot 沿用现有结构，逐 heap 显示 allocation、block、usage、budget；driver_reported=false 时明确标为 VMA estimate。
预算是观察值，不被解释成全部可立即分配的显存，也不替代 UploadManager 的现有预算控制。

`Allocator::build_allocation_report` 调用 VMA 原生详细统计，并以 RAII 释放 VMA 返回的字符串；Device 只转发该能力。
渲染／分配器不写项目文件。Render Stats 只产生一次性请求，由 Editor 主线程调用报告 API，
通过已有 write_text_file_atomic 保存到 `.comet/editor/diagnostics/gpu-allocations.json`。
手动再次保存会原子替换上次报告，失败保留既有文件并写 Log；不每帧生成大字符串，也不在 Inspector 内写日志。
该 JSON 是 VMA 原生诊断格式，不是本次迁移 scene/material/meta 的项目格式，也没有为此添加 JSON 依赖。

### 面板与配置

新增 editor/panels/render_stats.h/.cpp 是真实独立面板职责；头文件只前置声明 Engine，不把整个引擎头传播给面板消费者。
默认隐藏，通过 View → Render Stats 打开；MenuBar 的初始可见参数默认为 true，既有面板行为保持。
Capture 与 Save allocation report 复用已有 take_request 消费方式，没有增加全局 EventBus 或渲染生命周期回调。
关闭 Capture 后保留最后的图样本并明确提示；CPU 整帧快照在后续未采样循环清除。

`diagnostics.enable_render_diagnostics` 与旧 enable_profiler 独立：dev-debug/editor-dev 开启，裸 Config 和 app-release 关闭。
诊断代码不依赖 COMET_ENABLE_PROFILER，所以可在不带 scope 汇总的 Release 中测量实际图。

## 架构价值

- Engine 记录循环分段，RenderDiagnostics 记录图，Allocator 提供原生报告，Editor 决定存放位置，各层不倒置依赖。
- 查询完成证据复用已有 FrameScheduler serial；不能为了测量而改变 frames-in-flight 策略或制造 CPU/GPU 同步停顿。
- 有界 slot/query/pass 数据与旧帧 owner 协议一致，诊断对象可以独立销毁。
- 明确单位、采样时刻、延迟及数据来源，为是否引入 RenderThread 提供可解释证据，而不是只有一个混合 FPS 数字。

## 测试结果

最终 Debug／Release 构建成功；各自完整 CTest 均 5/5 通过，耗时分别 16.45／10.66 秒。
单元测试共 539 项（正常运行时 538 通过，1 项缺失 barrier 的专门同步对照按设计跳过）；
独立同步验证覆盖 25 GPU 项，另有 10 WSI 项和两个独立契约入口。
真实 GPU 时间戳测试在本机确实执行通过，并非因 unsupported 跳过。

随后单独执行 `GTEST_REPEAT=20 ctest --preset dev-debug -R '^render_graph_sync_validation$' --output-on-failure --timeout 120`，
首轮旧有 `OffscreenSceneExportsSampledLayoutAcrossMsaaAndResize` 在重建 Engine 时停滞，120.05 秒超时，exit 8。
此前本步六个诊断 GPU 测试均已通过，但这不能算 25×20 重复验证通过。
进程采样确认 `Engine::Engine → glfwInit → _glfwInitCocoa → NSApplication::run`，与 032 的生命周期问题路径一致；
没有进入本步 query API。没有静默重跑，下一项 035 优先修正 Window 与 GLFW 全局生命周期的归属，再做压力回归。
原始本机日志：`/tmp/comet-034-verified-debug.log`、`/tmp/comet-034-verified-release.log`、
`/tmp/comet-034-repeat.log`、`/tmp/comet-034-repeat.sample.txt`；临时日志不是仓库交付物。

- 纯 CPU 测试覆盖周期换算、有效位掩码、64 位及窄计数器回绕、非法位数与非有限 period。
- 真实 GPU 测试循环复用两个 slot，录制后／提交但未确认完成时读取不得前移 serial；完成后才得到对应 owned 名称和时长。
  不支持时间戳的设备显式跳过此项，不能将 CPU fallback 算作真实 GPU 计时通过。
- CPU-only 模式执行超过明细上限的图，验证全部回调仍执行；回调异常后可恢复，录制中的开关／收集请求被拒绝。
- 查询 owner 在诊断对象销毁后仍可提交，Vulkan validation 检查没有提前销毁查询池。
- 内存测试验证每秒上限、关闭时不采样，以及详细报告随真实命名 allocation 的创建／释放而变化；没有声称测试已解析全部 JSON schema。
- Engine 实际运行三帧，验证 CPU 分段之和、真实 Scene 图 CPU/GPU 样本；UI 测试验证默认隐藏、Capture 与保存请求只按交互产生。
- 首次构建遗漏了 engine 显式源列表中的新 cpp，出现链接失败；补入后重新构建，未拿旧二进制冒充通过。
- 未注入真实 GPU device loss／QueryPool OOM，也未声称完成手工窗口视觉巡检。

## 限制与后续方向

GPU 指标覆盖 SceneRenderer 图，不含 editor overlay、present 完成或其他队列；不是完整 GPU 利用率、设备/主机校准时间线。
计数器差值只能解释短于完整回绕周期的区间；没有记录超过该周期的长任务或多次回绕。
CPU/GPU 计时都有自身开销；validation 开启的小测试不是用户项目性能基准。
VMA 预算/报告只反映其管理范围及驱动预算，不包含所有外部 Vulkan 对象分配；详细报告可能较大，手动保存会占用主线程时间。
下一项 035 做定期及阶段 5 边界回顾：目录、职责、依赖、冗余、生命周期，并基于代表场景测量评估线程演进。
032 的 Cocoa 初始化停滞继续作为独立生命周期审查项，不能因诊断测试通过而忽略。阶段 6 原目标保持不变。
