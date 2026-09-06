# 014：后台任务背压与最新请求合并

## 背景与前后对比

阶段 3 已有固定 Worker 池，但线程数量固定不等于负载有界：submit 可以无限入队，
连续修改同一资产也会排入多个 revision。后续 Shader 编译和更多导入任务不能继续依赖这个隐含前提。

| 之前 | 之后 |
| --- | --- |
| TaskScheduler 等待队列无上限 | 可配置 queue_capacity，默认 128 |
| submit 只会成功或异常 | try_submit 对队列满返回空；submit 保留异常接口 |
| AssetManager 直接提交每个新 revision | 有界等待队列，同 Handle 最新请求替换尚未执行的旧请求 |
| 同 Handle 可同时存在多个已提交任务 | 每 Handle 最多一个在途任务和一个后继请求 |
| 完成处理只收取结果 | 回收任务后继续派发已接收请求，不在主线程等待容量 |
| 析构只等待 futures | 先取消未派发请求，再等待已派发任务 |

本项是 admission／任务数量背压。每帧发布预算作为下一独立验收项，不以队列有界冒充帧耗时有界。

## 具体代码变化

`TaskScheduler(worker_count, queue_capacity)` 拒绝零容量。`try_submit()` 在同一 mutex 下检查 stopping 和等待数量，
有空位才创建 packaged_task 并入队；无空位返回空 optional。既不阻塞提交者，也不采用 caller-runs 回退。
`submit()` 调用它，未接收时抛出 runtime_error；原有 future 异常传递、wait_idle 和 shutdown drain 不变。
容量只计算等待任务，正在执行的任务另受 Worker 数限制。

`AssetManager::AsyncLimits` 的默认值为 in_flight=8、queued=128；构造时验证正数并预留在途槽。
`AsyncStatus` 是 owner 线程可读的数量快照，不读取或暴露 Worker 内部状态。

`schedule_refresh_task()` 的处理顺序：

1. 同 Handle／revision 已待处理时合并。
2. 有尚未派发的同 Handle 请求时替换为最新 revision 和值捕获闭包，保留原排队位置。
3. 没有可替换请求且等待队列已满时拒绝，返回 false 并记录重试日志；不修改 Runtime 对象。
4. 成功保存请求后，由 `dispatch_queued_tasks()` 尝试派发。

派发时再次检查数据库 revision；已删除／移动后过期的排队项直接取消。
同 Handle 的前一个任务尚未由 owner 回收时跳过，但允许其他 Handle 前进。
达到 AssetManager 在途上限或 TaskScheduler 无空位时保留请求，后续 `process_completions()` 再试。
Checking／Importing 表示已接收的整个处理阶段，可能仍在等待，不声称所有请求都已占用 Worker。

Worker 仍只持有路径／设置／Handle／revision 的值，不读取 Scene、数据库、Registry 或 GPU owner。
发布前的 revision 验证完整保留，过期任务结果不能覆盖最新对象。

## 架构价值与生命周期

- 通用调度器只认识容量和 Task，不加入资产类型、revision 或重导入策略。
- 资产层拥有最新请求合并与导入状态；不为负载控制新建一套资源管理器或全局 JobBus。
- 两级限制分别表达全局 Worker 等待能力和单个 AssetManager 的导入吞吐／待处理容量。
- 未派发闭包仍引用 AsyncState；AssetManager 析构先清空等待队列，显式解除自引用，再等待自己的 futures。
  未派发工作不会为关机临时挤入 Worker，已派发工作正常完成但不在析构中发布 GPU 对象。
- AssetManager 必须先于 Registry／Factory／TaskScheduler 销毁；这条既有所有权约束不变。

## 测试结果

- Debug／Release 全部构建成功，各 **413 tests** 通过；完整图形日志无 VUID／Validation Error。
- 新增 3 个 TaskScheduler 测试：满队列无 caller-runs／无等待及重试、16 个并发生产者遵守容量、零容量／空任务拒绝。
- 新增 6 个 AssetBackpressure 测试：在途／等待限制和拒绝后重试、全局队列拥塞时延后、多个 revision 合并、
  资产删除后派发前丢弃、析构取消未派发请求、非法限制参数。
- 3 个原“旧结果不能覆盖新结果”测试改为显式推进两次 owner 周期：先回收旧任务并派发最新请求，随后发布最新结果。
  最终 Runtime／Artifact、revision 和 GPU 创建次数断言不放宽；不再误把 scheduler.wait_idle 当成资产管线排空。
- 新增 tests/core/task_scheduler_test_utils.h：两个测试文件共用 BlockedWorker，用 promise 确认占用 Worker，
  RAII 在断言提前退出时也解除阻塞，不依赖 sleep 猜时序。这不是生产引擎类。
- 编译中的 nodiscard 测试警告已修正；clang-format／diff 检查通过。
- 012／013 Linux CI 已成功（34051356460、34051606376）；本项 CI 待推送后确认。

## 限制与后续方向

- 资产等待队列满时是明确拒绝，不承诺无限接收。显式导入由调用方重试；自动源刷新拒绝会记日志，
  旧 Runtime 保留，可显式重导入或在下一次源变化后重新请求。不能把扫描成功等同于全部重导入成功。
- 限制任务数量不限制单个模型的字节数或导入时长，不中断已经执行的 importer；后续按实际 streaming 需求再补字节预算。
- 当前完成结果与完成 future 仍是两条通知路径；本项维持既有发布逻辑，下一项在引入发布预算时收敛完成结果的所有权和回收时点。
- 发布预算必须同时约束过期／失败结果的处理，并避免未发布候选因提前归还在途槽持续积累；015 将一起验收并做定期架构回顾。
- TaskScheduler 公共接口支持并发 submit；AssetManager 的请求、查询、派发、发布仍只在 owner 线程调用。
