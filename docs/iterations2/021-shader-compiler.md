# 021：共用 Shader 编译契约与构建接入

## 背景与验收边界

019 已能从 SPIR-V 反射接口，020 已能按真实配置缓存 Pipeline，但构建仍直接调用机器上的 glslangValidator。
编辑器若再独立拼命令行，stage、entry、宏、目标版本、include 搜索与诊断容易出现两套行为。
本项完成路线图阶段 5 的独立验收：一个可复用 CPU 编译入口，生产构建和测试构建实际使用它。
没有把后台热更新、specialization 或 Pipeline 发布算作本轮完成。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| CMake 寻找本机 glslangValidator，版本由机器决定 | 固定 glslang 子模块，构建 comet_shader_compiler |
| 构建命令仅传 -V100、include 和输出文件 | CLI 转换为 ShaderCompiler::Request，共享编译实现 |
| 共享头文件主要靠手填 DEPENDENCIES | 实际 include 生成 depfile，原 INCLUDE_DIRECTORY/DEPENDENCIES 接口保留 |
| 编译工具直接输出目标 SPIR-V | 编译先产出自有内存结果，成功后原子替换文件；失败保留旧文件 |
| 尚无供 Worker 使用的编译 API | 无 Device/窗口/engine 依赖的 CPU 静态工具库，结果拥有字节码和输入快照 |

## 代码变化与理由

### 1. 入口和依赖

```text
tools/shader/compiler.h/.cpp  → glslang（固定子模块）
         ↑
tools/shader/main.cpp         构建 CLI
         ↓
生成 SPIR-V → 现有 spv_to_cpp.cmake → engine 的嵌入字节码

未来 Editor Worker → 同一个 ShaderCompiler → ShaderInterface → owner 创建 GPU 候选
```

最后一行是后续接入方向，本轮并未注册监听器或新增后台服务。
没有让 engine 反向链接工具库，也没有建立“为了编译 Shader 必须先构建 engine”的循环依赖。
CLI 直接复用 `common/file_io.cpp` 的原子写实现作为自身源文件，不复制实现，不为它再创造 FileManager。
工具库仅 compiler.h/.cpp，命令行参数和 depfile 序列化留在 main.cpp；编译入口容易定位。

固定 glslang 提交 `efa016659ffc4f2ae566b6b1db71a70655ac33a1`。
关闭其测试、安装、独立工具、HLSL、外部项目与 SPIRV-Tools 优化器，只启用当前需要的 GLSL → SPIR-V。
本机 SDK 仍提供 Vulkan；源编译不再要求额外安装 glslangValidator。CI 的 glslang-tools 安装项也移除。

### 2. Request/Result 是 CPU 值，不是 GPU 对象

```cpp
ShaderCompiler::Request request;
request.source = source_path;
request.stage = ShaderCompiler::Stage::Fragment;
request.entry_point = "main";
request.defines = {{"USE_FEATURE", "1"}};
auto result = ShaderCompiler::compile(request);
```

Stage 明确区分 vertex/fragment/compute，Target 当前支持 Vulkan 1.0/SPIR-V 1.0 和 Vulkan 1.3/SPIR-V 1.6。
默认前者，保持原构建的目标版本。entry_point 指输出 SPIR-V 入口；GLSL 源函数仍叫 main，编译器负责重命名。
defines 使用有序 map，拒绝非法宏名与换行/NUL 注入；同一 Request 得到一致顺序的 preamble。
后续 variants 可以由这些明确输入组成，不在本轮先建 VariantManager。

Result 拥有 words、dependencies 和 diagnostics。编译失败清空 words，保留可用的文件/行号错误信息。
反射仍由已有 ShaderInterface 处理，不为了共用编译器把 graphics 依赖带入工具库。
glslang 的进程初始化仅一次，每次调用的 Shader/Program/输入快照独立，支持多个 CPU 编译请求并行。

### 3. include 与输入一致性

本地 include 先找当前文件相对路径，失败后查 Request 的 include_directories；嵌套 include 沿调用来源继续解析。
同一编译内，规范化后的文件内容缓存一次；结果保留逻辑路径、实际解析路径与内容。
不存在的搜索候选也记录：例如先使用 shared/value.glsl，之后新增同名本地 value.glsl，会使原快照失效。
符号链接改变目标、文件删除或内容变化同样使 `inputs_unchanged(result)` 返回 false。

单文件及唯一内容总量上限 8 MiB，搜索路径上限 256，include 深度上限 64。
读取/深度异常在 includer 内转成错误，不穿过第三方内部栈；曾遇到硬错误时不允许通过后续搜索“成功”掩盖它。
成功编译后重新核对快照，变化则丢弃结果。后续 Worker 完成到 owner 发布之间仍必须校验 request revision 和输入，
这不是文件系统事务，也不能单靠编译结束的一次检查保证未来发布时仍有效。

### 4. CMake/CLI 实际接入

`compile_shaders()` 的 COMPILER 现在是 CMake target，使用 TARGET_FILE 并把工具目标加入 DEPENDS。
新增 ENTRY_POINT、TARGET_ENVIRONMENT、DEFINES，扩展名提供显式 stage；原共享头接口未删除。
depfile 对空格、#、$ 等路径字符转义；已有头文件变化由 Ninja/Make 自动重建。
SPIR-V 成功后才替换输出，后续 C++ 嵌入头仍沿用原脚本；所有产物留在构建目录，学习 Shader 未修改。

## 架构价值

- 让构建和未来编辑器使用同一种编译行为，而不是只有名字相同的两个包装函数。
- CPU 编译、CPU 反射、设备创建、帧边界发布继续分层；源编译器不会随 engine 进入运行时。
- 源输入快照和不可变结果给后台任务提供明确边界，不让 Worker 持有 ShaderManager/Device 的可变状态。
- 没有新增事件总线、运行时文件监听器或通用资源管理基类；这些不属于本项验收。

## 测试结果

- Debug/Ninja：完整构建，460 个 GoogleTest 全通过；另 1 项 shader_build_contract 通过。
- Release/Make：完整构建，460 个 GoogleTest 全通过；另 1 项 shader_build_contract 通过。
- 新增 8 个 CPU 编译测试：全部生产字节码与构建结果完全相等，stage/entry/target、宏、嵌套 include、
  缺失搜索候选、语法错误/非法选项、递归和大小限制、符号链接改指向、并行请求隔离。
- 上述 8 项重复 20 轮，共 160 次通过；每轮并行测试启动 8 个独立 CPU 编译请求。
- 独立临时 CMake 工程实际验证带特殊字符的 include 路径、头文件修改重编译、生成嵌入头、错误返回码与旧产物保留。
- Release 构建契约测试首轮失败：第二次文件写入落在 Make 时间戳粒度内，未触发构建。
  修正测试为每次写入前跨越时间戳粒度后，Ninja/Make 均通过；没有改编译器来绕过该失败。
- `otool -L` 验证 CLI 只依赖系统库、不加载 engine；engine 的全局符号中没有 glslang 编译符号。
- `git diff --check` 通过；图形测试进程仍串行，CPU 编译压力测试不创建窗口。
- 020 远端 Linux CI `34055640202` 已成功；021 的 Linux CI 需本提交推送后确认，不将本地结果冒充远端验收。

## 限制和后续方向

1. 下一项是 specialization 值、PipelineKey 与反射一致性；当前 Vk specialization 仍为 nullptr。
2. 后台 debounce/有界任务、revision 验票、兼容/不兼容接口发布及 GPU Material 缓存失效仍待实现。
3. depfile 只列存在的依赖；新增原先不存在的同名搜索候选，普通增量构建未必自动触发，需显式重建。
   CPU 快照已能检测该变化，后续编辑器监听必须消费这些候选路径，不能只监控成功 include。
4. snapshot 检查不锁外部文件，也不替代发布时验票；编译器没有进程级超时/沙箱，当前面向受信项目源码。
5. 尚未做优化器、HLSL、持久编译缓存、Shader Artifact/Manifest、跨平台打包或交叉编译的 host-tools 配置。
   全量首次构建增加 glslang 编译成本；运行时二进制不增加该依赖。
6. 手工 MaterialLayout 的语义 metadata 仍独立存在，不从反射推断颜色/默认值。阶段 5、6 均未完成。
