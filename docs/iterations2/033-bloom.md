# 033：可关闭的 HDR Bloom 后处理链

## 背景与验收项

032 的 PBR 可以产生高于 1 的高光，但最终 tone mapping 只压缩当前像素，亮区不会向邻域扩散。
本项完成阶段 5 的 Bloom 独立验收：在线性 HDR 中提取、模糊、合成高亮，配置与帧边界 API 可调整参数，
关闭时不录制额外 pass，尺寸变化及旧在途帧保持正确。不是完整后处理栈、自动曝光或阶段 5 完成。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Shadow → Scene → tone mapping | 可选插入 extract → horizontal → vertical 三个 pass |
| PostProcessRenderer 只有 HDR→SDR | 同一模块拥有 Bloom 图声明、ping-pong 目标、录制及合成 |
| SceneRenderer 自己声明 tone map 依赖 | 后处理模块追加自己的资源／pass，SceneRenderer 只组合阶段 |
| 曝光固定 1 | 配置／帧边界 API 提供 exposure、bloom_strength、bloom_threshold |
| HDR/SDR resize 成对发布 | 启用 Bloom 时，额外两张目标准备成功后一起切换 |
| GPU 读回夹具在 RenderGraph 大测试文件里 | 公共夹具放入 tests/render/gpu_test.h，后处理测试独立成文件 |

## 代码级变化

### 参数入口

`Config::Render` 增加三个数值；ConfigLoader 支持分层覆盖并验证有限范围：exposure [0,100]、strength [0,10]、threshold [0,65504]。
裸 Config 默认强度 0，保持调用者不显式开启时的旧输出；项目 common.yaml 设置曝光 1、强度 0.15、阈值 1。
这是启动配置，不是自动监控 YAML 热更新。默认低亮度背景不会出现光晕，提高 Key Light 强度可观察高亮扩散。

`PostProcessRenderer::Settings` 是后处理运行参数，不是资源或 Material。SceneRenderer 从配置建立该值，
`set_post_process_settings` 只接受有效值和帧边界调用；帧录制中拒绝修改，避免同一帧几个 pass 读到不同参数。
仅从关闭切到开启或反向切换时重新编译图；更改已开启的强度、阈值或曝光只改变 push constant。
独立 PostProcessRenderer 调用也验证参数，不能只依赖启动文件验证。没有加入新的 Inspector 回调或事件系统。

### 后处理模块内部的 pass

`append_passes(graph, hdr, bloom)` 声明局部资源和顺序；`append_bindings` 按相同导入顺序追加当前 slot 的实际 Image。
SceneRenderer 保留 Shadow、Scene 两个前置 pass，把后续相对索引交给 `render_pass`，不持有 Bloom target 或 descriptor。
启用时的图为：

```text
Shadow → HDR Scene
            ├→ extract(HDR → ping)
            │     → horizontal(ping → pong)
            │     → vertical(pong → ping)
            └─────────────────────→ tone map(HDR + ping → SDR)
```

关闭时只声明 tone map 对 HDR 的读取，不导入 ping/pong，也不录制提取和模糊命令。
图负责 ping 的写→读→再写→再读及 pong 的写→读；中间 RenderPass 保持 ColorAttachmentOptimal，不能再隐式转 layout。
最终 SDR 仍由已有 RenderPass 转为呈现或 Viewport 采样布局，没有重复追踪该最终输出。

`bloom.frag` 用 mode 区分三个 pass，复用一个 Pipeline；`draw` 共用 descriptor 更新、retain、viewport/scissor 和 fullscreen triangle。
post_process.glsl 统一 20-byte push ABI，CPU static_assert 与 SPIR-V 测试核对。未引入 BloomManager 或纯字段包装类。

### 滤波与颜色空间

提取目标为 ceil(width/2) × ceil(height/2)。每个目标像素读取最多四个有效源像素：先按 RGB 最大分量提取超阈值能量，
再平均；奇数边缘不把越界样本当黑色，也不会重复越界读取。提取保留原有颜色比例。
模糊为横／纵两遍九 tap 二项核 `[1,8,28,56,70,56,28,8,1]/256`，边缘 clamp。
两张纹理都是单采样 RGBA16F，每个 frame slot 独立，不需要计算队列或 storage image。

所有中间读取使用 texelFetch；最终上采样用四 texel 手动双线性插值，因此不增加 HDR 格式线性过滤的设备能力要求。
合成在线性空间进行 `HDR + strength * bloom`，然后曝光、指数 tone mapping 和既有 sRGB 编码；不在 SDR 值上叠光晕。
中间／合成 HDR 限制为 half-float 有限范围，强度与曝光极值不把中间目标写成 Inf。
Bloom 模拟屏幕空间高亮扩散，不把普通低亮度材质当光源，也不会为其他物体提供实际照明。

### 尺寸、缓存与在途资源

`try_resize_bloom(full_size)` 拒绝零尺寸，按上取整计算半分辨率；尺寸相同时复用两张目标。
否则先创建两个局部候选，全部成功才替换，任何已知 GPU 创建失败返回原始 Result，旧目标不变。
SceneRenderer resize 先准备 HDR/SDR 候选，再准备 Bloom，成功后只执行不抛异常的 owner 替换。
开关和普通 resize 不重建材质 Pipeline/MaterialSet；Bloom RenderPass/Pipeline 也不随尺寸变化重建。

每个 pass 的 Binding 保留所用两个 ImageView、descriptor pool/set；输入变化时创建新 Binding，不覆写旧在途 descriptor。
FrameSlot 保留真正使用的 Binding、Pipeline、layout、sampler、RenderPass 和输出 target；不要求整个 PostProcessRenderer 活到完成。
两个候选发布后清理旧的内部 Binding 缓存，已提交的帧仍独立持有旧版本。
开启过再关闭时保留最近的 Bloom target 供再次开启复用，但不录制 Bloom pass；从未开启则不分配 Bloom 图像。
Bloom pass/pipeline 在模块初始化时创建，关闭不等于其所有资源占用都归零。

新增图像的基本存储量为 `2 × ceil(W/2) × ceil(H/2) × 8 × frame_slots` bytes，不含驱动对齐和在途旧代。
例如 1920×1080、两 slot 约 15.8 MiB。不是瞬态资源池，也没有跨 slot 共用同一张模糊图。

## 设计理由与架构价值

- 后处理本来就有独立输出协议和 GPU owner，扩展现有模块比新增单用途管理器更直接。
- 图声明和录制在同一模块维护，SceneRenderer 只负责组合，不泄露 ping/pong 绑定或滤波参数。
- 开关改变执行图、参数改变帧值、尺寸改变目标代，三类变化不互相扩大重建范围。
- 延续候选发布与 FrameSlot owner 协议，复用真实生命周期机制，而非追加全局退休队列或 CPU wait_idle。
- 新测试按后处理职责落地，共享夹具只在 tests 内，不为了测试修改引擎资源所有权。

## 测试结果

- Debug/Release 最终构建与完整 CTest 各 5/5 通过；主集 532 tests（531 通过、同步故障对照按协议跳过），
  专门同步验证覆盖全部 19 个 GPU 项，另有 10 个 WSI 恢复及两个独立契约测试。
- 19 个 GPU 项开启同步验证重复 20 轮，共 380 次通过；日志 `/tmp/comet-033-repeat.log`。
  最终完整日志 `/tmp/comet-033-complete-debug.log`、`/tmp/comet-033-complete-release.log`。
- 本项未发生 Cocoa 超时；这不构成对 032 平台初始化问题的修复证明。
- 所有 C++ 改动及新增测试文件通过 clang-format dry-run，git diff --check 通过。
- 032 远端 CI `34063934553` 已成功；033 自身 CI 以 push 后实际结果为准。

- 配置测试覆盖实际解析、缺省值以及负数、越界、NaN、Inf 拒绝；Settings 与 20-byte Shader push ABI 有纯 CPU 验证。
- CPU 图测试检查关闭时只有最终合成、开启时的局部四 pass 和 ping-pong 重写 barrier。
- GPU 参考比较覆盖 32×24、33×25、1×1、17×9，sRGB/UNORM 两编码及关闭、正常阈值、零阈值、高于全部亮区的阈值。
  共 32 个组合，逐像素比较独立 CPU 提取／卷积／双线性／tone map 参考，含颜色、alpha 与上下方向。
- GPU 主链测试在 MSAA 4 下切换强度、曝光与尺寸，读回多个在途帧，验证旧像素不被新参数覆盖；录制中修改与无效参数被拒绝。
- GPU owner 测试检查同尺寸复用、零尺寸失败保旧，录制后替换目标并销毁模块；旧 Image 在 GPU 完成前保留、之后释放。
  尺寸不匹配在录制 draw 前拒绝。未注入第二张图创建时的真实 OOM，不能把零尺寸测试称为完整驱动故障覆盖。
- 最终补充尺寸检查曾暴露 Image 不完整类型编译错误，补充直接头文件后重新构建；没有运行失败构建留下的旧二进制冒充结果。
- Shader 和第三方代码未批量格式化，学习 Shader 未删除；未声称完成手工窗口视觉巡检。

## 限制与后续方向

固定半分辨率与九 tap 半径，不含多级金字塔、soft knee、镜头污渍、自动曝光或新的艺术 tone mapper。
后处理设置暂由 YAML／C++ API 提供，没有新增编辑器设置面板；源 Shader 走构建，不属于材质热更新组。
关闭 Bloom 保留最近目标缓存；低内存时的主动释放策略与瞬态图资源池不在本项。
032 记录的 Cocoa 重复初始化停滞仍是生命周期审查项，未因本项正常运行就宣称已修复。
下一项为必要 CPU/GPU frame-time 与内存诊断，再做阶段 5 边界回顾及线程演进评估；阶段 6 目标不变。
