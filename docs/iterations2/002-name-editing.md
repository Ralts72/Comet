# 002：名称纳入属性事务

## 背景与验收

路线图阶段 4 要求扩大撤销覆盖。名称原本由 Inspector 独立输入框直接写入 NameComponent，
绕过描述符和 CommandHistory，且 256 字节缓冲会截断长名称。
本项验收是：名称实时预览，结束输入只记一条撤销，Escape 恢复；Play 修改不进入 Edit 历史；保存格式不变。

## 前后对比

| 边界 | 之前 | 现在 |
| --- | --- | --- |
| Inspector | 固定 char 数组 → 直接赋值 | String 控件 → 通用 render_property → PropertyEditTransaction |
| 历史 | 名称不支持 | 与 Transform/Gizmo 共用同一历史，UUID + name.name 定位 |
| 属性值 | 只有 bool/float/Vec3/AssetHandle | 加入拥有内容的 std::string |
| 序列化 | 私有重复 PropertyValue 列表 | 复用公共 PropertyValue，字符串拥有完整读写路径 |
| Name 组件 | 不在描述符注册表 | 有属性描述，但不给外部添加／移除权限 |

## 代码与设计理由

`component_registry.h/.cpp` 添加 PropertyType::String，以及对应的复制、类型检查和赋值。
`make_component_descriptor` 对 Scene 管理的组件不生成增删回调，注册表允许这种仅可编辑字段的描述符。
因此 NameComponent 可编辑名称，不意味着用户获得删除实体必需元数据的能力。

内置 name 描述符标记 component.serializable=false，因为现有 .scene 已专门保存 `components.name` 字符串。
这表示不走通用组件映射重复写入，不表示名称不保存；现有格式与读取路径保持不变。
普通自定义 String 属性则通过通用序列化分支保存，测试覆盖 UTF-8、引号、换行和长字符串。

`property_editor_registry.cpp` 的 String 控件使用 std::string 与 ImGui CallbackResize，动态调整拥有内容的输入缓冲。
回调仅在 InputText 调用期间访问本次字符串值，不保留组件指针，不修改第三方源码，也不新增文本缓冲服务。

Inspector 仍把 Name 显示在顶部，只保留这项布局特例；赋值、输入开始／结束、取消和历史全部调用既有 render_property。
没有 RenameCommand、NameEditTransaction 或第二份撤销栈。文本编辑时的 ImGui 内部 Undo 仍是文本框自己的行为，
结束输入后才由场景级 Undo 撤销整次名称编辑。

## 架构价值

名称不再是绕过公共编辑协议的特例，String 后续也能服务脚本暴露字段等真实属性。
消除序列化器重复的类型清单，避免扩展属性类型时编辑器已支持而保存器还停留在旧集合。
“可编辑字段”和“可增删组件”分开，给接下来的组件结构事务保留明确能力边界。

## 测试结果

- 新增 Name 描述符不能增删、类型错误拒绝且旧值保留测试。
- 新增名称事务、长名称、保存加载、Undo/Redo、取消保留 redo 分支测试。
- 真实 ImGui 帧验证 UTF-8 长文本不截断、Enter 提交一次、Escape 恢复和 Play 不记录。
- 扩展通用描述符序列化测试，验证 String 并保留原来的 serializable/transient 约束。
- Debug/Release：完整构建及各 344 项测试通过。

验证命令：`cmake --build --preset dev-debug --parallel 6`、
`ctest --preset dev-debug --output-on-failure --timeout 120`，Release 使用既有独立构建目录复验。

## 限制与后续

名称编辑不引入唯一命名或空名称限制；层级仍用 UUID/EntityId，名称不承担身份职责。
本次不做 Scene Schema 升级，components.name 的专门持久化路径留到真正迁移项目格式时处理。
下一项是可选组件增删及其撤销，不能简单把整个组件当作一个 PropertyValue。
