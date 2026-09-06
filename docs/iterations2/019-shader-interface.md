# 019：SPIR-V 接口反射与布局校验

## 背景

017–018 已让同一份 MaterialLayout 驱动 GPU 参数和 Inspector，但它仍是 C++ 手工声明。
Shader 修改 binding、参数偏移或类型后，C++ 不会自动知道；即使 Vulkan descriptor 类型正确，
错误的参数偏移也可能只表现为颜色错误。本项建立实际字节码与声明布局之间的检查，不扩展 UI 或热更新。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Shader 只创建 VkShaderModule | 先生成 CPU ShaderInterface，再创建模块 |
| 手工 ShaderLayout 直接提交 Vulkan | PipelineManager 在创建和缓存查询之前检查 Shader 所需接口 |
| MaterialLayout 只校验自身偏移/重叠/范围 | 再与 fragment Shader 的真实参数块和纹理 binding 对照 |
| Pipeline stage 入口写死 main | 使用已反射并确认存在的入口；ShaderManager 默认仍加载 main |
| 反射依赖未引入 | 固定提交的 SPIRV-Reflect submodule，静态私有链接，不编译工具及库自带测试 |

## 代码链路

```text
GLSL → 现有 CMake 编译规则 → SPIR-V
                              ↓
                       ShaderInterface（CPU 值）
                              ↓
Shader → VkShaderModule        ├→ ShaderLayout.validate → PipelineManager
                              └→ MaterialLayout.validate → MaterialRenderer
```

### 1. ShaderInterface 是可独立使用的 CPU 结果

新增 `graphics/pipeline/shader_interface.h/.cpp`，与 Vulkan Shader 的设备所有权分开。
构造函数接收 `span<const uint32_t>` 和入口名，先检查 header/指令长度，再调用 SPIRV-Reflect 的入口级枚举。
不是读取“整个文件的第一个入口”后假定它就是请求入口。

结果保存 set、binding、descriptor type/count/stages、参数块大小、顶层成员 offset/size/format，以及 push constant 范围。
32 位 float/int/uint 标量和向量有明确 format；数组、矩阵和结构体成员保留 Undefined，不能冒充可编辑 float4。
名称复制为 string，仅供诊断；所有列表都是自有 vector。临时输入和反射模块销毁后结果仍有效。
公开头文件不暴露 SPIRV-Reflect 类型或其指针，不持有 Device、ImGui 或 VkShaderModule。

反射库的 push constant 块 size 包含起始偏移和尾部 padding，不能直接作为 Vulkan range.size。
这里用成员 `max(offset + size)` 求末端，再减去 block.offset：测试中 offset=16、vec4 后接 float，
实际覆盖 `[16, 36)`，要求 20 字节，而不是从 offset 再加一个已经包含 offset 的块大小。

### 2. Shader 与通用布局的连接

```cpp
Shader::Shader(...)
    : m_device(device), m_interface(spirv_words, std::move(entry_point)) {
    // CPU 反射成功后才创建 VkShaderModule。
}

layout.validate(vert_shader->get_interface());
layout.validate(frag_shader->get_interface());
// 然后查 Pipeline 缓存或创建 Vulkan Pipeline。
```

DescriptorSetLayout 保存构造时的绑定描述，供 ShaderLayout 读取；不为了查询重新创建 descriptor。
ShaderLayout 检查所有所需 set 存在、set/range 指针非空，之后调用 ShaderInterface 的 CPU 校验：

- 所需 binding 必须存在、类型兼容、数量足够、stage 可见。
- 允许布局多出未使用 binding、额外 stage、较大数组容量。
- Uniform/Storage Buffer 的 dynamic layout 类型与对应 Shader 静态声明兼容。
- push constant 范围必须覆盖所需 stage 和字节范围；用 64 位末端计算避免加法溢出。

PipelineManager 还拒绝 null Shader 或 vertex/fragment stage 传反。
错误检查发生在缓存查询前，因此不能用已有同名 Pipeline 绕过错误布局检查。
这尚未解决“同名但不同合法配置”的缓存冲突；那是下一项 PipelineKey 的职责。

### 3. MaterialLayout 检查的是 CPU/GPU 数据约定

MaterialRenderer::add_pipeline 在创建材质 PipelineState 前调用：

```cpp
layout->validate(fragment->get_interface());
```

当前材质协议使用 set 1，binding 0 为单个 UBO，其余已声明纹理槽为单个 combined image sampler。
检查块大小、顶层成员数量、scalar offset/float32、vector offset/float32x4，以及纹理 binding 是否完整匹配。
例如把 `vec4 color` 改为 `ivec4 color`，即使块仍是 32 字节，也会在此处拒绝。

字段匹配按 ABI 的 offset/type，不按 Shader 的变量名；Shader 中 texture0 与资产槽 u_Texture0 可以继续不同名，
同偏移/同类型字段的业务语义仍由作者维护。反射不推断颜色、默认值、显示名或控件范围。
MaterialLayout 的既有 packing 和编辑语义没有迁出，也没有新建只服务 Inspector 的引擎类。

## 设计理由与架构价值

- 使用成熟的反射库解析 SPIR-V 类型和可达资源，不自行实现完整字节码解析器。
- 单独的 ShaderInterface 有明确 CPU/设备边界，后续 Worker 可生成它，owner 才创建 Vulkan 对象；本轮仍同步反射。
- 通用绑定覆盖检查属于 graphics；材质的固定 set/参数协议属于 render，不把 Material 或 ImGui 引入 graphics。
- 接口结构来自真实编译结果，编辑语义仍是人工 metadata，避免两种信息互相冒充。
- 检查在 GPU Pipeline 创建前发生；错误不会留下缓存中的半成品 Pipeline。
- 本项只增加一对有独立职责的源码文件，嵌套 Binding/Member 值类型不拆成额外文件或 manager。

## 依赖选择

按 agent-reach 的 GitHub/gh 路径核对官方源码及 CMake 集成。
引入 [KhronosGroup/SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect)
的固定提交 `3954c1e89a031cfb1724fa640be4e696558e6e8c`，不跟随浮动 main 自动升级。
只启用 `spirv-reflect-static`，私有链接 engine；工具、共享库、安装及上游测试关闭。
没有修改第三方源码。沿用项目的 `git submodule update --init --recursive` 流程。

## 测试结果

- Debug/Release 构建成功，完整 CTest 各 446 tests 通过；无 VUID / Validation Error。
- 11 个 Shader/Material GPU 相关测试重复 20 轮，220 次通过。
- 新增 7 个测试覆盖生产 Shader 反射、输入/解析器销毁后的值寿命、错误 header/截断/入口、runtime array 拒绝、
  descriptor 数量/类型/stage、动态 buffer 兼容、非零 push offset、材质偏移/块大小/类型/槽不匹配等路径。
- 真实 Vulkan 测试先创建有效 Pipeline，再以同名错误布局查询，确认引擎拒绝而不是返回旧缓存；stage 传反亦拒绝。
- 测试 GLSL 使用生产同一个 compile_shaders 函数，在测试构建目录生成 SPIR-V 和嵌入头文件。
  没有把测试 Shader 混入生产 Shader 列表，也未删除/格式化学习 Shader。
- 已有双布局像素读回、在途材质版本与 FrameSlot 寿命测试保持通过。
- 首轮截断输入曾触发上游 Debug 断言，现增加轻量 header/word-range 预检，并保留相应用例。
  测试中的 C++ 临时构造歧义及外部调用未导出辅助方法的链接错误均已修正。
- 018 的 Linux CI `34054327743` 已成功；本项跨平台结果以提交后的 CI 为准。

## 限制和后续方向

- 反射及预检不是完整 SPIR-V 语义验证，也不是不可信 Shader 沙箱；当前消费构建编译器生成的字节码。
  将来开放外部任意字节码输入时，需要完整 validator/隔离策略，不能把这里的长度检查当安全验证。
- 不支持 runtime descriptor array/bindless；未做顶点属性、stage 间 varying 或 specialization 后布局校验。
- 顶层成员不是完整类型树，当前材质仅接受已支持的标量和 float4；矩阵/数组/嵌套参数需按真实用例扩展。
- 不自动生成 MaterialLayout，不做 Shader 热更新、Worker 编译、持久化或反射驱动 Layout 重建。
- ShaderManager/PipelineManager 仍有按名称缓存的限制；下一项改为结构化 PipelineKey，并进行第 020 项定期架构回顾。
- 本轮没有桌面人工交互验收；使用自动化 CPU、真实 Vulkan 与已有像素测试验证。
