# 004：可撤销的组件增删

## 背景与验收

阶段 4 的属性事务只能修改已存在的组件；结构变化不能伪装成某个 PropertyValue。
本项提供 Inspector 添加／移除 Camera、Mesh Renderer 的入口，并让结构命令与属性事务顺序共享 Undo/Redo。
名称由 Scene 管理，Transform 作为编辑器基础空间信息保留；Play 暂不开放结构编辑。

## 前后对比

| 职责 | 之前 | 现在 |
| --- | --- | --- |
| Inspector | 只显示已有组件 | Add Component 菜单；右键组件标题 Remove Component |
| 撤销 | 单个属性前后值 | 结构命令记录“存在／不存在”及完整组件值 |
| 组件描述符 | 属性访问、默认添加和移除 | 增加类型擦除的完整值捕获／恢复能力 |
| 持有数据 | 属性事务有 PropertyValue | 结构快照 std::any 拥有原始组件值；不保存 EnTT 指针 |

## 代码级链路

1. `InspectorPanel::render_entity` 遍历注册表构建菜单，只暴露具备完整增删／快照能力的组件。
   当前 Scene 必须与 CommandHistory 所绑定的实体一致，Play 或其他文档的实体不能发结构命令。
2. 面板仅记下用户本帧选中的描述符，完成所有属性访问后，先 `m_property_edit.commit()`，再执行结构命令。
   因而不会把正在使用的组件移除，也不会把最后一次属性修改丢在历史之外。
3. `scene_commands.h/.cpp` 集中提供场景结构操作入口，不新增 Manager 实例。
   私有 ComponentCommand 保存 registry 引用、Entity UUID、组件稳定 ID、操作方向和自有快照。
4. 第一次 Add 创建默认组件并捕获值；Remove 先捕获再删除。Undo/Redo 重新解析 UUID，不缓存组件地址。
   恢复通过 `ComponentDescriptor::restore_component` 先检查目标不存在、快照类型正确，再构造组件。
5. `make_component_descriptor<T>` 为可复制的普通值组件生成捕获／恢复回调；Scene 管理的元数据没有此能力。
   快照包含未注册、只读或未显示的字段，不拿“可编辑字段集合”代替“组件完整状态”。

例如：Add Camera → FOV 45 改 65 → Remove Camera，撤销顺序是恢复 FOV=65 的 Camera、
恢复 FOV=45、移除刚添加的 Camera；重做反向执行。现有属性命令无须认识结构快照。

## 设计理由与架构价值

ComponentDescriptor 管类型操作，SceneCommands 管编辑行为，CommandHistory 管顺序与场景隔离，Inspector 管界面。
不把 ImGui 放到 engine，不让 engine 依赖 Undo，也不为 Camera/Mesh 各建一套命令类型或工厂类。
捕获完整值保留 AssetHandle 和隐藏数据；与文件序列化分离，撤销不用 YAML 往返，也不替换整个 Scene。
Transform 的不可移除是编辑器策略，未删除底层序列化器恢复无 Transform 场景的能力。

## 测试结果

- 新增结构历史测试：Add/Edit/Remove 顺序、资产引用与保存恢复、失败不移动游标、不丢 redo、UUID 重建与场景隔离。
- 验证隐藏字段完整复制、捕获异常时回滚刚添加的组件、错误类型快照不会留下半个组件。
- 真实 ImGui 帧驱动 Add 菜单、右键 Remove 菜单和 Play 禁用行为。
- Debug/Release 完整构建、各 353 项测试通过；本项共新增 9 个测试。
  验证命令为 `cmake --build --preset dev-debug --parallel 6`、`ctest --preset dev-debug --output-on-failure --timeout 120`，
  Release 使用独立构建目录；修改的 C++ 文件通过 clang-format dry-run，`git diff --check` 通过。
- CI 有界构建修复已推送；Linux 当前结果以远端运行记录为准，不能将本机 macOS 结果冒充跨平台通过。

## 限制与后续

自动快照面向当前纯值组件。将来含外部句柄、脚本实例或不可复制资源的组件必须定义自己的恢复协议，
或清空捕获／恢复能力以禁止通用结构编辑；不能把可复制 shared_ptr 误认为可恢复运行时生命周期。
回调返回 false 必须不留下部分改动；分配异常沿原异常路径传播，首次 Add 捕获异常会先回滚组件。
历史与描述符注册表同属 Editor 生命周期，注册表应保持稳定，不在命令仍存活时更换组件类型定义。
本项不增加 EventBus，不回调重播属性 on_changed；恢复的是已经归一化的组件值。
下一项是实体创建及其撤销，随后覆盖删除／复制／层级；005 同时执行阶段性架构回顾。
