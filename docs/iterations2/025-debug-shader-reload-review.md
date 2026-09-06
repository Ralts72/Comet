# 025：Debug Shader 热更新与阶段性架构回顾

## 背景与范围

024 已接通材质接口重建，但 `DebugRenderer` 仍只在构造时加载 Shader，且每次重建会覆盖 ShaderManager 中的同名条目。
选中框等线段绘制因此无法像材质一样安全热改。本项完成 Debug 两 Shader 的独立热更新验收，
并按每五项的约定审查 021–025 的编译、反射、发布和生命周期接缝。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Editor 只有材质三 Shader 更新组 | 新增 Debug 两 Shader 组，复用同一个 ShaderReload 类型和 TaskScheduler |
| Debug 构造时直接 load_shader | 只补缺失的内置版本，重建保留已发布 Shader |
| Debug Pipeline 创建只写在构造函数中 | create_pipeline 被构造与候选重建共用 |
| Debug 无帧边界发布入口 | SceneRenderer 拒绝活动帧内发布；DebugRenderer 校验固定接口并事务切换 |
| 已读到新源的 Worker 完成后仍沿用旧轮询时间戳 | 内容验票前建立新基线，避免下一 poll 再编译同一版本 |
| CPU 编译器日志声称保留了 GPU 版本 | CPU 只报告候选拒绝，GPU 发布错误由真正的 owner 报告 |

## 真实代码路径

Editor 初始化 `m_material_shader_reload` 和 `m_debug_shader_reload`，分别登记生产材质三文件与 `debug_line.vert/frag`。
两组各自最多一个在途任务和一个合并的最新请求，编译失败／队列背压／revision 和输入内容验票全部复用原实现。
没有新增调度器、线程、事件总线或带任意回调的 Shader 服务。

```text
Editor::on_update
  → Debug ShaderReload::update：变化、防抖、Worker 编译、内容验票
  → SceneRenderer::reload_debug_shaders：要求非活动帧
      → ShaderManager::prepare_update：两份 Shader 候选
      → 对顶点／片元 Shader 完整 has_same_layout
      → DebugRenderer::create_pipeline：候选 Pipeline
      → Shader snapshot swap + Pipeline swap
  → 后续 render 使用新 Pipeline
```

Debug 使用固定 `LineDrawList::Vertex` 与 view-projection mat4 push constant，因此接口整体必须兼容。
改变片元颜色公式可以发布，改变矩阵 row-major、顶点布局或 descriptor 要求则保留旧版，不能靠 Vulkan descriptor 类型相同蒙混过去。
同一份字节码重复发布命中既有 Pipeline，`reload_shaders()` 返回 false，不反复记录“发生更新”。
DebugRenderer 的动态库导出与公开 API 同步补齐。

### 为什么是两组，不把五个 Shader 强行绑在一起

两种材质共享一个顶点程序，必须一起校验／重建；Debug 不共享它们的 GPU 程序和材质语义。
因此材质组和 Debug 组独立，Debug 的源错误不应阻止用户验证材质修改。相同 include 被两组使用时，
也分别编译和发布，不承诺跨组原子一致性；未来若出现真正共享的运行时 ABI，再按依赖调整发布域。

两个明确命名的 owner 字段表达这两个真实生命周期，不把它们包进一个只有成员集合的新类。
SceneRenderer 只保留两条具体发布入口及帧边界检查，不塞入一个字符串命令分发器。

### 在途资源如何保活

`DebugRenderer::render()` 原本已把实际使用的 vertex buffer 和 Pipeline 交给 FrameSlot。
新实现仅替换 DebugRenderer 当前的 shared_ptr，不改旧 Pipeline，不重建各 slot 的线段 buffer，也不调用 device-wide wait。
PipelineManager 仍是 weak cache；旧 slot 完成后旧 Pipeline 才能销毁。
Editor 关闭时先销毁两个 ShaderReload；任务只捕获自有请求／结果，Engine 随后排空 TaskScheduler 并等待 GPU，才释放设备资源。

### 轮询基线修复的具体原因

任务排队期间源文件可能已经更新，Worker 实际读到的新字节正好通过最终内容验票；此时请求 revision 仍可有效。
旧 `watch_inputs()` 却对已有路径保留上次 poll 的 Stamp，下一次 poll 把已消费的修改再视为新请求。
新实现先读取新 Stamp，再执行既有内容验票：若输入在中间改变仍会丢弃候选；真正接受的输入不再重复编译。
这是消除冗余任务，不是弱化 revision 或内容检查。相同路径在单次基线收集中只读取一次。

## 第 025 项架构回顾

| 审查项 | 当前结论与处理 |
| --- | --- |
| 目录入口 | tools/shader/compiler 是 CPU 编译入口；graphics/pipeline 管接口／设备对象；render 的 MaterialRenderer、DebugRenderer 是实际消费者；editor 的 ShaderReload 管一次编译组。无新增生产文件或目录 |
| 编译依赖 | editor 链接 comet_shader_tools，engine/app 不依赖源编译库。ShaderReload 借用 ShaderManager 的 Bytecodes 值类型，未创建 Device；暂不只为这份 DTO 新增文件 |
| 职责 | ShaderManager 准备对象；消费端判断固定 ABI／可重建材质布局；SceneRenderer 保证帧边界。不能让通用 Shader 缓存替业务猜测 ABI |
| CPU/GPU 边界 | Worker 自有输入／结果，只编译和反射；Shader、Pipeline、descriptor 与 driver cache 仍由 owner 串行访问 |
| 发布域 | Material 三程序需要整组资源重建；Debug 两程序只需固定接口 Pipeline 更新。两组明确独立，既不强行全局原子化，也不每 Shader 单独发布 |
| 冗余 | 复用编译队列而非复制监控类；抽出 Debug Pipeline 构建函数；通过先失败的测试确认并修复重复编译；CPU 错误日志不冒充 GPU 状态 |
| 三种缓存 | ShaderManager 名称查找、MaterialRuntimeCache 布局／材质快照、PipelineManager 结构化弱缓存解决不同问题；尚未实现的 driver blob 不能代替它们 |
| 生命周期 | FrameSlot 持有实际 Pipeline/MaterialResources；Shader 候选发布不等待 GPU、不改在途对象。测试增加 scope cleanup，提前断言退出也先回收已提交 slot 再销毁读回 buffer |
| 重建 | Material 和 Debug 都只在 Shader 缺失时加载内置数据；修复 Debug 重建覆盖已发布版本的问题 |
| 文档 | rendering-ownership 中旧的“布局全手写／只有兼容材质热更／Debug 待办”已改成真实链路，README 保留用法而非迭代日志 |
| 明确暂缓 | driver cache 恢复、WSI 失败后无呈现恢复、多 pass 状态编排、大量材质发布峰值、无相机帧统计等继续按路线图验收，不以此次审查宣布解决 |

没有把 Render 生命周期改成 ECS System，没有增加全局 EventBus，也没有为减少字段数量再包装 SceneRenderer。
重复的测试读回步骤目前保持在现有相关测试文件中；后续多 pass 测试形成更广共用需求时再提取 fixture，不扩大生产资源 API。

## 测试结果

- Debug / Release 全量各 484 个 GoogleTest，加独立 `shader_build_contract` 通过。
- 20 个相关用例重复 20 轮，共 400 次通过；本机图形测试进程串行。
- 新增 4 个参数化 Vulkan 用例：在 Swapchain／Offscreen 场景配置与 1×／4× MSAA 下，
  通过同一 Worker 编译路径把实际线段从红色变蓝色；比较两个在途 slot 的像素通道和。
- 两帧提交后保留两份 Pipeline，等待全部 slot 后只保留当前一份；无 Validation Error。
- 缺失 Shader、空字节码、固定矩阵存储变化拒绝发布并保留 Shader；先成功创建 fragment 候选再在 vertex 失败也不会部分发布。
  重复发布不创建 Pipeline；重建继续使用新版本；活动帧入口拒绝更新。
- 两组 ShaderReload 的一组失败不影响另一组完成，修复失败源后独立恢复。
- 轮询基线回归先验证旧实现失败（submitted=2 而应为 1），修复后通过；日志保留在 `/tmp/comet-025-baseline-before.log`。
- 上一项 024 的 Linux CI `34058335800` 已成功；本项 Linux CI 以 push 后实际结果为准。
- 没有人工桌面验证，不把自动像素读回宣称为交互体验验收。

## 限制与下一步

- 文件监听仍采用元数据轮询；主动保留 mtime/size 的后续写入可能漏检。内容验票保护编译中输入，不是跨文件系统事务。
- 两组最多两个在途 CPU 编译；没有编译进程隔离或执行超时。GPU Pipeline 创建仍可能短暂占用主线程。
- Debug 只热改固定接口的实现，新增 descriptor、改 push/vertex ABI 必须配套改 C++ 消费路径。
- 先继续 driver PipelineCache 的版本／设备／长度／校验和恢复和原子保存，再推进 WSI、多 pass、forward 场景与阶段 6。
  阶段 5、6 均未完成。下一次定期架构回顾：030 或阶段 5 边界，取先到者。
