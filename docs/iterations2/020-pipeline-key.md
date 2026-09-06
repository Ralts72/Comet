# 020：结构化 PipelineKey 与阶段性架构回顾

## 背景

019 能拒绝不匹配的 Shader 接口，但 PipelineManager 仍按字符串名字查找。
两个同名、接口都合法但 raster/depth/viewport 不同的请求，会拿到同一个旧 Pipeline。
反过来，同一内容仅换一个标签又会重复创建。ShaderManager 也会在同名时直接忽略新字节码。
本项完成当前创建 API 的结构化对象缓存，不把尚未开放的 specialization 或后台编译算作已完成。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Pipeline 缓存键是 name | 键包含实际 Shader 内容/入口、布局、配置与 RenderPass 上下文 |
| 同名合法新配置直接返回旧对象 | 配置不同创建新对象，名称不决定身份 |
| 同内容不同标签重复创建 | 在同一缓存域复用仍存活的对象，get_name 保留首次创建标签 |
| 缓存强持有全部 Pipeline | 弱缓存，使用者和 FrameSlot 决定 GPU 寿命 |
| Shader 同名就跳过加载 | 比较完整字节码和入口，候选成功后替换旧条目 |
| PipelineConfig 的静态 viewport/scissor 未被使用 | 实际提交配置，动态模式规范化无关值 |
| Pipeline 的 subpass 固定为 0 | 由 config.subpass 指定并校验范围，默认仍为 0 |

## 真实代码变化

### 1. 结构化值与完整相等比较

PipelineKey 与 PipelineConfig 留在 `graphics/pipeline/pipeline.h`，键的 CPU 实现在 `pipeline_key.cpp`。
没有另建 CacheManager、DescriptorKeyManager 或把每个 State 拆成单独文件。

```cpp
std::unordered_map<PipelineKey, std::weak_ptr<Pipeline>, PipelineKey::Hash> m_pipelines;
```

键保存：

- vertex/fragment 的自有 SPIR-V words 与 entry_point。
- 按 set 索引排列的 binding/type/count/stages，以及 push constant 范围。
- 当前 PipelineConfig 的全部字段：vertex input、topology、raster、multisample、depth/stencil、blend、
  viewport/scissor、dynamic states 和 subpass。
- 当前 RenderPass 句柄身份、附件 format/sample count。

Shader 的 GPU 版本由不可变内容表达：旧/新内容不同即不同键，同内容不因日志标签或对象地址不同而失效。
未来后台任务的 request revision 仍用于“旧结果不得覆盖新结果”，它与 GPU 内容相等不是同一件事。
本轮没有添加无人维护、永远为 1 的 Shader revision 字段。

Hasher 按字段组合；没有 hash 原始 C++ struct 内存，避免 padding/指针/浮点表示混入。
unordered_map 仍使用完整 operator==；哈希碰撞不会被当作相等。
Vulkan 值结构使用其字段 hash，与字段相等语义一致；+0/-0 测试同样相等且 hash 一致，非有限浮点在键构造时拒绝。

### 2. 规范化不影响语义的顺序

descriptor 按 binding，顶点 binding/attribute 按 binding/location，push ranges 按 offset/size/stage 排序。
动态状态排序去重；Viewport/Scissor 为动态时，静态值恢复统一默认值，避免窗口尺寸改变导致重复 Pipeline。
重复 vertex binding/location、重复 push stage、非法 push range 与越界 subpass 提前拒绝。
规范化后的 config 同时用于创建 Vulkan Pipeline，不让“用于比较的状态”和“实际创建的状态”分离。

static viewport/scissor 原本在 Pipeline 构造函数中被固定 100×100 替代；现在真正使用 config。
默认配置保留旧 100×100 行为，生产 MaterialRenderer/DebugRenderer 仍显式选择动态 viewport/scissor。
subpass 由 RenderPass 保存的实际数量检查，不仅把一个未使用数字加入键。

### 3. 名称和 Shader 候选

```cpp
if(old_entry == entry_point && old_code == spirv_words)
    return old_shader;
auto candidate = std::make_shared<Shader>(...);
m_shaders[name] = candidate;
```

代码中使用 ranges::equal 比较 bytecode；构造失败不替换原条目。
Shader 保存自有不可变 words，key 不指向临时数组，调用者也不能通过 get_code 修改内容。
ShaderManager 仍管理每个逻辑名字的当前设备 Shader；并非全局 Shader 内容去重库或完整热加载服务。
PipelineConfig/VertexInputDescription/ShaderManager 补齐动态库导出，与公开配置/工厂 API 对应。

### 4. 缓存不代替实际使用寿命

```text
MaterialRenderer / DebugRenderer ── shared Pipeline
当前 FrameSlot                 ── shared Pipeline 或持有它的 MaterialResources
PipelineManager[key]          ── weak Pipeline
```

create_pipeline 先校验接口并建立 key，清理已过期项，之后 lock 命中项或创建候选。
失败不会插入半成品；最后一个真实 owner 释放后 GPU Pipeline 立即销毁，不因缓存延长到 Renderer 关闭。
过期 CPU key 在下一次创建或显式 collect_unused 时清理；没有每帧扫描，也没有引入新的退休队列。
在途使用仍由 FrameScheduler 原有 retention/fence 保护，本项没有减少必要的 GPU 等待。
get_cached_pipeline_count 表示表中条目数量，调用 collect_unused 后才等于仍存活的缓存项数。

## 第 020 项架构回顾

范围以 016–020 新增材质/反射/缓存链为重点，同时复核与资产发布、帧调度和编辑器的接缝。

| 审查项 | 结论和处理 |
| --- | --- |
| 物理目录 | graphics/pipeline 仍从 pipeline.h/shader.h 进入；只新增 key 的 cpp，不拆一堆字段文件。Material CPU/GPU 分工继续留在 render |
| 职责 | SceneResolver 只解析 Handle/相机；MaterialRuntimeCache 准备 CPU 值；MaterialRenderer 排序并绘制；PipelineManager 只查找/创建设备对象 |
| 依赖 | render 依赖 graphics 的 CPU ShaderInterface；graphics 不依赖 Material/Scene/ImGui。Inspector 仅消费布局语义，不访问 Device |
| 三层缓存 | MaterialRenderer 的 layout→PipelineState 是程序选择/绑定协议，不是 PipelineManager 的对象去重；Device PipelineCache 是驱动编译缓存，不能互相替代 |
| 冗余与失效 | 修复名称缓存错误、缓存无期限强持有、静态 viewport 配置被忽略；同内容跨标签复用和无关顺序规范化已验证 |
| 生命周期 | MaterialResources 保留 PipelineState/PreparedMaterial/Texture；DebugRenderer 和 FrameSlot 保留实际 Pipeline。weak cache 不持有裸资源地址作为键 |
| 线程边界 | TaskScheduler 有界；AssetManager 在 future 完成并验 revision 后由 owner 发布。本轮没有让 Worker 创建 Pipeline 或并发访问 Vulkan cache |
| 文档一致性 | 所有权文档中的 TranslationGizmo 改为当前 TransformGizmo，补齐 ShaderInterface、Key、弱引用和真实入口 |
| 暂不改动 | WSI 重建失败回退、driver cache 持久化、Shader 后台发布已在路线图；不夹带一个未验收的恢复架构 |

材质 OOM 保留旧版本/延迟重试仍缺故障注入；Material statistics 目前表示最近一次 render 调用，无相机帧可能保留旧统计。
这些不因本轮结构化缓存自动解决，后续诊断/失败恢复验收时补齐。
MaterialRenderer 当前按 PreparedMaterial 身份复用 GPU 材质；接入 Shader 发布时，必须同时纳入 PipelineState 版本，
否则仅换 Shader、不改材质内容可能继续复用旧 Pipeline。当前没有这条发布入口，作为下一主线的明确约束保留。
不以减少成员数为目的再包装 SceneRenderer；也不把渲染生命周期改造成全局 EventBus 或 ECS System。

## 测试结果

- Debug/Release 全量各 452 tests 通过。
- 新增 6 个 GPU/缓存测试，合并覆盖同名差异、不同名相同内容、Shader 候选失败保持旧对象、
  等价布局/顶点输入/动态状态顺序、+0/-0、36 种结构字段差异及强制所有 key 哈希碰撞。
- FrameSlot 实际提交绑定 Pipeline 的 command buffer：外部引用清空后仍存活；等待并回收 slot 后释放，过期 key 可清空。
- 真实离屏 32×16 像素读回：两个同名静态配置分别只绘制 x<8 和 x≥24 的红色区域，其余保持黑色。
  不设置动态 viewport/scissor，直接证明配置已经被 GPU 消费，而非只比较返回指针。
- 两个 subpass 分别创建 Pipeline，索引参与实际创建和缓存；无 Vulkan validation 错误。
- 17 个 Shader/Material 相关测试重复 20 轮，340 次通过；019 Linux CI `34054932834` 成功。
- 本轮 CI 以 push 后实际运行结果为准；没有人工桌面验证。
- Release 首轮在另一图形压力测试进程同时运行时超时；系统 sample 显示主线程停在
  `Engine::Engine → glfwInit → _glfwInitCocoa → NSApplication::run`，尚未进入 key 比较主体。
  等待 CTest 的 120 秒超时确认进程结束，串行重跑 1.51 秒通过。并发初始化干扰是当前推测，不宣称已证明上游根因；
  后续本机图形进程串行，不修改第三方代码，也没有删除超时记录或放宽测试时限。
- 随后 Release 完整 CTest 串行连续 3 轮全部通过（每轮 452 tests，合计 1356 次）。

## 限制与后续方向

1. 当前 Pipeline API 的 specialization 始终为空，尚无值覆盖接口。路线图保留此验收项，
   需随 Shader 编译契约补齐 specialization 值/键和可能影响 descriptor array 长度的反射一致性。
2. key 在同一 Device/RenderPass 域内使用，包含原生 RenderPass 身份；不做跨 pass 兼容性归并，不可持久化到磁盘。
3. 为保证完整判等，key 复制当前 Shader 字节码；请求频率低，暂不增加共享字节码池。性能需要时再测量并优化，不能退化成 hash-only。
4. immutable samplers 未在当前布局构建 API 开放，key 明确拒绝此路径；多颜色附件/完整 stencil 状态等随相应渲染接口扩展。
5. 弱缓存不会保留完全无人使用的 Pipeline；需要跨场景预热保留时，应以明确的资源 owner/预算实现，而不是暗中永久强持有。
6. 下一项是 Shader 编译契约及 CPU 编译结果，再接有界 Worker、revision 验票、owner 帧边界切换和失败保旧；
   driver cache 恢复、WSI 恢复、多 pass 与阶段 6 仍未完成。
