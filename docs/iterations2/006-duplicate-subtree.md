# 006：子树 Duplicate

## 背景与验收

阶段 4 要求复制实体。005 已有完整子树快照与原子恢复边界，本项复用它，不新建第二套克隆／序列化实现。
在 Edit 的 Hierarchy 右键实体选择 Duplicate，复制整个子树，选中新根节点，Undo 一次移除整个副本，Redo 恢复相同副本 UUID。

## 前后对比

- 之前：只能创建空实体，或手动重新添加组件和属性。
- 现在：捕获原子树 → 为每个节点分配新 UUID → 映射内部父级 → 通过已有 EntityTreeCommand 创建副本。
- 原子树不改动；副本根名称追加 ` Copy`，子节点名称、局部 Transform、Camera 字段等按原值复制。
- 外部父级不重映射，副本与原根同父；Mesh/Material 保留同一 AssetHandle，不重复拷贝资产或 GPU 资源。

## 代码及设计理由

`SceneCommands::duplicate_entity` 使用 capture_tree，未知或不能恢复的组件沿用拒绝策略。
先构建旧 UUID → 新 UUID 映射，生成值避开当前 Scene 及本批预留的 UUID。
只有 parent 命中本批旧 UUID 时替换，因而孙节点指向复制后的父节点，而根仍指向原外部父级。
新快照随后交给 EntityTreeCommand 的 creating 方向；失败回滚、历史边界、Redo 值恢复均复用 005。

Hierarchy 只追加一个 Request::Duplicate，不直接创建对象。Editor 复用创建结果的选中逻辑，
仍在结束属性/Gizmo 手势之后操作，Play 菜单禁用。未新增管理类、事件总线或生产代码文件。

## 架构价值

验证前一步子树快照不只是为 Delete 服务：它同时承担“原身份恢复”和“新身份实例化”的数据基础。
身份映射与资产共享分开，避免复制一个实体意外生成新资产、错误复用旧实体 UUID 或让副本层级仍连接原子树。
Scene 文件格式、资源缓存、渲染与 GPU 生命周期不变。

## 测试

- 三层子树复制，校验每层新 UUID、内部关系、外部父级、名称、AssetHandle 与值独立性。
- 原对象后续变化不污染副本；一次 Undo/Redo 覆盖整棵副本并保持副本 UUID。
- 不存在实体、未知子组件和未绑定 Edit Scene 时拒绝复制，不产生半棵树、不清空旧 redo 分支。
- 真实 ImGui 右键菜单产生 Duplicate 请求，菜单绘制期间 Scene 不变化。
- Debug/Release 完整构建、各 365 项测试通过；本项新增 3 项测试。
  使用 `cmake --build --preset dev-debug --parallel 6`、`ctest --preset dev-debug --output-on-failure --timeout 120`，
  Release 使用独立构建目录；修改文件通过 clang-format dry-run，`git diff --check` 通过。
  本提交 Linux CI 需推送后验证，不以前一提交的运行代替。

## 限制与后续

当前内置组件中的实体关系只有父级，因此本项重映射的是层级 UUID；未来脚本字段等加入实体引用时，
必须扩展描述符的引用重映射能力，不能对任意自定义组件承诺自动修正隐藏引用。
Camera 的 primary 字段照值复制，未新增唯一主相机规则。副本名称不保证唯一，不做自动偏移或跨场景剪贴板。
后续先补 Mesh 导入 UI/状态，让 Project 中非 demo 模型具备可用 Artifact，再接资产拖拽到场景；
这属于阶段 3 对阶段 4 内容编辑闭环的阻塞依赖，不同时扩大为完整资产浏览器重构。
