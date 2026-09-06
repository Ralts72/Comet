# 023：编辑器材质 Shader 兼容热更新

## 背景与范围

021 的 CPU 编译器和 022 的 Pipeline 变体仍没有连接到编辑器。直接把编译结果写入 ShaderManager 也不够：
两种材质共用顶点 Shader，部分成功会形成混合版本；材质 GPU 缓存若只看 PreparedMaterial，会继续使用旧 Pipeline。
本项完成阶段 5 的独立验收：内置材质组三 Shader 的兼容热更新，从文件变化到真实新画面与旧帧保活。
不把 Debug Shader 或不兼容接口重建计为已完成；这两条路径仍在路线图。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Shader 只在构建时编译，编辑器不监控 | 编辑器检查三个材质生产源和实际 include，后台编译 |
| 无 Shader 请求合并/过期检查 | 一个在途组，一个最新待执行请求，防抖并校验 revision/输入 |
| ShaderManager 单条 load 可替换名字条目 | 先准备整组 Shader/Pipeline 候选，全部成功才一起切换 |
| GPU 材质缓存只检查 PreparedMaterial | 同时检查 PipelineState；换 Pipeline 不重复上传原材质参数 |
| 重建 MaterialRenderer 再 load 嵌入字节码 | 缺少 Shader 才加载默认值，保留已发布热更新版本 |
| 接口检查不足以比较两个版本 | 额外比较递归类型、矩阵存储方式和阶段输入输出 |

## 真实调用链

```text
Editor::on_init：创建 ShaderReload，登记 material_mesh/textured/solid 三个文件
Editor::on_update
  → ShaderReload::update：文件元数据轮询 → 防抖 → TaskScheduler::try_submit
      Worker：ShaderCompiler::compile → ShaderInterface 检查 → 自有 CPU 结果
  → future ready：revision 相等、输入内容仍相同才交付候选
  → SceneRenderer::reload_material_shaders：拒绝活动帧内调用
      MaterialRenderer::reload_shaders
        ShaderManager::prepare_update → 两个 PipelineState 全部准备
        ShaderManager 快照 swap + MaterialRenderer PipelineSet swap
  → 本帧 prepare/extract/render：按新 PipelineState 绘制
```

编辑器的 update 位于新帧 begin 之前。上一帧可以仍在 GPU 执行，但不能在当前录制中途切换。
没有新增发布回调/EventBus，ShaderReload 不认识 Renderer；Editor 作为组合根取 CPU 候选后直接调用渲染入口。

## 代码与设计理由

### 1. editor/src/shader_reload.h/.cpp

ShaderReload 是编辑器私有的一个编译组，不是 engine 的文件监听管理器。
默认每 200 ms 检查来源路径/实际解析路径的 mtime 和 size，变化后等 150 ms；首次启动会编译一次。
实际 include（含不存在的搜索候选）由编译快照补充，完成后重建 watch 集，不无限累积历史依赖。

每组至多一个 InFlight，修改只提高 revision 并合并 pending；队列满时 try_submit 立即返回，下次 update 再派发。
构造最多允许 16 个来源；当前生产组只有 3 个。没有阻塞 UI 等队列容量，也没有 caller-runs。
只有 future 完成后才读取 Job；Worker 捕获请求副本和 shared Job，不捕获 this、Scene、UI、Device 或 ShaderManager。
销毁 ShaderReload 后仍在执行的 CPU 任务可以安全完成，结果没有 owner 消费；Engine 原有 shutdown 等待任务结束。

交付前再次检查所有编译输入内容，不能只看 mtime；旧 ticket 即使恰好读到了新字节码，也不能冒充当前请求。
任一文件编译/反射失败都不交付部分组，错误在 owner 写入 Log，未变化时不反复编译/刷屏。
字节码从编译结果移动到候选，Job 的输入快照不再复制保存同一份 words。

### 2. ShaderManager：准备快照与发布分离

Bytecode 是嵌套 CPU 输入值（words/entry），Snapshot 是既有名字缓存的一份 owning map。
prepare_update 在局部快照内创建变化的 Shader，未知名字/不兼容布局明确报错，旧 map 不变。
内容相同复用现有 Shader，发布只执行不抛异常的 swap。没有把任务 revision 与 GPU 内容版本混为一谈。
GPU Shader 的构造仍会重做自身 CPU 校验：Worker 检查用于提前诊断，GPU API 对收到的字节码保持独立验证边界。
这存在少量重复反射成本，目前没有为省它增加可被错误配对的“字节码 + 外部反射指针”入口。

load_shader_if_missing 用于内置默认加载，原 load_shader 的显式替换语义保留。
因此 offscreen/Renderer 重建不会把已更新源程序又覆盖成最初的嵌入字节码。

### 3. ShaderInterface：兼容检查是值比较

新增 TypeShape 同时服务 block 成员和 stage 变量，保存标量/向量/矩阵、row-major、stride 和数组维度。
block 和 stage 的成员递归拥有自己的值；不用反射库指针或哈希单值判断兼容。
has_same_layout 比较 stage、descriptor/block/push 成员及输入输出；block 名称变化也会保守判为不兼容。
这不只是“都是 64 字节就兼容”：mat4 改 row-major、vec3 输入改 vec2 都会拒绝，纯计算表达式修改则可通过。
没有从反射猜测编辑器颜色/默认值，MaterialLayout 现有语义仍独立。

### 4. MaterialRenderer：整组发布与版本缓存

add_pipeline 收敛为返回候选的 create_pipeline；初始化和热更新共用配置构建，不复制一套 Pipeline 配置。
热更新要求三个名字齐全，先得到 Shader 快照，再准备两种材质 Pipeline；任意一步失败都不改当前程序组。
同内容命中相同 Pipeline 时保持原 PipelineState，避免触发无意义材质版本更新。

```cpp
if(cached.resources->prepared == prepared
    && cached.resources->pipeline == current_pipeline)
    return cached.resources;
```

兼容新 Pipeline 复用原 material descriptor layout。若 PreparedMaterial 未变，新 MaterialResources 只共享原
buffer/pool/descriptor/sampler 并指向新 PipelineState；绝不修改旧 MaterialResources 内的 Pipeline 指针。
新统计 material_bindings_created 区分“新 GPU 绑定”和“只换 Pipeline 的材质版本”，不是用版本计数假装上传量。
失败退避也同时记住 PreparedMaterial/PipelineState，新的 Shader 版本不会被上一轮失败的退避挡住。

旧 MaterialResources → 旧 PipelineState → 旧 Pipeline，继续由 FrameSlot retention 持有。
ShaderModule 不需要一直存活到 draw 完成，但 Pipeline 必须；没有添加新的 GPU 退休队列或 device wait idle。

## 架构价值

- 编译与设备发布边界真实落地；程序组事务覆盖共享顶点 Shader 的两个消费者。
- 保留原 Frame/Material/Object 分层和不可变版本，不以重建整个 MaterialRenderer 来实现每次热更新。
- Shader 更新不改变 Material revision，也不制造材质/纹理重新加载事件。
- 轮询/防抖留在 editor，CPU 编译在 tools，GPU 候选与提交在 render/graphics；Runtime app 不链接 glslang。
- 复用 FrameScheduler、TaskScheduler、PipelineKey 和现有 Log，避免第二套生命周期与消息系统。

## 测试结果

- Debug/Ninja、Release/Make 完整构建；各 473 个 GoogleTest 与 1 项 shader_build_contract 通过。
- 10 项相关测试连续重复 20 轮，共 200 次通过；格式检查与 git diff --check 通过。
- 6 个 ShaderReload 测试覆盖初始/空闲、连续修改合并、旧结果拒绝、队列满、缺 include 修复、整组失败、
  保持 mtime/size 的内容变化，以及 owner 销毁后 Worker 安全完成。
- CPU 反射兼容测试：计算代码修改兼容，矩阵 row-major 和顶点输入类型修改不兼容。
- 真实 GPU 测试通过同一个 ShaderReload 编译临时源：两个材质仅交换 Shader 输出颜色通道，原 Material/参数不变。
  两帧分别读回旧/新颜色；新帧 material_versions_created=2、material_bindings_created=0。
  旧/新 Pipeline 共 4 个，等待在途帧完成后缓存只剩新版本的 2 个。
- 组内第二个 Shader 失败时，第一个已创建候选也不发布；重新构造 MaterialRenderer 保留当前字节码。
- SceneRenderer 发布入口在活动 FrameScheduler 内明确拒绝，帧边界允许；Vulkan 日志未发现 VUID/Validation Error。
- 021 Linux CI `34056281827`、022 Linux CI `34056663399` 已成功；023 需推送后单独确认。

## 限制与后续方向

1. 本轮只监控材质三个生产文件及其 include；DebugRenderer 的两个 Shader 尚未接入。
2. 接口变化当前拒绝并保留旧画面，下一项处理可支持的 MaterialLayout/descriptor 重建及缓存失效；
   Frame/顶点输入/push 的 C++ 数据契约不能任意靠反射自动改写。
3. 元数据轮询不是原生文件事件。编译完成前会核对内容；但已发布后若外部刻意同时保留 mtime/size 改写文件，
   常规轮询可能不触发，需再次保存。发布期间的外部文件变化将在后续观测周期处理，不声称文件系统事务。
4. 一次交付有界为一个程序组；驱动 Pipeline 创建仍在 owner，可能超过帧时间软预算，后续以实测决定进一步拆分。
5. 尚未做取消第三方编译执行、进程沙箱/超时、编译产物缓存、任意 Shader 资产和 variants UI。
6. 未做人工桌面操作验收；自动测试执行真实 Worker、GPU 发布、像素读回及生命周期，而不是仅 mock 回调。
   阶段 5、6 均未完成，下一次定期架构回顾仍为 025 或阶段 5 边界。
