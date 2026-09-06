# feat/auto2 连续迭代

## 目标与停止规则

从 main `6728ee7` 及当前未提交的 Gizmo 改动出发，逐项完成路线图阶段 4、5 的核心验收，推进到阶段 6 完成。
阶段 3 先补主线的阻塞依赖，长期扩展不捆绑进此目标；阶段 7 不在本轮范围。

以下任一条件成立即停止开发，不继续开启或补完新的步骤：

1. 阶段 6 的约定验收全部通过，且阶段 4、5 的核心验收已通过。
2. 账号主 `codex` 周额度剩余低于 10%。
3. 主额度发生重置，或到达保守截止时间 **2026-09-07 11:37:55 +08:00**。

初始额度快照：2026-09-07 01:07 +08:00，`limitId=codex`、`windowDurationMins=10080`、
`usedPercent=46`、`resetsAt=1788752275`。不使用其他模型的 100% 桶判断重置，不消耗重置券。
每步开始、验证后、提交前及长步骤中间读取额度；检测跨窗口变化或主额度恢复，不能只等待恰好显示 100%。
接口 reset 时间存在秒级取整抖动（本轮已观察到 +1 秒且已用额度上升），不能误报为重置；保守截止时间不向后延长。
额度查询持续失败则停止，避免盲跑。触发停止时只做最小状态记录，未验收内容保留工作区，不作为完成项提交。

## 实施与验收协议

- 一步对应一个可独立验收项，完成代码、测试、文档后提交并推送 feat/auto2，不使用 PR，不回写 main/feat/auto。
- 文档按 `NNN-topic.md` 编号，包含背景、前后对比、设计理由、架构价值、测试结果、限制和后续方向。
- 每步构建与相关测试；阶段收尾完整 Debug/Release 回归，图形变化检查 Vulkan validation。
  CI push 覆盖 feat/auto2，失败先修复，不把仅本地通过冒充跨平台通过。
- 本机图形测试进程串行执行；构建可并行。曾在两个图形测试进程同时运行时遇到 Cocoa 初始化超时，
  先确认原进程结束并保留采样/超时记录，再串行复核，不静默重跑掩盖失败。
- 每 5 个验收项及大阶段结束进行目录、职责、依赖、冗余与生命周期回顾，写入当次文档。
  不为减少字段或文件数量增加无职责的包装类；不为凑迭代数量单独制造空的文档步骤。
- 常规实现、选型和自查自主进行；真实权限/网络/依赖阻断有限重试，不无限卡住，不绕过权限。
- 不创建新的子代理；格式遵循 .clang-format，不批量格式化 Shader 或第三方代码，保留学习用 Shader。
- README 只同步实际架构/用法，不写成迁移日志。路线图勾选必须有代码与验收证据，不靠删除待办宣布完成。

## 阶段完成契约

- 阶段 4：命令历史覆盖实体名称、组件与实体结构、层级；必要编辑入口与保存闭环；Gizmo/高亮/拾取在 DPI、resize、裁切下正确。
  旋转/缩放、本地轴、吸附按独立验收项推进；按需事件只服务真实的一对多消费者，不建全局 EventBus。
- 阶段 5：多布局材质及参数、结构化 PipelineKey、Shader 编译/反射与安全热更新、缓存恢复、资源同步及 WSI 恢复、
  多 pass/资源状态编排、可运行的 forward 场景及必要诊断；明确 owner、在途资源寿命和队列边界。
  RenderThread/并行录制/Dynamic Rendering 等条件项需按真实需要和测量评估，不以新增类名作为完成证据。
- 阶段 6：键鼠/手柄输入、Fixed/普通 Update、Native Script 生命周期/字段、暂停/单步、dirty Transform、
  共享 descriptor 链路，角色移动/碰撞/声音 demo、Play/Edit 隔离、固定输入与时间步及单线程回退可重复。
  物理/音频库按 demo 选择；动画/AI/Runtime UI 按需求评估并记录理由，C++26 反射不是前置条件。

## 进度与下一项

| 编号 | 验收项 | 本地验证 |
| --- | --- | --- |
| 001 | 平移 Gizmo；输入互斥、事务与坐标边界 | Debug/Release 各 340 tests 通过 |
| 002 | 名称的描述符、编辑事务及字符串序列化 | Debug/Release 各 344 tests 通过 |
| 003 | CI 有界构建与测试超时（跨平台验证前置修复） | Linux CI run 34048158962：344 tests 通过 |
| 004 | 可选组件增删、完整值快照与共享撤销历史 | Debug/Release 各 353 tests 通过；Linux CI 34048392036 成功 |
| 005 | Hierarchy 结构命令与首次架构回顾 | Debug/Release 各 362 tests 通过；Linux CI 34048809364 成功 |
| 006 | 子树 duplicate、UUID 重映射与撤销 | Debug/Release 各 365 tests 通过；Linux CI 34048968491 成功 |
| 007 | Project Mesh 后台导入与 Artifact 状态 | Debug/Release 各 371 tests 通过；Linux CI 34049335440 成功 |
| 008 | Mesh 拖入 Viewport、关注平面放置与单条撤销 | Debug/Release 各 377 tests 通过；Linux CI 34049739269 成功 |
| 009 | 类型化资产引用选择／拖拽与材质纹理槽拖拽 | Debug/Release 各 382 tests 通过；Linux CI 34050111746 成功 |
| 010 | 场景资产加载／修复闭环与第二次架构回顾 | Debug/Release 各 386 tests 通过；Linux CI 34050484992 成功 |
| 011 | 平移 Gizmo 本地轴和相对步长吸附 | Debug/Release 各 391 tests 通过；Linux CI 34050761975 成功 |
| 012 | 旋转 Gizmo、本地／世界轴、角度吸附及共享变换事务 | Debug/Release 各 398 tests 通过；Linux CI 34051356460 成功 |
| 013 | 缩放 Gizmo、序列化联合验证与阶段 4 核心回顾 | Debug/Release 各 404 tests 通过；Linux CI 34051606376 成功 |
| 014 | 调度队列／资产在途背压与最新请求合并 | Debug/Release 各 413 tests 通过；Linux CI 34052025797 成功 |
| 015 | 完成发布预算、候选生命周期收敛与定期架构回顾 | Debug/Release 各 420 tests 通过；Linux CI 34052585677 成功 |
| 016 | 场景／材质解析边界、布局驱动绑定与 revision 缓存 | Debug/Release 各 427 tests 通过；Linux CI 34053125063 成功 |
| 017 | Frame/Material 分层、多布局 GPU 参数、排序及像素读回 | Debug/Release 各 432 tests 通过；Linux CI 34053891913 成功 |
| 018 | 布局驱动 Material Inspector、事件更新与草稿修复 | Debug/Release 各 439 tests 通过；Linux CI 34054327743 成功 |
| 019 | SPIR-V 接口反射、Pipeline 覆盖检查及材质 ABI 校验 | Debug/Release 各 446 tests 通过；Linux CI 34054932834 成功 |
| 020 | 当前 Pipeline API 结构化键、弱缓存、配置生效及架构回顾 | Debug/Release 各 452 tests 通过；Release 首轮 Cocoa 超时后串行复核；Linux CI 34055640202 成功 |
| 021 | 共用 CPU Shader 编译契约、构建 CLI、输入快照与 depfile | Debug/Release 各 460 tests + 1 构建契约测试通过；Linux CI 34056281827 成功 |
| 022 | 固定接口 specialization、类型/默认值反射、位模式键与真实 GPU 变体 | Debug/Release 各 464 tests + 1 构建契约测试通过；Linux CI 34056663399 成功 |
| 023 | 编辑器材质 Shader 有界热更新、整组发布与在途 GPU 版本保护 | Debug/Release 各 473 tests + 1 构建契约测试通过；Linux CI 34057650139 成功 |
| 024 | 材质反射布局、所有驻留 CPU/GPU 资源整组重建与 Inspector 发布快照 | Debug/Release 各 478 tests + 1 构建契约测试通过；Linux CI 34058335800 成功 |
| 025 | Debug Shader 整组热更新、轮询基线修复与阶段性架构回顾 | Debug/Release 各 484 tests + 1 构建契约测试通过；Linux CI 34058803915 成功 |
| 026 | 驱动 PipelineCache 校验恢复、原子保存及独立进程启动验证 | Debug/Release 各 490 tests + 2 独立契约测试通过；Linux CI 34059473777 成功 |
| 027 | WSI 失败暂停呈现、退休句柄隔离、间隔重试及 present Result 修复 | Debug/Release 各 490 单元 + 10 WSI + 2 契约测试通过；Linux CI 34059790706 成功 |
| 028 | 有序 RenderGraph、跨提交状态交接、离屏接入及 acquire 同步修复 | Debug/Release 5 个 CTest 项通过；505 单元含专门运行的同步对照、10 WSI、2 契约；Linux CI 34060940872 成功 |
| 029 | app/editor HDR 场景、fullscreen 色调映射／编码及成对 resize | Debug/Release 各 5 个 CTest；507 单元含专门同步对照、10 WSI、2 契约；8 GPU 项 ×20；Linux CI 34061527931 成功 |
| 030 | typed enum／LightComponent／三类 forward 灯光及架构回顾 | Debug/Release 各 5 个 CTest；516 单元含专门同步对照、10 WSI、2 契约；17 项 ×20；Linux CI 34062462366 成功 |
| 031 | 方向光深度 pass、PCF、帧绑定与上传等待合并 | Debug/Release 各 5 个 CTest；521 单元含专门同步对照、10 WSI、2 契约；21 项 ×20；Linux CI 34063046726 成功 |
| 032 | 金属粗糙度 PBR、相机帧 ABI、共享 Shader 消费者闭包与斜面阴影修正 | Debug/Release 各 5 个 CTest；526 单元、10 WSI、2 契约；16 GPU 项 ×20；两次 Cocoa 停滞另行记录；Linux CI 34063934553 成功 |
| 033 | 半分辨率 Bloom、后处理图组合、配置／帧边界参数及在途目标代 | Debug/Release 各 5 个 CTest；532 单元、10 WSI、2 契约；19 GPU 项 ×20；Linux CI 34064473387 成功 |
| 034 | CPU/GPU 帧诊断、完成后查询、低频预算与手动 VMA 报告 | Debug/Release 各 5 个 CTest；539 单元、25 同步 GPU、10 WSI、2 契约；重复验证在旧 GLFW 路径超时、035 修正；Linux CI 34065505083 成功 |
| 035 | GLFW 进程生命周期、面板可见性单一来源及定期架构审查 | Debug/Release 各 5 个 CTest；542 单元、10 WSI、2 契约；25 GPU 项 ×20；Linux CI 34065767335 成功 |
| 036 | 可复现 forward 场景测量与阶段 5 核心回顾 | Debug/Release 各 6 个 CTest；542 单元及原有 GPU/WSI/契约；新增 profile smoke；12 次 Release 测量各 240 个 CPU/GPU 样本；Linux CI 34066231351 成功 |
| 037 | 键鼠／手柄稳定输入帧、失焦／断连、ImGui 串接及 app 消费 | Debug/Release 各 6 个 CTest；551 单元及原有 GPU/WSI/契约/profile；12 输入／窗口项 ×20；Linux CI 34066986088 成功 |
| 038 | System 生命周期、有界 Fixed／普通 Update、输入消费与真实帧顺序 | Debug/Release 各 6 个 CTest；561 单元及原有 GPU/WSI/契约/profile；12 项 ×10；Linux CI 34067499081 成功 |
| 039 | Play Runtime 启停、暂停／单步与 UI／游戏状态隔离 | Debug/Release 各 6 个 CTest；570 单元及原有 GPU/WSI/契约/profile；21 项 ×10 |
| 040 | Viewport 游戏输入路由、采样中断边界与定期架构回顾 | Debug/Release 各 6 个 CTest；581 单元及原有 GPU/WSI/契约/profile；输入／Runtime／Viewport UI ×10 |

阶段 4、5 本轮核心验收通过，扩展项保留在路线图；阶段 6 尚未完成。
阶段 3 的主线负载控制已补齐。下一项：041 Native Script 生命周期与共享描述符字段。
已完成定期架构回顾：005、010、015、020、025、030、035、040；阶段边界回顾：013、036。下一次：045 或阶段 6 边界（取先到者）。

远端推送结果与 CI 以 git 远端 refs 和 CI 实际运行状态为准，不能将本表视为远端成功证明。
