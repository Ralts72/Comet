# 028：有序 RenderGraph、状态交接与离屏场景接入

## 背景与范围

现有 ResourceUsage／ImageState／Barrier2 可以描述一次明确的转换，但每个消费者仍需手写前后状态。
阶段 5 接下来需要 HDR、后处理、shadow 等多 pass，不能把所有 layout 放进 Image 的一个可变字段。
本项先完成“声明资源 → 编译同步计划 → 录制多个真实 GPU pass”的闭环，并接入现有离屏 Scene。
不宣称已完成 forward、HDR、自动资源分配或独立渲染线程。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| 调用方逐条指定 barrier 的 before/after | RenderGraph 根据 ordered pass 的资源使用推导 barrier |
| 离屏 RenderPass 自动导出 ShaderReadOnly | RenderPass 内保持 attachment layout，Graph 在 pass 外统一导出 SampledRead |
| MSAA resolve 的 initial layout 固定 Undefined | RenderSubPass 可显式给出 resolve_initial_layout，默认行为不变 |
| ImageInfo 的 mip/layer 信息不完整，创建固定各 1 | 显式 mip_levels/array_layers，实际传给 Vulkan，默认仍各 1 |
| 通用 resource usage 没有 CPU 边界语义 | HostRead/HostWrite 可表达导入／导出，不允许把 CPU 访问假装成 GPU pass |
| 普通 validation 未抓到 acquire→自动 transition 的执行缺口 | 专用同步校验及缺屏障对照，补齐 present RenderPass 的 external dependency |

## 类型与所有权

新增生产文件只有 `render/render_graph.h/.cpp`。ResourceId、Use、Pass、Barrier、BufferState、CompiledPass 和 Plan
都是这个概念的嵌套类型，不再拆 GraphData/Store/Manager，也不依赖 Scene、ImGui 或项目路径。

```text
RenderGraph（CPU 声明）
  import_image / import_buffer：名称、固定区域、已知初态
  add_pass：按顺序声明 ResourceId + usage + shader stages
  export_resource：结束后的外部使用状态
     ↓ compile（不访问 GPU）
RenderGraph::Plan（独立不可变快照）
  每 pass 的 Barrier2 输入 + export barriers + final states
     ↓ record(FrameScheduler, bindings, recorder)
  先检查全部绑定 → FrameSlot 保留 Image/Buffer → barrier → pass 命令 → exports
```

ResourceId 只在所属图内有效，不是 AssetHandle 或跨图身份。Bindings 按 index 对应 Image／Buffer owner；Plan 没有 GPU owner。
FrameSlot 保留实际绑定直到 completion。Pipeline、Framebuffer 等非声明对象仍由录制者按既有规则保留，
不是任意 callback 捕获的东西都会被图自动管理。BorrowedImage 的底层外部 owner 仍由调用者维护。
recorder 是同步录制函数，record 返回后不保存，不是事件系统或异步任务队列。

## 同步算法

一个资源声明对应固定的 mip/layer/aspect 区域或 buffer 字节区间。不同资源可绑定同一物理对象的互不重叠区域；
重叠区域必须使用同一个声明，否则在任何命令录制前拒绝，避免两个独立 tracker 同时描述一块内存。
本项不做动态区间分裂／合并，也不假装支持任意内存 alias。

编译保持 add_pass 顺序，不进行 DAG 重排／裁剪。每个声明保存累计访问 scope、是否有写入者、是否已有内容、
以及已建立可见性的 stage/access 对：

- Undefined 内容不能直接读取，也不能用于 StorageReadWrite；必须有初始化生产者。
- layout 改变会生成 barrier；RAW、WAR、WAW 建立依赖。
- 相同可见范围的 RAR 不重复加 barrier；新 shader stage 或新 access 类型不能借用另一个组合的可见性。
  例如 vertex UniformRead 和 fragment ShaderRead 都发生过，不代表 vertex ShaderRead 已经可见。
- 后续写入等待全部 reader，不能只保留“最近一次读取”。
- export 只改变边界契约，不生产内容；没有实际生产者的资源不能借 export 伪装成已初始化。
- final state 保守保留真实生产者和 reader 的 scope。后一个图显式 import 它，同 graphics queue 的 barrier 可以衔接不同 submission。

目前限制 256 个资源、512 个 pass、每 pass 256 个使用、256 个 export。重复名称、重复使用、非法索引、空或溢出区域、
缺少 shader stage、错误资源类型等明确报错。compile 使用局部状态，失败不修改旧 Plan。

record 要求正在录制且尚未提交的 FrameSlot；先检查完整 binding、资源类型、图像 usage/aspect/范围、buffer 范围、
queue family 和 alias，再准备全部原生 barrier，随后才保留资源和调用 recorder。
当前固定单 graphics queue：异 owner 拒绝，不生成不完整的 ownership transfer。
没有启用 separate depth/stencil layouts，因此组合格式必须一起声明；也不把深度和模板的独立声明当成安全的可分离布局。

## 生产 Scene 路径如何接入

`SceneRenderer::setup_offscreen_render_pass()` 根据最终 RenderPass 附件构建一次 Plan：
color/depth/resolve 导入 Undefined、Scene pass 声明 attachment write、带 Sampled usage 的最终输出导出 fragment SampledRead。
每个 slot 在 fence 完成后才复用，因此本帧 clear 可丢弃旧内容，不需要 Image 上保存全局 layout。

离屏 RenderPass 的 color/depth 初态与终态保持对应 attachment layout；MSAA resolve 也保持 ColorAttachmentOptimal。
Graph 负责 pass 外的 Undefined→attachment 和 attachment→ShaderReadOnly，避免隐式转换和显式 tracker 各自维护一套状态。
FrameBuffer 提供只读 attachments span，SceneRenderer 每帧从当前 slot 取得 Image owner 绑定计划。
resize 替换 target 后仍复用同一份逻辑计划，只换 bindings；整个 target 的原有 FrameSlot retention 保留。

`render_scene_pass()` 负责图编排，原有实际场景录制收口到 `record_scene_pass()`。材质上传 waits 仍沿原返回值进入 submit，
图不替代 UploadManager 的 timeline ready 协议。普通 runtime 直绘仍走原来的 RenderPass/WSI 路径，后续 HDR 再迁入多 pass。

## 同步校验暴露的呈现问题

专用测试开启 synchronization validation，发现旧的 present RenderPass 自动初始 layout transition
没有明确衔接 image-available semaphore 的 ColorAttachmentOutput wait。
错误是 `vkQueueSubmit2(): WRITE_AFTER_READ ... previously accessed by vkAcquireNextImageKHR`。

现在对实际呈现 color／resolve 输出的 subpass 增加 external→subpass 依赖：
source 和 destination stage 都为 ColorAttachmentOutput，destination access 为 ColorAttachmentRead/Write。
这让自动 transition 位于 acquire 的执行依赖之后，不扩大每帧 waitIdle，也没有简单把所有 semaphore 等待改成 AllCommands。
runtime 与 editor 最终呈现共用这条修复。

同步校验另有缺屏障对照：只录制 fill→copy 而不提交，要求校验器报告 RAW，确保测试没有在未启用校验时假通过。
不同 SDK 会输出 `READ_AFTER_WRITE` 或 `READ-AFTER-WRITE`，断言兼容两者；正向用例检查全部 error 级日志，
不能只搜 VUID，因为新版同步错误文本可能不带 VUID。故意的对照错误单独核对，不混入正向结果。

## 测试结果

- Debug / Release 构建 app、editor、tests 成功，5 个 CTest 项全部通过。
- 单元测试共 505 个：普通入口通过 504 个，缺屏障对照默认跳过；专用 `render_graph_sync_validation` 运行 6 个 GPU 用例，
  包括该对照，全部通过。另有 10 个 WSI 用例、PipelineCache 跨进程契约及 Shader 构建契约通过。
- 9 个 CPU + 6 个 GPU 图测试在同步校验开启时连续 20 轮（300 次）通过。
- 四 pass 读回：不同 mip/layer 的红／绿图像、不同 buffer 区间先写再读；随后覆盖为蓝色／新整数，再读回，192 bytes 全部按预期。
  清除外部 image/buffer owner 后，FrameSlot 保留到 completion，完成后 weak owner 过期。
- 两个 submission：后一个 Plan 直接 import 前一个导出状态并 copy，不为依赖插入 Device waitIdle；最终等待后读回整数正确。
- 生产离屏链路：MSAA 1×／4×、两个 frame slots、32×32→40×24 resize、线段绘制和最终呈现，外部消费者核对 ShaderReadOnly 状态。
  这项验证输出 layout 和真实绘制，不声称已经验证新的 fullscreen sampling shader；后者是下一项。
- 非法 binding／空绑定／类型不符／缺 usage／越界／重叠 alias／异 queue 均在调用 pass 前拒绝。
- 首轮对照的文本断言不兼容本机 SDK，随后通过；生产同步检查先暴露 acquire 依赖缺口，补依赖后通过。
  中途补齐了新文件的完整类型 include；负对照改用 native command-buffer 分配，没有仅为测试扩大 CommandPool 导出接口。
- clang-format、git diff --check 通过。026 CI `34059473777`、027 CI `34059790706` 均成功；028 CI 待本项 push 后运行。

## 限制与下一步

- 有序图不是完整调度器：没有自动 DAG 重排、pass culling、瞬态池、跨资源内存 alias、跨 queue family 或跨 Device 执行。
- Binding 必须来自当前 Device，buffer 创建 usage 和 recorder 的真实访问由调用方保证；图不反汇编 Vulkan 命令验证所有访问。
  未声明的可变资源、隐式改变图中状态或 recorder 内抛异常不享有自动 rollback；录制失败后的帧取消协议尚未扩展。
- 外部 upload/WSI wait、GPU completion 和 CPU host 访问时点仍须显式维护，HostRead export 不等于 GPU 已完成。
- Image 的 mip/layer 创建参数已生效，但 Texture 自动 mip 生成、压缩、数组采样 View 等仍在资源路线图中。
- 保留传统 RenderPass；本项不以换 Dynamic Rendering 为前置条件，也未进行人工桌面截图验收。
- 下一项接 HDR 场景与 fullscreen 后处理，把实际 shader producer/consumer 纳入多 pass 链路；forward lighting/shadow/PBR 与阶段 6 继续保留。
