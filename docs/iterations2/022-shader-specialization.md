# 022：类型化 specialization、PipelineKey 与真实 GPU 变体

## 背景与边界

021 已统一编译入口，020 的 PipelineKey 覆盖原有创建参数，但 Vk specialization 一直为 nullptr。
本项让固定接口的 Shader 数值/开关变体能从 PipelineConfig 真正传到驱动，并与反射、缓存和帧寿命对齐。
阶段 5 的热更新、动态布局重建和 variants 资产/UI 管理仍未完成，不能用本项代替它们。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| ShaderInterface 只列资源绑定与 push constant | 同时列出 specialization 的 ID、名字、类型与默认值 |
| PipelineConfig 无常量覆盖项 | 顶点/片元分别保存 ID → 类型化值 |
| VkPipelineShaderStageCreateInfo 的 pSpecializationInfo 固定为空 | 创建时生成连续数据和映射，真实传给驱动 |
| 缓存无法区分相同字节码的常量变体 | 完整比较 stage、ID、类型与原始位模式，哈希碰撞仍检查相等 |
| specialization 数组长度可能让默认反射与实际配置分离 | 明确保持接口形状不变；长度变体通过编译期 defines 生成并重新反射 |

## 代码级变化

### 1. 值与反射放回已有接口类型

`ShaderInterface` 内新增嵌套 `ConstantValue`、`SpecializationConstant` 和 `Specialization` map 别名，
不另建一组小文件/Manager。值只保存类型和 uint32 位模式，支持 bool、int32、uint32、float32。
构造时转换，之后没有可以随意写坏类型标签或 bool 编码的公开字段。

```cpp
PipelineConfig config;
config.vertex_specialization = {{0, false}};
config.fragment_specialization = {{1, 0.5f}, {2, int32_t(1)}, {3, uint32_t(1)}};
```

bool 对应 Vulkan 的四字节值，不是把 C++ 单字节 bool 地址直接交给驱动。
float 按 bit_cast 保存：+0/-0 不合并，NaN 位模式也不是用浮点 operator== 比较。
这与固定光栅状态的浮点相等策略不同，因为 Shader 可以观察符号位和位表示。

SPIRV-Reflect 枚举模块中的常量，Comet 拷贝 ID/名称/默认值，不保留库指针或临时 SPIR-V 地址。
`canonicalize_specialization()` 先完整校验所有覆盖项的 ID 和类型，随后移除与默认位模式完全相同的值。
因此省略覆盖和显式填写默认值共享 Pipeline；验证失败不会先删除调用方 map 的部分默认项。
若同一 ID 对应多个声明，逐个检查类型，只有所有默认值均相同时才能消除覆盖，不能只看第一个声明。

### 2. 保持“反射看到的布局”与实际 ABI 一致

不能只把数值塞进 VkSpecializationInfo：例如 `textures[COUNT]` 的 COUNT 若为 specialization，
默认反射可能是 2 个 descriptor，实际覆盖后却需要 4 个。
反射库也不提供完整的 specialization 表达式求值，不能手写一小部分运算并声称覆盖所有合法 SPIR-V。

本轮选择清晰的接口契约：specialization 用于固定形状的数值/逻辑；布局形状由编译期 defines/字节码决定。
`validate_fixed_array_lengths()` 在进入反射库前扫描有界指令，要求 OpTypeArray 的长度来自普通 OpConstant。
直接 specialization 长度与派生表达式均明确拒绝；当前所有这类数组（含局部数组）适用该限制。
这是保守、可诊断的限制，不是反射错误后默默使用默认长度。

对应替代路径已实际测试：同一 GLSL 中 `textures[COUNT + 1]` 用 defines 分别编译 COUNT=2/7，
得到不同字节码，反射分别要求 3/8 个 descriptor，后续 PipelineLayout 检查沿用现有链路。
本项没有把 GPU specialization 变成字符串宏，也没有增加运行时源编译器或通用 SPIR-V 优化器依赖。

### 3. 键和实际创建消费同一配置

`PipelineKey` 拷贝 PipelineConfig 后，用各 Shader 的接口校验/规范化对应 stage 的常量 map。
Hasher 按 map 大小、ID、类型、原始位模式组合，完整相等比较仍包含实际 map；不使用裸指针或数据地址作键。
同 ID 在 vertex/fragment 属于不同 stage，不会误合并。

`Pipeline` 创建时用 cpp 内部的 `SpecializationData` 组织 Vulkan 临时参数：

```text
规范化后的 map
  ├── SpecializationMapEntry：constantID / offset / 4-byte size
  └── uint32 words：真实原始位模式
          ↓
VkSpecializationInfo → 两个 ShaderStageCreateInfo → createGraphicsPipeline
```

临时数据一直存活到同步创建调用返回；Pipeline 创建后不借用配置 map 的内存。
没有常量覆盖时保持空指针。该 cpp 内部结构是 Vulkan 调用参数的临时 owner，不是公开资源类或缓存层。
直接调用 Pipeline 构造也会校验/规范化，不能绕过 PipelineManager 的类型检查。

## 架构价值

- 保持 Shader 代码、接口、Pipeline 变体三个概念分离；同一个 ShaderModule 可产生多个合法 Pipeline。
- 不为常量变化复制 GPU ShaderModule，也不把场景 Material 每帧值误用成需要重建 Pipeline 的 specialization。
- 声明、默认值、检查在 ShaderInterface，选择值在 PipelineConfig，临时 Vulkan 内存在创建作用域，职责明确。
- 原有弱缓存和 FrameSlot retention 继续保护实际 GPU Pipeline；不增加常驻配置指针或新的退休队列。
- 阶段 5 后续 Shader 更新可沿用同一配置/键，仍需独立完成 revision 和 PipelineState 发布协议。

## 验证

自动测试覆盖：

- 四种标量类型的反射值、未知 ID/错类型拒绝、显式默认值消除、+0/-0 位模式差异。
- 固定哈希碰撞下仍区分 stage 和数值，失败不替换现有 Pipeline 或增加缓存条目。
- 真实 GPU 像素读回：原左右 viewport/scissor 两种红色区域，加上半红青色变体、顶点关闭和片元关闭，共 5 种配置。
  使用 2 个 FrameSlot、5 个图像；创建后修改调用方配置，不影响已创建 Pipeline 的结果。
- 编译出来的 specialization 数组与派生表达式明确拒绝；编译期 defines 数组变体按真实数量反射。

- Debug/Ninja 与 Release/Make 完整构建均通过；各 464 个 GoogleTest 全通过，另各通过 shader_build_contract。
- 本项相关 5 个测试重复 20 轮，共 100 次通过，其中每轮包含真实 Vulkan 绘制/读回与错误路径。
  日志未发现 VUID/Validation Error；本机 GPU 测试进程串行运行。
- clang-format 检查与 git diff --check 通过；Shader 源码未交给格式化工具。
- 021 Linux CI `34056281827` 在本项收尾时仍运行；022 需推送后触发，远端结果后续独立确认。

## 限制与后续

- 目前 Graphics Pipeline 只有 vertex/fragment，其他 stage/Compute Pipeline 随真实渲染路径接入。
- 常量类型暂限 bool 和 32 位数值；64/16/8 位类型需要设备能力与明确 Shader 需求后再开放。
- specialization 改变数组长度不支持，使用已验证的 defines 编译变体。未来如确有必要，再引入完整专用化求值/反射方案。
- 常量不是每帧 uniform；当前没有在 Inspector 增加会频繁重建 Pipeline 的控件。
- 下一项进入 Shader 安全热更新：有界 Worker、debounce、输入/revision 验票、owner 发布与在途版本保护。
