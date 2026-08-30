# 053：Editor Command History 与属性事务

## 目标

建立可供 Inspector 和后续 gizmo 共用的编辑器撤销/重做边界，并立即让现有实体属性编辑可撤销：

1. 增加有界 `EditorCommandHistory`，支持 execute、push already-applied、undo、redo、clear；
2. 新命令执行后清空 redo 分支，容量满时只丢弃最旧的 undo entry；
3. 属性命令不持有 Entity/组件/属性裸指针，而是用 `EntityUuid + component id + property id` 在执行时重新解析；
4. Inspector 把一次 ImGui item 激活到 deactivated-after-edit 视为一个事务，而不是每帧值变化都入栈；
5. MenuBar 和全局快捷键使用同一 Undo/Redo 请求，文本输入仍可优先消费自己的快捷键；
6. New/Open/Edit/Play active Scene owner 切换时清空历史和未完成 Inspector 事务。

## 前后对比

改造前：

- PropertyEditorRegistry 直接把控件值写入组件；
- rotation 等字段只在当前编辑帧执行 `on_changed`；
- Edit 菜单显示 Undo/Redo，但点击没有行为；
- 系统不知道连续拖拽是一次操作还是多次操作；
- gizmo 如果直接接入，只能每帧写 Transform，无法自然撤销整个手势。

改造后：

```text
ImGui property item activated
  -> copy typed before value
  -> preview changes continue writing component
  -> item deactivated after edit
  -> copy typed after value
  -> push one already-applied EntityPropertyEditCommand

Undo/Redo
  -> active Scene
  -> find Entity by UUID
  -> find ComponentDescriptor by stable id
  -> find PropertyDescriptor by stable id
  -> typed assign + on_changed
```

因此用户拖动 Translation/Rotation/Scale 的一个完整手势只产生一个 entry；Bool、Float、Vec3 和 AssetHandle 的实体组件属性都复用同一路径。
实体 Name 当前仍是 Inspector 的特殊文本字段，尚未进入描述符，因此本步不把它伪装成 property command。

## Command History 语义

`EditorCommand` 只有 `undo()` 和 `redo()` 两个可失败操作。`EditorCommandHistory` 提供两种入栈方式：

- `execute(command)`：先 redo，成功后加入 undo；用于尚未应用的离散操作；
- `push_applied(command)`：当前状态已经由交互 preview 应用，只登记 before/after；用于 Inspector drag 和后续 gizmo drag。

两者都只在成功接收命令后清空 redo 分支。undo/redo 只有目标操作成功才移动 entry；目标失效时栈保持原状，Editor 记录一次 warning 并清空历史，
避免同一个 stale command 永久堵塞顶部。

默认容量为 256，容量至少为 1。超限只删除最旧 undo command，不回滚它已经形成的当前 Scene 状态；redo stack 来自 undo，不需要独立容量策略。

## 为什么使用 UUID 和描述符 ID

运行时 EntityId 只在当前 Scene 内有效，Entity wrapper 和组件地址会在删除、重建、New/Open、Play clone 后失效。命令长期保存这些引用会产生悬空
访问或把修改应用到错误 Scene。

`EntityPropertyEditCommand` 保存：

- active Scene getter；
-稳定 `EntityUuid`；
- `ComponentRegistry` 引用；
- component/property stable id；
-类型化 before/after 值。

执行时重新查找所有层级。Scene owner 切换仍主动 clear history，UUID 解析是单个 Scene 生命周期内对实体增删/registry relocation 的保护，不是允许
命令跨项目或跨 Play clone 漂移。

## 类型化 PropertyValue

`ComponentRegistry` 增加与当前受支持属性类型一致的 `PropertyValue`：Bool、Float、Vec3、AssetHandle。每个由
`make_property_descriptor()` 生成的 descriptor 同时具备：

- `copy_value(component)`：生成类型安全快照；
- `assign_value(component, value)`：验证 variant 类型、赋值并调用既有 `on_changed`。

这避免 Command 层按字节复制非平凡类型，也避免 Inspector 为 Transform、Camera 和 MeshRenderer 写多个专用分支。rotation 的 wrap 逻辑仍由 descriptor
定义，Undo/Redo 不复制业务规则。

`PropertyValue` 是组件描述系统的通用值边界，不包含 ImGui 类型；未来 gizmo 可直接为 Transform 属性构造 before/after，场景迁移、批量属性编辑和
脚本桥接也可复用同一 typed assign。

## Inspector 事务

Inspector 在调用现有 PropertyEditor 前复制当帧的值；如果 item 同帧激活，该值成为 before。之后组件继续实时更新，保持用户要求的实时预览；只有
`IsItemDeactivatedAfterEdit()` 成立时才复制 after 并回调 Editor。before/after 相同不会入栈。

活动事务只保存 UUID、stable ids 和 PropertyValue，不保存组件地址。active Scene 切换时 `reset_entity_edit()` 与 history clear 同时执行，防止尚未完成
的 UI 手势进入新 Scene。

## Menu 与快捷键

- Edit 菜单根据 `can_undo/can_redo` 启用或禁用条目；
- `Ctrl/Cmd+Z` 执行 Undo；
- `Ctrl/Cmd+Y` 和 `Ctrl/Cmd+Shift+Z` 执行 Redo；
- 使用 ImGui global low-priority shortcut route：聚焦文本框或活动控件注册相同 shortcut 时，由控件优先消费，不强抢文本编辑；
- Play 模式禁用编辑历史请求。

菜单和快捷键都只产生 `EditCommand`，实际 history owner 留在 Editor composition root；MenuBar 不持有命令对象。

## 自动化验证

- 多命令按栈顺序 execute/undo/redo；
- undo 后执行新命令清空 redo 分支；
- 有界容量删除最旧 entry；
- already-applied preview 入栈时不重复 redo；
- 失败命令不移动 history；
- EntityPropertyEditCommand 通过 UUID 和 descriptor ids 修改/恢复 Transform；
- 实体被删除后命令失败且栈位置不变；
- PropertyDescriptor typed copy/assign 拒绝错误 variant 类型；
- assign 会执行 rotation normalization；
- 完整 Debug 构建与 CTest。

Inspector 的真实 item 激活/释放、菜单 enabled 状态和各平台快捷键路由需要本地编辑器手工检查；history、解析和赋值核心已有纯测试。

## 下一步

建立通用 DebugDraw line submission 与渲染 executor：CPU list 不含 Vulkan 类型，GPU pipeline/buffer 按 frame slot 和 RenderPass generation 持有。
先用测试固定 AABB 的 12 条边与提交数据，再接入当前场景 pass；之后选中高亮和 gizmo 都消费这条路径。
