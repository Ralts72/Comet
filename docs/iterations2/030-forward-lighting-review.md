# 030：三类 forward 灯光与阶段性架构回顾

## 背景与验收项

029 打通 HDR→fullscreen→SDR，但场景颜色仍只来自不受光材质。阶段 5 下一独立验收项是
LightComponent → 渲染快照 → FrameSet → 实际 forward Shader，并让编辑、保存、撤销继续使用既有架构。
本项实现方向光、点光和聚光的 Lambert 漫反射，不把它宣称为阴影、PBR 或完整 forward 渲染阶段的完成。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Scene 只有 Transform/Camera/Mesh 等编辑组件 | 新增可增删、保存、克隆和撤销的 LightComponent |
| 属性支持 bool/float/vec3/asset/string | 新增 typed enum 描述符，Inspector 使用下拉选项，文件/历史使用稳定字符串 |
| RenderScene/Submission 没有灯光 | 拥有 RenderLight 值快照，不借用组件地址 |
| FrameSet 只有 vertex 相机矩阵 | binding 0 仍是相机，binding 1 为 fragment LightingData |
| 两种不受光材质 | 保留原语义，增加独立 lit_color 材质与两个 Shader |
| camera_world_matrix 实际省略本地 scale | 准确命名为 pose_world_matrix，供 Camera/Light 共用 |
| 示例直接显示纹理颜色 | 示例使用 lit.mat + Key Light，可调灯光观察明暗 |

## 代码级链路

```text
LightComponent + WorldTransformComponent
  → SceneExtractor：过滤 enabled=false，复制世界位置和 pose 的 -Z 方向
  → RenderScene.lights → SceneResolver → RenderSubmission.lights
  → LightingData::prepare：校验、排序、32 灯上限、std140 打包
  → MaterialRenderer::FrameResources[slot].lighting → FrameSet binding 1
  → material_lit.vert / material_lit.frag + lighting.glsl
  → 原有 HDR / tone mapping / SDR 输出
```

### 场景属性与枚举

LightComponent 保存 `type/enabled/color/intensity/range/inner_angle/outer_angle`，没有 GPU 对象。
LightType 同时用于场景数据和渲染值快照，显式指定 Directional=0、Point=1、Spot=2，与 Shader 打包协议对应。
方向由 Transform 本地 -Z 决定；Range 是世界距离，内外角是聚光半锥角。

PropertyDescriptor 增加 Enum 选项表及 typed read/write 适配。`make_enum_property_descriptor` 在真正的 C++ Enum 与
稳定名字间转换，不把 enum 指针强转成 int/string 指针。`PropertyValue` 沿用 string 保存枚举快照，未知名字/原生非法值明确拒绝。
Inspector 编辑的是值副本，再经 assign_value 提交；现有 PropertyEditTransaction、CommandHistory、组件增删和 Scene clone 无需灯光专用命令。
UI enum 选项使用稳定 ID，避免同名显示标签冲突。序列化复制属性统一走 copy_value，并保留有限数检查，删除重复的手写类型复制分支。
加载仍不触发 UI 专属 on_changed，也不会把只读/可编辑策略错误地混成文件加载策略。

`pose_world_matrix = parent_world * local_TR` 保持原 camera 矩阵语义，仅改准确名字并增加灯光消费者。
它不包含本地 scale，但仍包含父级变换；不是剥离全部父级缩放的严格刚体矩阵。灯光方向在打包时归一化，零方向被拒绝。

### 渲染数据与 Shader

`render/lighting.h/.cpp` 放在一起描述 RenderLight 和 LightingData；没有新增 LightManager/Store/Service 目录。
LightingData 是真实 GPU ABI，不是为了缩短成员列表而包装的类：每灯 4 个 vec4，末尾 counts，最大 32 灯，总计 2064 bytes。
CPU static_assert 与 SPIR-V 反射测试同时核对 block size 和 counts offset。
按 EntityId 稳定选择当前快照的前 32 个有效灯光；超限、非法颜色/强度/范围/锥角/方向分别统计。
不相关的字段（如点光方向、方向光位置/range）不影响有效性，打包成确定的安全值。

MaterialRenderer 每 slot 增加一个 LightingBuffer，沿用同一个 FrameSet；等待 slot 后才写入，FrameResources 由该帧保留。
灯光变化只改变帧常量，不增加材质 revision、不重建 MaterialSet。缺少有效灯光时 lit 物体为黑色，没有隐藏环境光。
遗漏灯光数量变化时记录 Log，避免逐帧刷屏；细节统计通过 MaterialRenderer::Statistics 暴露。

lit_color 只定义 albedo 参数，vertex Shader 输出世界位置和逆转置法线；原 material_mesh/unlit fragment 不改变接口或视觉语义。
奇异变换给出零法线，非有限法线在 fragment 中跳过，不把 NaN 传播到光照结果。
方向光使用 N·L；点光采用有限 range 的平滑衰减及有近距离下限的反平方项；聚光再乘内外锥角平滑权重。
漫反射除以 π；Intensity 是本轮约定的照明强度参数，并非宣称已经实现完整的 lumen/candela 单位系统。
极窄锥角的余弦可能在 float 中相等，改为硬边判断，避免 smoothstep 的零宽区间。

`lighting.glsl` 是本次真实使用的共享头文件；构建 depfile 和编辑器源输入快照均跟踪它。
修改该头文件后已观察到仅相关 lit fragment 重新编译。原 INCLUDE_DIRECTORY/DEPENDENCIES 契约不需要另外发明一套。

### Shader 发布与示例

MaterialRenderer 的热更新接受完整 unlit 三 Shader、完整 lit 两 Shader或完整五 Shader，拒绝半组及额外未知项。
候选 Shader/布局/Pipeline/驻留绑定全部准备成功后再发布；Frame 固定接口仍不能热改。
editor 当前监控完整五 Shader 组，共享头变化也触发编译；Debug 两 Shader 保持独立。
新增 lit.mat 及稳定 .meta，与 app/editor 的 Key Light 一起进入仓库。旧 demo.mat/solid.mat 保留，可从 Inspector 切回。

## 设计理由与架构价值

- 光照放在 Frame 层，不塞到每个材质参数，也不在渲染时读取可变 Scene。
- 不受光与受光材质分开，新增功能不偷偷改变旧模板含义。
- enum 是现有属性系统支持的新值种类，序列化/命令/UI 复用同一份元数据，不写 LightInspector 或灯光专用历史。
- 真实 CPU/GPU ABI、GPU owner、场景作者数据三者分开；prepare 是 CPU 工作，不创建或提交 Vulkan 对象。
- 热更新沿用已有候选事务和 FrameSlot 保留，不增加全局通知系统或独立退休队列。

## 030 定期架构回顾

| 维度 | 本轮核对与处理 | 保留的后续项 |
| --- | --- | --- |
| 目录 | lighting 的快照/打包放同一对文件，Shader 头放实际使用目录；组件仍集中在 components.h | 不为三个灯类型建立三套目录或 Service |
| 职责 | Scene 管数据，Extractor 复制，LightingData 打包，MaterialRenderer 管帧 UBO，PostProcessRenderer 管显示输出 | Shadow pass、PBR 与 Bloom 按实际需求增加对应执行者 |
| 依赖 | lighting 只引用 CPU 组件/数学定义，不依赖 Scene/Entity 指针、ImGui、Device 或线程池 | RenderThread 仍需实测与 owned frame packet，不先加空 System |
| 冗余 | 删除 serializer 的重复属性复制 switch；camera-specific 名字改成共享 pose；原材质测试改为基线数量而非固定两模板 | FrameSet 相机/光照上传可在阶段 6 按 dirty 数据优化 |
| 生命周期 | GPU 帧保留 FrameResources/材质版本；旧帧与新 Shader 像素分别验证；HDR/输出成对 resize、WSI 恢复回归通过 | surface/device 恢复及不兼容输出格式仍按路线图处理 |
| 回调 | 枚举读写是同步类型适配，不是广播事件；编辑事务复用现有 CommandHistory | 仍只在真实一对多需求出现时引入通知，不建立全局 EventBus |

RenderLight/LightingData 的分开是作者空间值与 std140 ABI 的区别，不是重复 owner。
当前排序只保证给定快照/EntityId 的稳定选择，不能将其声称为跨任意场景重建都不变的光源优先级系统。
全场景 Transform 重算和整份灯快照复制仍存在；这是阶段 6 dirty/subtree 与帧准备优化的既有计划，不在此处靠字段包装掩盖。

## 测试结果

- Debug、Release 构建成功，各 5 个 CTest 项通过；主测试集 516 tests，普通运行 515 通过、同步故障对照按协议跳过。
- 专用 sync-validation 项运行全部 10 个 GPU 图/渲染测试；10 项 WSI 故障恢复、两个独立契约测试继续通过。
- 新增 6 个 CPU 测试：层级提取/快照所有权、确定上限、非法参数、SPIR-V ABI、enum/撤销/clone/序列化、错误元数据。
- 新增真实 ImGui 下拉选择测试：选 Point → 一条历史 → undo/redo，最终无悬空属性事务。
- 新增 GPU 像素测试：17×17 上逐像素验证三种灯、背向、非均匀缩放、奇异变换、极窄锥角；检查灯光更新不新建材质 binding。
- 新增 lit 热更新两帧测试：旧帧保持原颜色，新帧使用新 Shader；破损字节码及改变 Lighting Frame binding 都拒绝发布。
- 灯光 CPU、ImGui enum 和 GPU 图测试共 17 项，在同步验证开启下重复 20 轮，共 340 次通过；日志 `/tmp/comet-030-repeat.log`。
- 首轮两个材质测试失败是固定 4/2 pipeline 总数的旧断言（新增 lit 后实际为 5/3）；改为基线+两个旧版本及回收到基线，保留寿命验收。
  首轮结果保存在 `/tmp/comet-030-initial-regression.log`。新测试头文件路径和 nodiscard 警告已修正并重新构建。
- 本轮所有 C++ 文件通过 clang-format dry-run，git diff --check 通过；未批量格式化 Shader 或第三方文件。
- 上一项 029 的 Linux CI `34061527931` 已成功；本项以 push 后自身 CI 结果为准。

## 限制和后续方向

- 尚无阴影、PBR/高光、IBL、Bloom、自动曝光；不提前标记阶段 5 完成。下一项为方向光 shadow pass 与采样依赖。
- 32 灯是简单 forward 的明确上限，不是 clustered/tiled 光照；多灯优先级/空间筛选等需场景负载驱动。
- Intensity/Range/角度有效域由 prepare 守卫，UI 显示全部字段，非当前灯型的字段被忽略；极窄锥角退化硬边。
- 材质暂为纯色 albedo；默认示例可运行并受光，但不是完整物理光度标定或大规模性能演示。
- 自动测试覆盖真实像素、编辑器控件与资源寿命；不把尚未进行的手工视觉巡检或远端平台测试写成通过。
- 下一次定期架构回顾为 035 或阶段 5 边界，取先到者。
