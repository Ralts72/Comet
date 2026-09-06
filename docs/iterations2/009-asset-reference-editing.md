# 009：类型化资产引用编辑

## 背景与前后对比

008 能创建模型实体，但 MeshRenderer 的 Mesh／Material 仍是数字输入框。用户需要知道 Handle，
并且写入新 Handle 不会加载 Runtime 资源。材质已有纹理下拉选择，本项把 Project 拖拽接入同一路径。

| 之前 | 本项之后 |
| --- | --- |
| Mesh／Material 的 AssetHandle 没有目标类型信息 | PropertyMetadata／PropertyDescriptor 带可选 `asset_type` |
| 引用直接输入数字 | 类型过滤的路径下拉框、None 和 Project 拖拽 |
| UI 写入 Handle 后可能没有对应 Runtime 资源 | UI 提交请求，应用层加载成功后再修改引用 |
| Mesh 独立拖拽格式 | 统一 AssetDragPayload，携带 Handle、资产类型、文档 generation |
| 材质纹理只能下拉选择 | 同一纹理槽也接收 Texture 拖拽，仍走已有材质更新回调 |

## 代码级逻辑

### 属性元数据

`make_property_descriptor()` 复制 `asset_type` 元数据。MeshRenderer 的 `mesh` 声明 Mesh，
`material` 声明 Material。注册表拒绝在非 AssetHandle 字段上声明资产类型，也拒绝 Unknown 类型。
这是字段语义，不是 ImGui 配置；保存仍写 Handle，场景格式不变。没有 AssetDatabase 的纯属性赋值不擅自访问磁盘。

### Project 与 Inspector

Project 的实际 Selectable 行提供 Mesh／Material／Texture drag source。载荷采用值复制和 `ImGuiCond_Once`，
跨帧不借用列表记录。实际 UI 测试验证开始拖动不会把当前实体选择切换为资产选择，且历史 generation 改变后
载荷仍保留最初 generation。

Inspector 对带 `asset_type` 的引用使用数据库感知控件，其余 Bool／Float／Vec3／String 和未类型化 Handle
仍交给 PropertyEditorRegistry。数据库感知控件不塞进无状态数值控件分发器。

- 下拉按声明类型过滤资产，显示路径；无效旧 Handle 显示 Missing / invalid，不隐式清空。
- None 发出空 Handle 请求。
- 拖拽先检查 payload 格式、声明类型、数据库真实类型和 generation，再接受交付。
- 相同引用不产生请求；`take_asset_assignment()` 一次性取走 Target UUID／组件 ID／属性 ID 和资产值。
- UI 不在组件访问期间创建 GPU 资源，也不保存指向组件的地址。

### 应用层与历史

`EditorApp::handle_asset_assignment()` 在 UI 尾部重新解析当前 Scene、UUID 和属性描述，拒绝跨文档或失效目标。
先结束 Inspector／Gizmo 活动交互，再按类型调用 AssetManager 的现有加载入口。
Mesh 仍只加载已发布 Artifact，不在引用编辑中解析 glTF。加载失败不写入新引用。

Edit 使用 `PropertyEditTransaction::begin → preview → commit`，一次选择／拖拽对应一条可撤销属性命令。
Play 的下拉引用调试仍可修改 Runtime 属性，但不进入 Edit 历史；Project 不在 Play 开始新的资产拖拽。

### 材质纹理槽

Texture 拖拽更新 MaterialData 候选值，然后复用 `validate_material()` 和 `update_material()`。
保留首次修改前的完整 MaterialData；更新失败恢复旧值，成功由既有 AssetManager 保存文件并更新 Runtime 材质。
日志仍在 Log。它是资产文件编辑，不制造 Scene 历史命令。

## 设计理由与架构价值

- 字段语义来自描述符，不在 Inspector 用 `mesh_renderer.mesh` 字符串硬编码目标类型。
- 一对一请求直接在 UI 尾部消费，不新增回调接口或全局事件系统。
- 跨帧身份与加载前置条件显式化；无效资产不会先写入组件再让渲染层发现错误。
- Scene 属性撤销和资产文件更新保持不同事务边界，不让一个 Undo 意外回滚共享材质文件。
- 新测试文件按“资产编辑 UI”职责组织，没有为单一字段增加生产代码类。

## 测试结果

- Debug／Release 构建与全量测试各 **382 tests** 通过，变更 C++ 遵循 clang-format，`git diff --check` 通过。
- 新增元数据非法组合测试；原有注册表测试同时验证 Mesh／Material 类型声明。
- 新增 4 个 ImGui UI 测试：真实 Project source 与选择／generation 生命周期、引用请求与共享历史、
  错误类型／伪造类型／失效 Handle／旧 generation 拒绝、材质更新失败回退及无效重复更新抑制。
- 更新 Viewport 用例，确认 Material 不会作为 Mesh 放置，也不显示为可接受的 Mesh 交付。
- 材质 UI 测试用回调捕获候选而不写项目文件；真实磁盘／GPU更新由已有 AssetManager 回归覆盖。
- 首轮材质槽测试落在控件右侧：CursorPosPrevLine.x 是上一控件右边缘，不是行起点。
  修正为 CursorStartPos.x 后通过，没有修改产品行为迁就测试。
- 全量图形测试日志无 VUID；没有进行桌面人工验收。本项 Linux CI 以推送后的实际结果为准。

## 限制与后续

- `asset_type` 是语义元数据，基础 PropertyDescriptor 不查数据库；加载／类型核验由编辑应用负责。
- Inspector 的类型化选择支持当前 Mesh／Material／Texture 运行时入口；Shader／Scene 引用编辑未扩展。
- GPU 首次创建仍同步发生在 owner 线程；任务背压、上传预算另行推进。
- 本项发现：重启后打开已保存场景还缺少非示例资产的统一加载入口。009 不声称解决此问题。
  010 将完成该独立闭环，并按约定审查目录、职责、依赖、冗余和生命周期，尤其关注 editor.cpp 中增长的资产编排。
