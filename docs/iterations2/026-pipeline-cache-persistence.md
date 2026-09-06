# 026：驱动 PipelineCache 校验恢复与原子保存

## 背景与范围

现有 PipelineManager 的结构化弱缓存只在当前 Device/RenderPass 中复用 GPU 对象。Device 虽然创建了 Vulkan PipelineCache，
但启动始终为空，关闭直接销毁，不能跨进程利用驱动编译结果。本项完成阶段 5 的 driver cache 恢复验收，
不把它当成 PipelineKey、Shader Artifact 或资产数据库的替代品。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Device 直接保存原生 VkPipelineCache | Device 独占 PipelineCache，内部 Vulkan-Hpp unique handle 管寿命 |
| 每次创建空缓存 | 从设备对应文件读取、校验后作为 Vulkan initialData；无效则回退为空 |
| 关闭丢失所有驱动缓存 | 关闭前提取数据并原子保存，也可在编译批次后显式 save |
| 无磁盘格式和大小边界 | Comet 32 字节封装 + Vulkan 32 字节 v1 头；数据上限 64 MiB |
| app/editor 不提供缓存位置 | RUN_APP → LaunchOptions → Config → RenderContext → Device 传入项目缓存目录 |
| ImGui 创建 Pipeline 时没有提供 cache | 借用同一 Device 的原生缓存句柄，不取得所有权 |

## 文件与调用链

生产文件只新增 `graphics/pipeline/pipeline_cache.h/.cpp`：GPU owner、格式校验、恢复和保存同属这个类，
不再拆成 CacheData/CacheStore/CacheManager。类不认识 ProjectPaths、AssetHandle、Material 或 ImGui。

```text
RUN_APP
  ProjectPaths(PROJECT_ROOT_DIR).cache()
    → LaunchOptions.cache_directory
    → Config::Vulkan.pipeline_cache_directory = cache_directory / "vulkan"
    → RenderContext → Device::CreateInfo
    → PipelineCache(device, physical_device_properties, directory)
        读取、解码、匹配设备 → createPipelineCacheUnique(initialData)

Pipeline / ImGui backend → 借用 PipelineCache::get()

正常关闭：Engine 等待后台工作和 GPU
  → Renderer / Pipeline 等实际对象销毁
  → Device 释放 PipelineCache
      getPipelineCacheData → 校验 → encode → write_binary_file_atomic
      unique handle 销毁
  → Device 原生设备销毁
```

路径为 `.comet/cache/vulkan/<vendor>-<device>-<pipelineCacheUUID>.bin`。不同 GPU／驱动缓存 UUID 不互相覆盖。
路径来自本机启动上下文，不写入共享 YAML；直接构造 Config 或 LaunchOptions 时留空即可只用内存缓存、不读写磁盘。
`.comet/` 原本已在 gitignore，无需新增忽略规则。

## 磁盘格式与安全边界

Comet 文件封装采用固定小端编码，不把 C++ struct 原始内存直接写盘：

| 偏移 | 字段 |
| --- | --- |
| 0–7 | magic `CMVKPC01` |
| 8–11 | 封装版本 1 |
| 12–15 | 封装头大小 32 |
| 16–23 | 驱动数据字节数 |
| 24–31 | 驱动数据 FNV-1a 64 位校验和 |
| 32 起 | Vulkan 原始 PipelineCache 数据 |

先限制文件长度，再分配读取内存；文件须完整读取且不能带未计入的尾部内容。decode 检查 magic、版本、头大小、
精确 payload 长度、校验和，之后检查 Vulkan 头的 headerSize/headerVersion/vendorID/deviceID/UUID。
decode 返回文件 span 内的借用片段；构造函数保持文件内存存活直到 Vulkan 消费结束，不额外长期缓存这份字节。

Vulkan v1 头固定 32 字节、所有字段按小端排列，无论本机字节序如何；实现按明确偏移读写，
不依赖编译器 padding。[Vulkan 官方格式说明](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheHeaderVersionOne.html)

缓存缺失安静创建空缓存；校验失败记录 warning，原文件在运行中不被立即删除或改写。关闭成功保存后替换为新的有效缓存。
若驱动仍拒绝通过本地校验的数据，捕获 Vulkan 错误后再尝试创建一次空缓存；连空缓存都不能创建属于设备／内存故障，
不能伪装成恢复成功。当前没有驱动拒绝／OOM 的故障注入测试。

## 保存与所有权

PipelineCache 的 `save()` 通过 Vulkan-Hpp 的显式大小接口读取缓存，先检查大小再分配，最多处理三轮 `INCOMPLETE`，
不使用可能无界增长／重试的便捷循环。结果再次匹配设备和格式，随后复用已有同目录临时文件＋rename 原子替换。
读取或写入失败只跳过保存；析构额外保护日志后端异常，不能因为可丢弃的缓存中断 Device 关闭。

PipelineCache 始终在 owner 线程使用；当前自动保存只发生在关闭，没有每帧写盘或新后台线程。
PipelineManager 仍弱引用 Pipeline，FrameSlot 仍保留真正使用的对象；缓存不参与 GPU 资源退休，也不延长 Pipeline 的寿命。
ImGui backend 只借用句柄；编辑器先销毁 backend，Device 后销毁缓存。该接线完成编译验证，未新增人工桌面视觉验收。

## 架构价值

- PipelineManager 回答“当前是否已有等价 GPU 对象”，PipelineCache 回答“驱动能否复用历史编译数据”；没有合并两种不同缓存。
- Device 不再承担二进制解析、路径命名和文件恢复逻辑，GPU cache 生命周期仍清晰地归属 Device。
- 启动层决定项目缓存位置，graphics 不引入项目／资产依赖；测试及嵌入使用者可以完全禁用磁盘。
- CPU 编解码与真实 Vulkan 使用都在同一概念内接受验证，不额外增加只服务测试的生产接口回调或 Store 类。

## 测试结果

- Debug / Release 全量各 490 个 GoogleTest，另有 `pipeline_cache_process` 和 `shader_build_contract`，三个 CTest 项全部通过。
- 6 个缓存测试连续运行 10 轮（60 次）通过；独立进程恢复契约连续运行 5 轮通过。
- CPU 格式：小端往返、所有截断长度、尾随字节、magic/version/长度/校验和破坏、原生头大小/版本、vendor/device/UUID 不匹配、超限拒绝。
- Vulkan：正常关闭生成文件，新设备恢复后创建真实 Pipeline 并绘制；截断／损坏／异设备缓存回退，运行期间不提前覆盖原输入，关闭后修复。
- 保存失败：用占据目标路径的目录和 sentinel 验证原目标保留、临时文件清理、关闭继续；空目录配置不产生磁盘缓存。
- 独立进程：`pipeline_cache_probe` 通过真实 `run/Application/Config/Engine` 链路运行两次，各绘制一帧，分别要求 Missing/Restored，
  验证默认拼接到 `cache/vulkan` 并在 end 后保存。临时路径包含空格，使用独立 argv，不拼 shell 命令。
- CTest 为单元测试和图形 probe 设置同一个 resource lock，避免 `ctest -j` 在同一运行中并发初始化两个图形进程。
  probe 的单次进程有 30 秒超时，失败保留独立临时目录。
- 本机首轮样本：同进程重建设备的 Engine 创建约 371 ms／16 ms，文件 16772 bytes；独立进程总运行约 377 ms／411 ms。
  后者没有更快，说明启动噪声与其他开销不能当成缓存收益；没有要求或声称固定提速比例。
- Vulkan 测试无 VUID/Validation Error。上一项 025 的 Linux CI `34058803915` 成功，本项 CI 在 push 后触发。

## 限制与下一步

- FNV 校验和防意外损坏，不是认证机制；本机缓存不是不可信远端二进制的安全沙箱。
- 原子 rename 保证可见完整文件，不承诺断电后 fsync 持久性。多进程同时关闭采用最后一次完整写入，不做合并或跨进程锁。
- 按 UUID 分文件尚无目录总预算／历史版本淘汰；单文件有 64 MiB 上限。用户可以删除整个缓存目录，首次运行再生成。
- 驱动可能忽略缓存，Restored 表示数据通过校验并被提交创建，不等于证明驱动命中内部编译条目。
- 只在正常关闭自动保存；崩溃可能丢失本次新增缓存，但不影响资源正确性。频繁批次落盘策略按实际数据量再扩展。
- 下一项处理 WSI 创建失败后的无呈现恢复：旧 swapchain 已退休时不错误恢复旧目标，后续可重试。
  多 pass、forward 场景与阶段 6 仍未完成。
