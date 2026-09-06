# 032：金属粗糙度 PBR 与共用 Shader 消费者闭包

## 背景与验收项

031 提供了直接灯光与阴影，但 lit_color 只有 Lambert 漫反射，无法表现金属、高光或粗糙度。
本项完成阶段 5 的基础 PBR 直接照明：参数进入现有材质编辑链路，透视／正交相机给出正确观察方向，
复用三类灯光和方向光阴影，热更新时共享顶点 Shader 的消费者保持一致。不是完整 glTF 材质导入或阶段 5 收尾。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| 三种内置布局，lit_color 只有 albedo | 增加 pbr_color：base_color、metallic、roughness；旧布局不变 |
| Frame binding 0 只有两矩阵、128 bytes | 追加相机位置和观察方向／投影标记，共 160 bytes，供 vertex/fragment 使用 |
| 光源衰减写在 Lambert 函数里 | sample_light 提供方向／辐射强度，Lambert/PBR 各自求 BRDF |
| lit 顶点只有一个 fragment 消费者 | Lambert/PBR 共用顶点，三个文件必须作为完整候选组 |
| 内置 Pipeline 创建与热更新分别维护列表 | 一张 BUILTIN_PIPELINES 表驱动创建、消费者校验和重建 |
| 阴影使用固定角度偏移 | 补充 PCF 采样 footprint 对应的接收面斜率偏移 |
| 默认 demo 使用 lit.mat | app/editor 使用 pbr.mat；其他材质资源继续保留 |

## 代码级逻辑

### 参数与作者数据

`MaterialLayout::find_builtin("pbr_color")` 描述一个 32-byte MaterialData：base_color 位于 0、metallic 位于 16、
roughness 位于 20；尾部 padding 由现有打包器清零。默认值为灰色、非金属、粗糙度 0.5。
Inspector 直接使用布局已有的颜色／浮点控件，金属度范围 [0,1]，粗糙度范围 [0.045,1]；没有增加 PBR 面板分支。
控件实际变化才调用既有提交路径；Material revision 变化使 PreparedMaterial 产生新不可变版本，旧字节仍可被旧帧持有。
CPU 继续拒绝非有限参数；来自文件/API 的有限越界值由 Shader clamp，避免绕开 UI 后产生无效 BRDF。

`assets/materials/pbr.mat` 仍使用现有 YAML .mat 与类型化 .meta，不新增资源格式或管理器。
app/editor 示例引用它，用户可以在 Project 选中后编辑，也可以切回 lit/demo/solid 材质比较。

### 相机是帧输入，不是材质属性

`MaterialRenderer::FrameData` 为私有 GPU ABI，前 128 bytes 保持 ViewProjectMatrix。
render 时从 view 的逆矩阵取得相机位置与朝相机的 +Z 方向；当前标准正交矩阵的 projection[2][3]==0，
据此写入正交标记。透视片元使用 camera_position-world_position，正交片元使用统一 view_direction。
不能只使用相机位置：那会让正交平面两端出现错误的高光差异。

`frame.glsl` 集中声明该 ABI，lit vertex 与 PBR fragment 共用；FrameSet binding 0 的可见阶段扩展为 vertex|fragment。
旧 unlit vertex 继续读取两矩阵前缀，Debug 的独立相机 UBO 不改变。160-byte 大小及 128/144 offset 有 CPU/反射测试。
这不把 RenderCamera、Scene 或编辑器相机对象传入材质系统，也不新增只用于包装字段的公共类。

### BRDF 与光源分离

`lighting.glsl::sample_light` 提供归一化方向、颜色×强度×距离／锥形衰减；两种受光材质复用它和 shadow_visibility。
PBR 使用 GGX 法线分布、height-correlated Smith 可见度及 Schlick Fresnel：粗糙度先平方得到 alpha，
介电 F0 为 0.04，金属 F0 为 base_color，按 metallic 混合介电／金属响应；漫反射只保留非金属部分。
公式对照了 [Khronos glTF BRDF 附录](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc#appendix-b-brdf-implementation)，
这里只采用直接照明模型，不声称支持该规范的完整材质特性。

低粗糙度时，GGX 分母改写为避免相近浮点数相减的形式；背光／退化法线／无效观察方向返回黑色。
最终 HDR RGB 限制到 half-float 最大有限值 65504，避免极亮高光把后处理输入写成 Inf；输出 alpha 固定 1。
场景没有环境光或 IBL，因此关掉所有灯后黑色是当前预期行为，不是材质加载失败。

### 热更新的真实依赖

BUILTIN_PIPELINES 同时记录布局名、vertex/fragment 名称和嵌入字节码。
候选中对每个 Pipeline 要求 vertex 与 fragment 同时出现或同时缺席；共享顶点因此递归约束所有消费者。
Lambert/PBR 三文件和 unlit 三文件可分别更新，也可一起更新；未知名称、空组、缺少任一消费者都提前拒绝。
编辑器现有后台编译请求增加 PBR fragment，include 快照自动覆盖 frame.glsl 和 lighting.glsl。
实际 Shader stage 决定是否允许忽略可重建的 MaterialSet，不再通过手工判断几个顶点文件名决定。
后面的反射、CPU/GPU 候选准备、帧边界发布和旧 FrameSlot owner 保留沿用已有协议。

### 回归暴露的斜面自阴影

PBR 阴影测试在 45° 入射、远离遮挡者的位置观察到输出从 189 降为 165，原因为 3×3 PCF 比较相邻 texel 时，
固定 bias 不足以覆盖同一接收面的深度变化。没有通过放宽像素容差或移动断言位置掩盖问题。
现在把接收面的两个切向量变换到 light clip，叉乘求投影平面，换算 UV/depth 梯度；
按 nearest 采样最远 1.5 texel footprint 增加斜率 bias，并以 0.01 归一化深度封顶。
Lambert/PBR 共用修正；这仍是有界偏移近似，不是无偏阴影算法，极斜面与贴近接触处可能有漏光或脱离感。

## 架构价值

- 作者属性、帧相机和光源采样分层明确；新增材质不需要扩充 SceneRenderer 的材质分支或 Inspector 回调。
- 相同光源／阴影实现服务多个 BRDF，不复制 attenuation、灯索引或阴影绑定。
- Pipeline 表成为创建与更新的同一事实来源；共享 Shader 变化不会只更新一个消费者。
- 本次未新增引擎管理类，也没有引入 EventBus、RenderThread 或新的资源退休机制。

## 测试结果

- Debug/Release 最终构建与完整 CTest 各 5/5 通过；主集 526 tests（525 通过、同步故障对照按协议跳过），
  专门同步验证运行全部 16 个 GPU 项，另有 10 个 WSI 故障恢复及两个独立契约测试。
- 16 个 GPU 项开启同步验证重复 20 轮，共 320 次通过，包含缺失 barrier 的未提交故障对照。
  日志 `/tmp/comet-032-repeat.log`；完整结果 `/tmp/comet-032-verified-debug.log`、`/tmp/comet-032-verified-release.log`。
- C++ 修改通过 clang-format dry-run；git diff --check 通过。Shader 未批量格式化，学习源码未删除。

- 新 CPU 测试核对布局、默认值、参数打包、旧版本不变与 Frame ABI；项目资源测试核对 PBR 源资产。
- 新 UI 测试操作实际 Metallic/Roughness 控件，检查按变化发布和 idle 不重复提交。
  首轮错误地要求 fake callback 同时落盘；该 fixture 只捕获提交，已改为验证其真实职责，未改生产保存路径。
- 新 GPU 测试对照独立 double CPU BRDF，覆盖 12 种参数／视角／投影／灯型／退化输入／高能量组合。
- 新 GPU 阴影测试检查投影中心变暗、未遮挡区不变，且切换阴影不创建 MaterialSet。
- 新 GPU 热更新测试保留旧在途帧，同时更新共享顶点的 Lambert/PBR 两个消费者；部分候选被拒绝。
- 一次直接测试在 glfwInit→Cocoa NSApplication run 停滞，约 130 秒后采样并结束（SIGTERM，143）；
  后续一次完整 CTest 又在另一 fixture 的相同初始化位置停滞，由 120 秒超时处理。两次均在 Vulkan 初始化之前，且本机 GPU 测试串行。
  栈采样保留在 `/tmp/comet-032-hang.sample.txt` 和 `/tmp/comet-032-ctest-hang.sample.txt`，不把异常运行记为通过。
- 031 远端 CI `34063046726` 已成功；032 CI 以本次 push 后实际运行结果为准。未声称完成手工窗口视觉巡检。

## 限制与后续方向

当前为不透明纯色基础 PBR，没有纹理、法线贴图、IBL、透明、多次散射或 glTF 材质自动转换。
投影判定针对引擎当前标准透视／正交矩阵，不是任意用户投影矩阵协议。
阴影固定分辨率／单灯和完整场景扫描等限制保持；Cocoa 重复初始化停滞需要独立生命周期排查，不能以串行运行宣称已根治。
下一渲染验收项 Bloom；随后必要 CPU/GPU／内存诊断和阶段 5 回顾，再推进阶段 6，未缩小原目标。
定期架构回顾仍为 035 或阶段 5 边界（先到者）。
