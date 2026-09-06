# 036：可复现 forward 基线与阶段 5 核心验收

## 背景与验收项

034 提供了 CPU/GPU 诊断，035 修正平台与面板 owner 并完成定期架构审查。
现在可以测量真实多 pass 场景，而不是根据 FPS、空场景或 validation 小测试决定是否增加渲染线程。
本项提供可重复运行的独立测量入口，并完成本轮阶段 5 核心边界评估；不把线程类名当作完成指标。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| 主要靠手工开编辑器看 FPS／Render Stats | 一个独立进程生成带配置元数据的 CSV |
| 每次测试场景／窗口／预热条件不一致 | 固定 PBR 网格、三类灯光、方向光阴影、4×MSAA，可改变对象数／分辨率／Bloom |
| 不清楚是否真的绘制了目标场景 | 检查 draw、light、pass、材质版本与绑定数量 |
| 性能数据和 validation 正确性测试混在一起 | 测量程序请求关闭 validation；原有 GPU 同步测试继续独立保留 |
| RenderThread／并行录制只有方向性规划 | 记录本机实测范围、暂不采用理由和重新评估条件 |

## 代码级逻辑

`tests/render/render_profile.cpp` 构建独立 render_profile 可执行文件，不进入 unit_testing 的递归源列表。
它是工程验收／性能基线工具，不是新的 engine 子系统，也没有向 app/editor 的正常启动增加测试分支。
tests/CMakeLists 增加一个小场景 render_profile_smoke，和其他 GPU 进程使用同一个 CTest resource lock。

命令参数为 `OUTPUT.csv OBJECTS WIDTH HEIGHT FRAMES BLOOM(0/1)`。
from_chars 严格解析完整非负整数，检查对象 1～4096、逻辑窗口宽高 64～4096、采样帧 8～10000、Bloom 0/1。
错误在进入测量前拒绝。CSV 原子写入指定路径，重复使用同一路径会替换上一次生成报告。

临时项目只复制既有 cube.gltf／pbr.mat 及它们的 .meta，使用生产 AssetManager scan → import_mesh → load_mesh/load_material。
运行时仍消费 Mesh Artifact，没有重新引入程序化 cube 或绕过缓存直接解析模型的运行时捷径。
临时目录必须由本进程成功创建才取得清理所有权，退出后仅删除该目录，不改写源项目或它的缓存。

场景创建主相机、N 个共享 Mesh/Material 的实体、Ground、方向／点／聚光三灯；方向光开启阴影。
固定世界覆盖范围，增大对象数时缩小 cube／间距，避免简单把更多对象堆到同一像素；但小三角形效率也随规模改变，不能视作纯 CPU 控制实验。
静态场景故意不新增脚本或动画系统；它能包含当前真实的全量 transform／提取、材质缓存、排序、阴影和后处理开销。

预热 32 帧，在预热末尾预分配样本数组；记录其后指定数量的 CPU/GPU 图样本。
Engine update 回调读取上一循环快照，CPU wall 和图 serial 分开处理，GPU 按 serial 去重。
额外运行三个循环让在途查询自然可见，不为逐帧读时间戳执行 GPU wait。
GPU 不支持／诊断降级时明确写元数据，不输出假的 GPU 数值；GPU 样本数独立报告，不能默认与 CPU 相同。
测量中呈现跳过、CPU 样本不足或输出尺寸改变会拒绝报告，避免把被打断的运行当作有效基线。

最终还检查 `draw_calls == N+1`、三灯、Bloom 开／关对应六／三个 pass，以及共享 PBR 的一次 Pipeline 绑定、一次 Material 绑定和一个缓存版本。
这验证多实体走共享 descriptor／材质版本链路，没有为规模测试偷偷新建 N 份相同材质。
VMA allocation_bytes 在测量结束额外查询一次，不混入采样循环；它不是系统总显存，也不含所有外部分配。

输出记录构建类型、GPU／驱动、实际 framebuffer、实际 present mode、预热／样本数，以及逐项 P50／P95。
百分位采用 nearest-rank，不将各分段的 P50 相加当作整体 P50。
采样代码本身仍有少量 CPU 开销，且位于 update 回调中，不能把它当成完全零侵入 profiler。

## 本机测量

Release，Apple M4，Vulkan driverVersion 10401，实际 present mode 为 Immediate；未请求 validation。
所有测量串行，期间没有启动构建或其他本任务 GPU 测试。普通桌面后台负载、驱动调度和热状态仍可能影响数据。
四种配置各三次独立进程，每次 32 帧预热、240 帧采样；本机每条 CPU/GPU 指标实际均取得 240 个样本，共 2880 个测量帧。

以下是三次运行各自百分位的最小～最大值，不是置信区间，也不是把三次样本混在一起后计算的百分位。耗时单位 ms。

| 对象／Bloom／实际像素 | CPU 图 P50 | GPU 图 P50 | GPU 图 P95 | CPU 整帧 P50 | CPU 整帧 P95 |
| --- | --- | --- | --- | --- | --- |
| 64／开／1280×720 | 0.094～0.096 | 1.186～1.213 | 1.442～1.458 | 3.269～3.653 | 14.150～14.255 |
| 64／关／1280×720 | 0.081～0.085 | 0.662～0.676 | 0.912～0.915 | 2.496～2.540 | 14.768～15.059 |
| 1024／开／1280×720 | 0.512～0.657 | 1.623～1.810 | 3.277～3.368 | 5.060～6.693 | 13.048～13.570 |
| 64／开／2560×1440 | 0.096～0.109 | 4.215～4.221 | 4.770～5.708 | 6.990～8.260 | 10.889～13.275 |

每组的 shared Material 缓存版本、Material 绑定和 Pipeline 绑定均为 1；对象增到 1024 没有相同比例增加 VMA allocation。
1280×720 开／关 Bloom 分别为 125,965,872／118,036,016 bytes；2560×1440 开启为 460,658,224 bytes。
这说明当前附件、MSAA、分辨率成本值得持续观察；不把 VMA allocation 数值等同于进程 RSS 或全部 GPU 内存占用。

### 如何解读，而不是过度推断

- 增大像素数量时 GPU 图明显增加，CPU 图变化较小；关闭 Bloom 降低 GPU 图时间，说明这些开关实际影响 GPU 工作。
- 1024 对象的 CPU 图成本上升，但仍不能把整个 5～7 ms 墙钟都算作图录制：prepare 包含 frame-slot／acquire 等待，render-submit 还含提取和驱动调用。
- CPU 整帧 P95 在低负载配置下反而较高，说明桌面呈现／调度影响不能忽略。仅凭此数据不能确定是哪一个系统调用制造尖峰，更不能直接宣称 GPU-bound 或缺少 RenderThread。
- 静态低面数、单材质 PBR 网格不是复杂游戏，也未测 editor overlay；不能据此承诺某个对象数在所有平台的帧率。

## 阶段边界架构回顾与采用决策

目录、职责、依赖、冗余和寿命的详细审查见 035；本项新增入口只留在测试工具层，未向 engine 传播文件复制、基线输出或测试参数。

| 条件项 | 当前决策与原因 | 重新评估条件 |
| --- | --- | --- |
| 独立 RenderThread | 暂不引入。现有 CPU 图成本没有证明需要承担 Scene/UI packet、GPU cache owner 转移、额外队列／关机协议的复杂度 | 实际项目更新与渲染准备存在可并行重叠的显著成本；更细分 CPU/提交测量证明收益，并能维持有界 owned packet |
| 并行 culling／sorting／secondary recording | 暂不引入。本基线共享材质，一次绑定即可处理批次；先保留串行正确性基线 | 高对象／多材质项目中该阶段持续占据显著预算，先优化数据访问与 dirty transform，再评估每 worker 独立 CommandPool |
| Dynamic Rendering | 暂不迁移。现有固定 Shadow／Scene／Bloom／tone map 在传统 RenderPass 上表达清楚，重建、MSAA、ImGui 已有测试 | 动态 attachment/pass 插拔或多格式组合成为真实需求；完成 feature、ImGui、MSAA、resize 与目标设备验证 |

不使用这次决定删除上述规划，也不声称将来永远不需要；尤其不能把 `apiVersion=1.3` 当作 dynamicRendering feature 已开启。

本轮阶段 5 核心验收已具备对应实现和证据：

- 多布局材质、反射、结构化 Pipeline key／驱动 cache、安全 Shader 组发布：017～026 和 032 的回归。
- 有界任务与主线程 GPU owner、fence/retained owners、WSI 创建失败恢复：014～015、023～028，另有独立 WSI 故障测试。
- RenderGraph 资源依赖、HDR、forward 灯光／阴影、PBR、tone mapping／Bloom：028～033 及实际 GPU 像素／同步验证。
- CPU/GPU／内存诊断、生命周期审查和代表场景测量：034～036；035 的 25 GPU 项×20 已通过。

因此转入阶段 6。仍保留的扩展包括完整 surface/device loss 恢复、运行时不兼容格式重建、跨队列／动态资源图、透明、IBL 和更完整阴影等；
这些不是已实现能力，也没有通过删去路线图条目伪装完成。

## 测试结果与复现

Debug／Release 完整构建成功；最终完整 CTest 各 6/6 通过，耗时 14.03／10.56 秒。
既有 542 单元项（正常运行 541 通过、1 项专门同步对照跳过）、25 同步 GPU 项、10 WSI 项及两个契约入口保持通过；新增独立 profile smoke 通过。
12 次 Release 基线进程全部 exit 0，每条已报告 CPU/GPU 指标的样本数均为 240；没有用 GPU unsupported 跳过代替本机测量。
负对象数与整数溢出参数均在启动前 exit 1，未生成指定输出文件；格式检查和 git diff --check 通过。
本步没有修改生产 Renderer／Shader，未再次重复 035 已完成的 500 次 GPU 压力测试；本机日志为
`/tmp/comet-036-verified-debug.log`、`/tmp/comet-036-verified-release.log` 及下面的各次测量输出。
独立 smoke 执行真实生产资产导入与 forward 场景，不以绝对耗时设 CI 门槛，避免不同 GPU／虚拟显示的速度误报失败。

```bash
cmake --preset ci-release -B build-profile
cmake --build build-profile --target render_profile --parallel
./build-profile/tests/render_profile /tmp/comet-profile.csv 64 640 360 240 1
```

本机已有 Release 构建实际位于 `/tmp/comet-audit-release.lfw8ZZ`。原始报告为
`/tmp/comet-036-{64-bloom,64-no-bloom,1024-bloom,64-highres}-{1,2,3}.csv`，全部进程输出保存在 `/tmp/comet-036-measurements.log`。
花括号是文件名概括，不是单个文件。临时报告不进入版本控制，关键数据与复现命令在本文保留。

## 限制与后续方向

这是同机同版本基线，不作跨 GPU 排名，也不自动定位驱动／呈现尖峰；更细分性能调查须使用平台／驱动工具。
GPU pass 时间包含同步等待，不是独占 GPU busy time；CPU wall 包含系统等待，GPU 与 CPU 指标不能机械相减解释为开销。
工具只测固定静态场景，运行期间不要操作／最小化窗口；异常终止可能留下带随机后缀的临时项目目录。
validation 请求与外部强制 layer 环境设置不同，复现时需排除外部强制启用 layer 的影响。

下一项 037 是运行时键鼠／手柄输入的稳定帧快照和生命周期边界，然后接 Fixed Update、脚本、暂停／单步与其余阶段 6 验收。
现有 Material descriptor 跨 slot／实体共享已经有实际基线，后续优先复用，不再为阶段编号重复造一套缓存。
