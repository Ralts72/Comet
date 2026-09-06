# 005：Hierarchy 结构命令与架构回顾

## 背景与验收

阶段 4 的创建、删除、父子关系原本由 Hierarchy 在绘制时直接修改 Scene，绕过新建的历史。
只添加 CreateCommand 会保留两个破坏撤销链路的写入口，因此本项统一验收已有 Hierarchy 三种结构操作。
目标：一次操作一条历史，创建／删除恢复稳定 UUID；删除父实体可恢复子树；非法关系或恢复失败不留下部分结果。

## 前后对比

| 边界 | 之前 | 现在 |
| --- | --- | --- |
| Hierarchy | UI 遍历时直接改 Scene | 只提交 Create/Delete/Reparent 请求，Editor 在 UI 后执行 |
| 拖拽身份 | 进程内 EntityId | EntityUuid + 文档 generation，拒绝过期请求 |
| 删除 | 递归销毁，无历史 | 捕获子树完整值、父 UUID，再通过 EntityTreeCommand 删除 |
| 恢复 | 不支持 | 校验 UUID/外部父级，创建组件，再连接内部父子关系 |
| 未注册组件 | 不涉及撤销 | 描述符覆盖检查不通过就拒绝删除，不能静默丢数据 |
| 引擎实体销毁 | 遍历与删除交错 | 先收集整棵树，再逆序销毁，遍历分配失败不留下半棵树 |

## 代码级链路

`HierarchyPanel::render` 的 + / - 不再调用 Scene；它们记录 Request。
`accept_reparent_drop` 只验证当前场景与 generation、保存源和目标 UUID。
`take_request()` 通过 exchange 只消费一次，`set_scene()` 丢弃旧请求；Play 禁用结构按钮和拖拽，不禁用浏览／选择。

Editor 在所有面板绘制结束后消费请求，检查 Edit/generation，取消 Viewport 手势并完成 Inspector 属性事务。
接着调用 SceneCommands，成功创建时选中新实体，成功删除时清空选择；随后仍按原顺序更新相机和提取 Scene。
没有新增三个回调或全局事件总线。菜单 Undo/Redo 与结构操作共用 CommandHistory。

`scene_commands.cpp` 扩展原有命令模块，未新增生产代码文件：

- 私有 EntitySnapshot 保存 uuid、parent uuid、原始名称、描述符 ID 与完整组件 std::any。
  不保存组件地址、EntityId 或 WorldTransform 缓存；名字为空、组件缺少 Transform 也按原样保存。
- capture_tree 先用 ComponentRegistry::covers_entity 检查 EnTT 存储类型是否都被描述符或 Scene 元数据覆盖。
  再检查每个普通组件有可用捕获／恢复协议；不能安全恢复就拒绝操作。
- restore_tree 先检查所有 UUID 无冲突、外部父级存在、描述符可恢复。
  创建所有实体、恢复组件后再连接关系；任一步失败，销毁本次新建实体，保留原场景及历史游标。
- EntityTreeCommand 同时服务创建和删除，方向相反。恢复仍用原 UUID，但由 Scene 分配新的 EntityId。
  旧属性命令继续按 UUID 找到恢复的实体；不强行复用已释放的 EnTT 句柄。
- ReparentCommand 保存前后父 UUID，通过 Scene 原有环检测设父或解除父级。
  本地 Transform 保持不变，世界位置可能改变；这是沿用已有行为，不暗中改变操作语义。

恢复或重复删除前还检查子树 UUID 集合，历史之外新挂入的子节点不能被顺带删除。
Scene 创建默认组件异常时销毁未完成实体；销毁子树先收集再删除，减少部分修改风险。

## 共享库边界修复

实体创建测试暴露 macOS 的隐藏类型信息问题：在 editor/test 构造的 Transform std::any，
不能被 engine 中的恢复回调正确识别。内置组件原本未导出，而 engine 使用 `-fvisibility=hidden`。
为公开组件类型添加 COMET_API 后恢复通过，新增“共享库外构造快照、引擎内恢复”的回归测试。
这是 C++ 类型边界修复，不是给 ImGui 增加特例；未修改第三方 std::any/EnTT 源码。

## 第一次阶段性架构回顾（001—005）

| 维度 | 检查与处理 |
| --- | --- |
| 目录 | 编辑历史、场景命令和 Gizmo 留在 editor/src；面板在 panels；组件能力在 engine/src/scene。此次扩展已有 scene_commands，不按命令数量拆文件。 |
| 职责 | CommandHistory 只管历史顺序与文档身份；事务管属性手势；SceneCommands 管结构；描述符管类型操作；Scene 管实体不变量。 |
| 依赖 | engine 不认识编辑器或 ImGui。covers_entity 在引擎内部只读访问 EnTT，不向编辑器暴露 registry 或原始 handle。 |
| 冗余 | 名称已共用 PropertyValue；创建／删除共用子树快照；父级复用 Scene 环检测。没有 NameManager、SnapshotStore、每种组件专用命令或全局 EventBus。 |
| 生命周期 | 注册表早于历史构造、晚于历史析构；退出时先取消手势、解绑清空历史，再销毁面板／Scene owner，补齐 Viewport/Menu 显式清理。 |
| 运行时资源 | 历史仅持有 CPU 值和 AssetHandle；GPU retention、FrameSlot、上传队列完全不变。 |

Inspector 的组件操作仍在本面板完成属性遍历后立即执行，因为不删除实体或修改树结构；
Hierarchy 会使其他面板实体失效，故必须在 Editor 的 UI 尾部执行。
后续若出现跨面板复合编辑，再统一请求载荷；目前不为形式一致新增一个通用分发管理器。

## 测试结果

- 结构测试覆盖创建→改名→删除→连续 Undo/Redo、子树外部父级、资产引用、空名称、无 Transform、保存加载。
- 验证 UUID 冲突、父级丢失、未知组件拒绝、恢复中途失败回滚、历史外子节点保护、环和空操作不破坏 redo。
- 真实 ImGui 帧验证 + / - 只产生一次请求、UI 期间不改 Scene、Play 禁用及换场景清除请求。
- 新增跨共享库组件类型恢复测试；既有 Scene 的环检测、子树删除和元数据访问约束回归保留。
- Debug/Release 完整构建、各 362 项测试通过；新增 9 项回归，退出清理的最终修改已复验。
  验证使用 `cmake --build --preset dev-debug --parallel 6`、`ctest --preset dev-debug --output-on-failure --timeout 120`，
  Release 使用独立构建目录；修改的 C++ 文件通过 clang-format dry-run，`git diff --check` 通过。
- 003 Linux CI run 34048158962 已完成：344 tests 通过，证明有界 CI 基线可运行。
  005 自己的 Linux 构建需本提交推送后再验证，不能沿用旧运行声称已通过。
- 004 Linux CI run 34048392036 已成功完成，覆盖组件结构操作；过期的无界并发 002 CI 由新运行覆盖。

## 限制与后续

Undo 恢复实体但不恢复旧选中态／树展开态；选择仍属于编辑器 UI，不写入 Scene。
历史只限制条数，单个巨大子树仍占较多内存，已列入后续测量；灾难性持续 OOM 不承诺可继续运行。
非值语义组件必须显式提供恢复协议；运行时脚本／外部资源实例不能依靠浅复制获得生命周期正确性。
组件 type_id 仅用于进程内覆盖检查，持久化仍用既有稳定字符串 ID，未改 .scene Schema。
不引入多选或跨场景剪贴板。下一项复用快照实现 duplicate 的新 UUID 映射，而不是再次复制序列化器。
