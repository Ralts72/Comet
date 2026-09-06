# 013：缩放 Gizmo 与阶段 4 核心验收回顾

## 背景与前后对比

已有平移、旋转可以直接编辑视口中的实体，但缩放仍需 Inspector。本项补上第三种变换工具并复用已有事务。

| 之前 | 之后 |
| --- | --- |
| Move／Rotate | Tool 增加 Scale，固定本地轴 |
| XYZ 三个手柄槽 | 增加 All 联合缩放手柄；其他模式该槽为空 |
| 箭头／旋转环 | Scale 轴末端方块，中心白色方块负责比例缩放 |
| 距离／角度步长 | Scale step，默认 0.1，相对起点吸附 |
| translation／rotation 事务 | 增加 scale 事务，三种工具共享捕获、取消、撤销 |

## 具体代码和算法

`TransformGizmo::Mode` 增加 Scale，`Axis::All` 表示三轴联合缩放，Context 保存初始 scale。
原 XYZ 方向数组仍只有三项；生成轴、角度和轴方向计算不会把 All 当作 Vec3 的第四个坐标。

`make_context()` 对 Scale 始终采用 `parent_world * local_rotation` 的归一化方向，不乘自身 scale。
这使零／负缩放实体仍可以操作；非均匀父级改变真实显示方向，但不修改用户的旋转或父级。
Scale 模式不提供 World，因为沿世界轴非均匀缩放一般不能仅靠本地 scale 表达。
Tool 明示 Local，保留 Move／Rotate 先前的空间设置，切回时不用重新选择。

单轴仍使用已有 `axis_parameter()`，由鼠标射线与真实轴求最近参数；中心手柄采用屏幕右上方向的投影距离。
距离除以手势起点的 `axis_length` 得到无量纲增量 delta。中心方向按 90 个逻辑点对应 delta=1 归一化。
Snap 对 delta 使用 `round(delta / scale_step) * scale_step`，不是把原始 scale 强制吸附到绝对网格。

```cpp
// 单轴：其他分量不变，能从零恢复，也能穿过零。
scale[axis] = initial_scale[axis] + delta;
// 中心：统一比例，保留原有三轴比值和零分量。
scale = initial_scale * (1 + delta);
```

单轴增加的是组件单位，中心增加的是比例；两者都相对起点，不逐帧累乘，避免结果随帧数漂移。
中心命中优先于经过原点的 XYZ 轴，避免白色方块只能偶然点中。
所有更新仍经 PropertyEditTransaction，只写 scale。外部旋转／位置、父级、相机、布局、文档变化取消；
自身 scale 预览不使上下文失效。跨零不会提前结束手势，释放只新增一条历史。

## 设计理由与架构价值

- 使用同一个 TransformGizmo，而非每种模式一个重复维护输入和历史的类。
- 轴使用本地 scale 的真实语义，不通过矩阵分解悄悄改变 rotation，避免引入当前 TRS 表达不了的剪切。
- 单轴采用加法允许修复零分量；中心采用乘法保留比例。UI 与 README 明确区分，不混称世界尺寸。
- 交互设置留在 editor，组件、序列化、运行时和渲染器不需要增加 Gizmo 专用字段。
- 无新增外部库、Shader、GPU 对象或生命周期队列。

## 测试结果

- Debug／Release 全部构建成功，各 **404 tests** 通过；图形日志无 VUID／Validation Error。
- 新增 5 个直接交互测试：非均匀镜像父级与零分量恢复、中心比例缩放跨零、正交／透视与 HiDPI 吸附一致性、
  非法设置与上下文取消、三种变换连续操作后的 SceneSerializer 恢复及世界矩阵等价性。
- 新增 1 个真实 ImGui 帧测试：中心方块捕获、比例／吸附、与拾取和相机互斥、释放以及 Undo／Redo。
  Tool 菜单测试同时覆盖 Scale 选项和默认步长。
- 完整回归包含场景文件保存／加载、Play 克隆隔离、结构历史、拾取与选中绘制、帧准备顺序、目标替换及 GPU retention。
- clang-format／diff 检查通过；没有桌面人工操作／截图验收。
- 012 Linux CI 34051356460 在本项验证时仍运行中；不预报成功。本项 CI 待推送后验证。

## 阶段性架构回顾：阶段 4 核心边界

此次是阶段边界回顾，额外于每 5 项的定期审查；015 的审查计划不因此取消。

| 维度 | 复核结论 |
| --- | --- |
| 目录 | panels 是界面，editor/src 是编辑交互／文档服务，engine/scene 是数据，render/scene 是提取与绘制；本次没有新增碎文件 |
| 职责 | Gizmo 负责数学和手势，ViewPanel 负责 ImGui；CommandHistory／SceneCommands 分别保存属性事务和结构快照；Editor 只在 UI 尾部协调请求 |
| 依赖 | editor 依赖 Scene 描述符和渲染相机值，engine 不反向依赖 editor；场景只存 AssetHandle，资源发布回 owner 后再恢复引用 |
| 冗余 | 三种变换共用射线、布局、UUID、generation、事务；没有重新实现三套捕获／撤销或增加无状态包装类 |
| 生命周期 | shutdown 先解除渲染回调、取消视口／属性手势、解绑历史，再释放 ImGui／面板／文档／资产服务；registry 早于事务构造且晚于它们销毁 |

复查 Editor 的 UI 尾部顺序：菜单、结构请求、Mesh 放置、引用赋值、模式请求、相机更新、Gizmo 绘制；
随后 Engine 进行当前 Scene 提取。提交者明确，不用业务广播代替有结果的命令。
SceneDocument 继续用 getter／replacer 表达所有权，资产操作继续经服务返回结果；目前没有必须引入的全局事件总线。

核心验收证据覆盖：名称、组件、实体和层级历史，Duplicate，导入／拖拽／类型化引用，三种 Gizmo 与吸附，
逻辑屏幕／纹理坐标、裁切／resize、保存恢复及 Play/Edit 隔离。路线图标注的是**本轮核心验收通过**，不是所有扩展都完成。

仍保留的边界：Prefab／复制粘贴／搜索／缩略图、精确拾取、多视口、资产文件事务、历史内存预算。
阶段 5 继续处理多布局材质、Shader／Pipeline 版本、多 pass 与 WSI 恢复；阶段 6 才引入运行时输入与 System。
后台任务背压／完成发布预算是进入这些工作前的独立负载控制项，下一步先完成。

## 限制和后续

- 中心手柄不会恢复原本为零的分量，使用单轴手柄或 Inspector 修复；允许负缩放，不替渲染材质决定镜像面的剔除策略。
- 单轴增量按起点手柄长度归一化，透视倾斜轴的屏幕投影会缩短；这不是屏幕上任何方向固定像素对应固定物体尺寸。
- 未增加平面缩放、自由包围盒缩放、世界非均匀缩放或保存工具设置；此类交互不是本轮核心验收前置项。
- TransformGizmo 仍是一个职责完整的交互文件；若未来工具模式增加到无法清晰维护，再按真实算法拆分，而非仅为减少行数拆类。
