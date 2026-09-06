# 015：完成发布预算与在途结果所有权

## 背景与前后对比

014 限制了任务 admission，但原实现仍先把 Mesh／Texture 结果放入完成队列，再另行回收 future。
若只给完成队列加每帧条数限制，却继续提前回收 ready future，就会不断归还在途额度，留下越来越多未发布候选。
本项把候选与完成信号的生命周期收敛，再增加发布预算。

| 之前 | 之后 |
| --- | --- |
| 两个完成 deque + mutex + future 列表 | 每个在途槽同时持有 future 与独立 ImportResult |
| Mesh 批次后再处理 Texture 批次 | 同一个就绪任务遍历，共用数量和时间预算 |
| future ready 后即可回收槽 | 结果发布／检查／丢弃完成后才回收槽 |
| Worker 闭包共享 AsyncState | Worker 只持有自己的结果和请求值，AsyncState 为 unique_ptr |
| 一次消耗当前完成批次 | 默认最多 2 个结果、约 2 ms 非抢占软预算 |

## 代码级说明

`ImportResult` 是 AssetManager 私有的传输数据，使用 variant 保存 MeshArtifactCandidate 或 TextureImportCandidate，
不公开给 Scene／Renderer，不另建公共类文件。`QueuedAssetTask` 保存 `void(ImportResult&)` 的值捕获计算，
只有实际尝试派发时才创建独立结果容器。

`dispatch_queued_tasks()` 向 TaskScheduler 提交的闭包只执行 `task(*result)`。
ScheduledAssetTask 持有同一 result 和 completion；Worker 完成 future 后，owner 才能读取结果。
future 的同步关系替代了此前“结果队列 mutex + 第二次 future 扫描”的两条路径。

`process_completions()` 的无参数重载采用 `CompletionBudget` 默认值，也可显式指定：

```cpp
manager.process_completions({
    .max_results = 2,
    .max_time = std::chrono::milliseconds(2),
});
```

处理每个槽时：

1. 数量耗尽、时间非正或已处理至少一个结果且时间耗尽时停止。
2. 未就绪 future 跳过，不等待；允许后面的已就绪任务先完成。
3. 已就绪结果先消耗一个预算，再清理对应 pending revision，get future 并验证当前身份／版本。
4. 过期直接丢弃；有效结果交给 `publish_import_result()`，只有真实发布成功才返回 Handle。
5. 发布／丢弃后 erase 整个槽，释放候选数据，再尝试派发等待请求。

检查、失败和过期结果也计数，不能让一个失败批次绕开预算。零数量或非正时间暂停结果处理，
但仍可把等待请求派发到尚有容量的槽；已完成但预算外的结果继续占据在途额度。
正时间预算允许至少一个就绪结果前进，避免极小预算永久饥饿；单个原子文件替换或 GPU 创建不做中途抢占。

`publish_import_result()` 保留原发布语义：Mesh Artifact 发布成功即可报告 Handle，即便后续 GPU 创建失败仍保留旧 Runtime Mesh；
Texture 只有 Runtime 替换成功才报告。GPU 创建后继续二次检查 revision；材质依赖刷新路径不改变。
递归 process_completions 被显式拒绝，RAII 保证正常返回和异常后都清除处理标记；未知 GPU 异常也不会留下 invalid future 槽。

## 设计理由与架构价值

- 在途额度覆盖 `排队到调度器 → Worker 计算 → 就绪等待预算 → 发布／丢弃`，而不只覆盖 CPU 执行时间。
- 调度器不认识 Mesh／Texture；资产层的版本校验、Artifact 和 Runtime 发布仍由唯一 owner 执行。
- 删除完成 mutex、两个完成 deque 和 AsyncState 共享所有权，减少真正重复的状态及同步路径。
- 预算只控制 owner 的消费策略，不把同步显式导入、扫描或资源命令都改为事件。
- 没有把结果运输类放到 engine 公共接口，也没有预建通用 EventBus／CompletionBus／退休队列。

## 测试结果

- Debug／Release 全部构建成功，各 **420 tests** 通过；完整图形测试日志无 VUID／Validation Error。
- 新增 7 个测试：零／数量预算与槽保留、极小正时间预算仍有进展、过期／失败／检查计数、
  Mesh／Texture 共享预算、递归调用与发布中 revision 更新、未知 GPU 异常后的槽释放、析构丢弃未发布结果。
- TaskScheduler／背压／预算的 19 个测试重复 30 轮，共 **570 次测试执行** 全部通过；不依赖 sleep 控制 Worker 时序。
- 原有资产版本、失败保留、移动、场景恢复和编辑／渲染生命周期测试保持通过。
- 测试工厂的 Mesh／Texture 是身份占位符，不能解引用；旧 shared_ptr 相等断言改为布尔身份比较，
  避免失败时 GoogleTest 自动打印对象而读取占位内存。测试仍检查同样的对象身份，没有放宽成功条件。
- 首轮编译发现发布函数中局部 mesh 与候选指针同名，已修正。最终 clang-format／diff 检查通过。
- 014 Linux CI 34052025797 已确认成功。本项只涉及后台与 owner 处理，没有桌面人工交互验收；本项 CI 待推送后确认。

## 定期架构回顾（015）

| 维度 | 结论与本次处理 |
| --- | --- |
| 目录 | 导入在 asset/import、持久产物在 asset/artifact、调度器在 core；预算与发布属于 AssetManager，不额外拆出无独立职责的服务 |
| 职责 | TaskScheduler 负责并发 admission，AssetManager 负责版本／队列／发布；ImportService／Importer 只做 CPU 处理，Registry 管 Runtime 缓存 |
| 依赖 | Worker 的闭包仅持请求值与私有结果，不再持整个 AsyncState；GPU／数据库仍只由 owner 访问 |
| 冗余 | 移除两个类型完成队列、锁和独立 future 回收循环；通用预算只实现一次，类型差异留在发布策略 |
| 生命周期 | 就绪候选不提前归还额度；析构取消等待请求、等已派发任务、丢弃结果，不发布 GPU；future 与 result 的共享范围只到单任务 |
| 测试 | 公共 Worker gate 继续复用；预算夹具只配置不同容量，复用同一临时项目和 Factory；修复占位指针错误打印风险 |

011—013 的 Gizmo 仍在 editor，渲染器不认识编辑手势；014—015 的后台队列不接触 ImGui／Scene。
现有目录能够体现职责，本次不为减少成员数引入额外包装。资产测试仍集中在所属功能文件，未把少量新断言分散到大量小文件。

阶段 3 的主线阻塞项“任务背压／完成发布预算”已验收；更多格式、Texture Artifact、总字节预算等扩展继续保留。
阶段 4 核心已通过；接下来进入阶段 5 的多布局材质，依次推进 PipelineKey、Shader 安全更新与多 pass。
下一次定期回顾为 020 或阶段 5 边界（先到者）。

## 限制与后续方向

- 2 ms 是开启下一结果前检查的软预算，不是强制帧耗时上限。单个 GPU 创建、文件替换或材质依赖刷新可能超时。
- 无单模型大小限制／总解码字节预算；运行中的 importer 不做强行中断。同步扫描、显式加载和导入不受此预算约束。
- 预算可由调用方显式传入，尚未加入项目配置或动态调参 UI；Shipping／Editor 的调用策略以后可不同。
- 资产 API 仍是 owner-thread 协议，不支持从任意线程同时 scan／process／destroy；共享 TaskScheduler 的提交本身仍支持并发。
