# 007：Project 模型导入与 Artifact 状态

## 背景与验收

阶段 3 的导入管线可从代码调用，但 Project 无导入按钮，非 demo 模型不能通过编辑流程得到 Artifact。
这是阶段 4 资产拖拽的前置缺口。本项为 assets 中已扫描的 Mesh 提供后台检查和 Import/Reimport，
显示缺失／过期／就绪／失败及运行中状态，不把源文件解析塞进 UI 帧。

## 前后对比

| 边界 | 之前 | 现在 |
| --- | --- | --- |
| 显式导入 | 仅同步 import_mesh 调用 | 保留启动同步入口，增加 import_mesh_async 给交互操作 |
| 后台任务 | 只刷新已加载 Mesh | 未加载模型也可构建 Artifact，但不分配 GPU |
| 状态 | 调用 bool / Log | owner 按 Handle + revision 缓存 MeshImportState |
| Project | 文件列表与移动 | 选中 Mesh 显示 Artifact 状态，按钮提交 Handle 请求 |
| 文件检查 | ImportService 调用时检查 | inspect_mesh 用现有 Worker 队列，UI getter 不做 I/O |

## 代码级链路

Editor 在 update 中读取当前选中 Mesh 的状态。Unknown 才调用 inspect_mesh，提交后立即变成 Checking，
之后每帧只是内存查询，不重复调度。Worker 读取缓存、验证源指纹，产出 Missing / Stale / Ready 候选；
损坏或版本不匹配的已有产物视为 Stale。owner 在 process_completions 校验 revision 后更新状态及依赖。

Project 显示 `Artifact: ...`，Import/Reimport 只记录请求；Editor 消费后调用 import_mesh_async。
该入口和原有已加载模型刷新共用 schedule_mesh_task、TaskScheduler、完成队列、revision 验票和原子发布。
同 revision 的导入请求合并；检查期间不并行启动该 revision 的导入，按钮禁用，检查结束后再操作。
同步启动入口也拒绝与同 revision 后台任务重叠，避免旧检查结果覆盖显式导入状态。

候选成功后先发布 Artifact、更新依赖。Registry 没有该 Mesh 时到此结束，不为了生成缓存创建 GPU 对象。
已经加载的模型仍通过工厂创建候选并安全替换；失败不替换旧 Runtime。
因此 Ready 明确指 Artifact，不把它与 GPU residency 混为一谈，GPU 失败的详细诊断仍只进入 Log。

scan 使已完成状态缓存失效，正在运行的任务仍由 revision 管理。
手动删 .comet/cache 后点击 Refresh，随后选中模型触发后台复查；没有新增每帧 stat/hash。
导入失败保留旧产物和可用 Runtime，显示 Failed；源修复后可以再次提交。

## 设计理由与架构价值

AssetManager 是任务和状态的 owner，ImportService 仍管格式与产物准备；Project 不拥有 Importer 或 TaskScheduler。
没有 MeshImportController、第二个线程池或 EventBus；状态和请求沿既有 Editor 编排传递。
首次导入与重导入共用候选发布边界，后续拖拽只需要先确保产物就绪，再加载到 Registry。
新增状态嵌套在 AssetManager 内，没有为了一个 enum 再拆新文件。

## 测试

- 检查／导入异步状态、同 revision 合并与互斥；Worker 完成前后均不会自己发布磁盘或创建 GPU。
- 未加载模型导入后只有 Artifact，显式 load_mesh 才创建 Runtime。
- 缓存损坏和删除经 Refresh 检出；源改变后 Stale，重导入恢复 Ready。
- 失败保留旧 Artifact、修复可重试；文件移动后旧 revision 候选不能覆盖新路径或状态。
- 真实 ImGui 帧验证按钮提交稳定 Handle、只消费一次，Checking/Importing 禁用，界面自身不写缓存。
- Debug/Release 完整构建、各 371 项测试通过；本项新增 6 项测试，保留原有加载／热刷新失败矩阵。
  使用 `cmake --build --preset dev-debug --parallel 6`、`ctest --preset dev-debug --output-on-failure --timeout 120`，
  Release 使用独立构建目录；修改文件通过 clang-format dry-run，`git diff --check` 通过。

## 限制与后续

源文件仍需用户放入 assets 并保持 glTF 外部 URI/依赖布局；未新增 OS 文件选择器、源文件复制事务或 glTF 多 mesh 子资产。
目前只展示选中模型的状态，不对整个项目每帧扫描产物。
Artifact 写入和依赖发布仍在 owner；大量任务合并、完成队列预算和背压尚未完成，路线图继续保留，不因有后台线程就宣称负载受控。
下一项是 Project → Scene 的资产拖拽；之后继续完成 Gizmo 模式和阶段 4 剩余验收。
