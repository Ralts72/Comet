# 052：选中高亮、Gizmo 与 Undo/Redo 架构审计

## 目标

在继续增加可见编辑器渲染能力前，核对当前 Shader、Pipeline、Selection 和属性修改链路，确定不会形成临时死角的实现顺序。本步结论：

1. 删除未编译、无调用方且属于旧架构的 axis/cube/triangle Shader 与旧 PBR include；
2. 不用现有材质 Shader 临时 tint 选中对象，也不用 ImGui 画无深度的屏幕框冒充高亮；
3. 先建立 Editor Undo/Redo 命令边界，再让任何 gizmo 修改 Transform；
4. 随后建立可复用的 DebugDraw 数据与渲染路径，以 bounds 线框完成选中高亮，再让 gizmo 复用同一路径；
5. GPU ID picking 和多 pass outline 仍留给阶段 5 RenderGraph，不为当前需求扩张传统 RenderPass attachment。

## 现状审计

### 遗留 Shader 不是可复用基础

`engine/shaders/CMakeLists.txt` 只编译 `cube_texture.vert/.frag`。原目录中另外存在：

- `axis.vert/.frag`；
- `cube.vert/.frag`；
- `triangle.vert/.frag`；
- `constants.h`、`gbuffer.h`、`mesh_lighting.h/.inl`、`structures.h`。

这些文件没有任何生产或测试引用。特别是 axis shader 使用 set 0 storage buffer、旧的 `PointLight` 数组、`selected_axis` buffer 和
`CHAOS_LAYOUT_MAJOR` 宏；当前 SceneRenderer 使用 uniform buffer + 两个 Texture sampler + model push constant，CPU 侧也没有与旧 buffer 匹配的
结构或 descriptor 创建路径。把它加入编译不会得到 gizmo，只会重新引入一套与现有 Pipeline layout 冲突的旧协议。

旧 PBR/GBuffer include 同样没有对应 pass、attachment、材质参数或 CPU layout。继续保留会让目录看起来已经拥有尚不存在的 deferred/PBR
架构，并给后续实现错误锚点。本步删除这些文件，同时让 Shader 编译函数仅在调用方真实提供 include directory 时才传 `-I`；未来
DebugDraw、Gizmo 和阶段 5 材质 Shader 从实际稳定的数据契约重新建立。

### 当前 Selection 和渲染边界

Selection 是 Editor 状态，只保存 EntityId 或 AssetHandle。RenderSubmission 已带 EntityId、model matrix 和 Runtime Mesh，因此选中高亮可以在
Editor 提交“需要装饰的 EntityId”后，由渲染侧关联实际 draw；但不应让 Scene、Mesh、Material 永久保存 selected bool。

当前 SceneRenderer 只有一个传统 forward subpass 和一个固定材质 pipeline。它没有 editor pass、debug primitive batch、stencil outline、
整数 ID attachment 或 RenderGraph resource declaration。任何直接塞入 `SceneRenderer::render()` 的特例都会让阶段 5 再拆一次。

### 当前 Transform 修改没有事务边界

Inspector 的 PropertyEditorRegistry 直接把 ImGui 控件写到组件字段，MenuBar 的 Undo/Redo 还是空回调。鼠标 drag 的每个中间值都立即可见，但系统
不知道一次操作何时开始、何时提交，也没有 before/after snapshot。此时先实现 gizmo 会造成同样的直接写组件路径，之后难以判断一次拖拽应该撤销
一帧还是整个手势。

## 方案对比

### 选中对象直接 tint

给现有 `cube_texture.frag` 增加 selected 分支实现最短，但它把 Editor 状态写入固定材质协议；阶段 5 的 ShaderInterface/MaterialLayout 会再次删除，
并且 tint 不能表达被遮挡轮廓或 gizmo。拒绝作为正式路径。

### ImGui 屏幕框

把 world AABB 投影后在 ImGui draw list 画矩形不需要 Vulkan pipeline，但矩形既不是旋转 bounds，也没有场景 depth，遮挡、近裁剪和 1x 裁切时均会
给出错误视觉。它适合临时诊断，不适合作为用户选择反馈。

### 立即增加 stencil/ID pass

轮廓质量最好，但需要 attachment format、MSAA、barrier、target generation 和 readback/pass 生命周期。当前阶段刚完成 generation 审计，正确位置是
阶段 5 RenderGraph，不应再把一套 editor-only attachment 手工焊入传统 RenderPass。

### 通用 DebugDraw 线段路径

推荐先建立 CPU `DebugDrawList`（line vertices + color/depth policy）和渲染侧 DebugDraw executor，在现有场景 pass 内使用独立简单 pipeline。它可复用：

- 选中 Mesh world AABB；
- gizmo 轴线与手柄；
- camera/frustum、碰撞体、光源和导航调试；
- runtime diagnostics。

它不是为了 ImGui 创造的类，也不修改 Material。阶段 5 只需把 executor 挂到 graph pass，CPU submission 契约可以保留。

## 推荐实施顺序

### 1. Editor Command History

先建立 editor-only command/history：execute、undo、redo、clear、分支后清空 redo；场景替换、New/Open 和 Edit/Play owner 切换时清空。命令引用实体时应使用
稳定 EntityUuid 并在应用时解析，不长期保存 Entity 或组件裸指针。

第一步只建立可靠历史与 MenuBar/快捷键边界；gizmo 手势最终以一次 before/after Transform command 提交，而不是每帧 push 命令。

### 2. DebugDraw 基础

建立 backend-neutral line list 和独立 pipeline/executor。CPU 数据不含 Vulkan 类型；GPU buffer 和 pipeline 属于渲染侧，按 frame slot 与当前
RenderPass generation 管理。默认 depth-tested，必要时再显式增加 overlay/x-ray policy，不能靠修改 clip depth 绕过。

### 3. Bounds 高亮

Editor 只把当前 Selection 对应的 world AABB 转成 12 条线，复用第 051 步 `transform_box()`。无选中或 Mesh 未就绪时提交空列表。此时用户已有
稳定选择反馈，同时验证 DebugDraw 的 camera、depth、resize 与 target generation 对齐。

### 4. Gizmo 与 Transform Command

Gizmo 复用 Viewport pixel mapping、当前 Camera 和 DebugDraw。轴命中属于独立纯数学；拖拽开始捕获 before Transform，拖拽中只做 preview，结束时把
before/after 作为一个已应用命令进入 history，取消则恢复 before。Inspector 后续也接同一属性事务边界。

## 所有权边界

- Editor：Selection、工具模式、gizmo 手势、CommandHistory、构建 debug draw 请求；
- Scene：实体和可序列化 Transform，不保存 selected/gizmo 状态；
- 通用 geometry：ray、AABB、transform，不依赖工具或渲染；
- Renderer：把 DebugDraw submission 与当前 Camera/RenderTarget 一起编排；
- DebugDraw executor：拥有 line pipeline 与 per-frame GPU 数据，不解析 Selection；
- RenderGraph（阶段 5）：未来决定 outline/ID 等额外 pass 与 attachment 生命周期。

## 验证

- `rg` 确认删除文件除自身外没有生产、测试或构建引用；
- Shader 编译目标仍只包含当前生产 `cube_texture` pair；
- 完整 Debug 构建验证删除遗留文件不影响 engine、app、editor 或 tests；
- 完整 CTest 回归。

本步不改变可见画面，无需手工 GPU 验证。

## 下一步

实现 Editor Command History 与 MenuBar/快捷键接线。先覆盖 execute/undo/redo、redo 分支失效、容量边界和 scene owner 切换清空，再让后续 gizmo
通过稳定 EntityUuid 提交 Transform command。
