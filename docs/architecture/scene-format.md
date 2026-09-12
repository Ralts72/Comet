# 场景文件格式

Comet 的 `.scene` 文件使用 UTF-8 JSON。根节点必须包含 `version`；读取器会拒绝当前实现不支持的版本和未知字段，
不会静默丢弃数据。当前格式为 `"version": 2`，仍处于开发阶段，不代表已经冻结或承诺向后兼容。
只接受当前 FORMAT_VERSION；旧 YAML、缺失或不匹配的版本直接报错，不做兼容、迁移或旧格式备份。

```json
{
  "version": 2,
  "entities": [
    {
      "uuid": "550e8400-e29b-41d4-a716-446655440000",
      "components": {
        "name": "Root",
        "transform": {
          "translation": [0, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        }
      },
      "children": [
        {
          "uuid": "67e55044-10b1-426f-9247-bb680e5fe0c8",
          "components": {
            "name": "Camera",
            "transform": {
              "translation": [0, 2, 5],
              "rotation": [0, 0, 0],
              "scale": [1, 1, 1]
            },
            "camera": {"primary": true, "fov": 60, "near_clip": 0.1, "far_clip": 1000}
          }
        }
      ]
    }
  ]
}
```

`entities` 只包含根实体，子实体直接嵌套在父实体的 `children` 数组中；叶节点保存时省略 children，读取也接受空数组。
不再保存或接受 `parent` 字段。`uuid` 仍是全场景唯一的持久化身份，不能在不同子树里重复。
运行时 `EntityId`、EnTT handle、`RelationshipComponent` 中的运行时 ID 和派生的 `WorldTransformComponent` 矩阵都不会序列化。
根节点及每组兄弟节点分别按 UUID 排序输出，保证同一棵树的输出稳定；数组顺序不表示自定义的编辑器排列顺序。
根实体算第 1 层，读写最多支持 128 层（SceneSerializer::MAX_HIERARCHY_DEPTH），超出会报错而非截断。

当前内置 registry 支持 `name`、`transform`、`mesh_renderer` 和 `camera`。`name` 是 Scene 格式显式管理的必填字段，其余组件可选。
`mesh_renderer.mesh` 和 `mesh_renderer.material` 保存 `AssetHandle` 的无符号整数值；源文件路径和 GPU 对象不会落盘。
Handle 通过 uint64 解析，不经过浮点数；外部工具同样须保留完整 64 位整数（不能直接使用 JavaScript Number）。
字符串、布尔、数字严格区分，不接受将 `"42"` 当成数字；JSON 不支持注释、尾逗号或 NaN/Infinity。
`NameComponent` 可以编辑但不能通过通用 Entity API 添加或移除；`IdComponent`、`UuidComponent`、
`RelationshipComponent` 和 `WorldTransformComponent` 只允许只读访问，由 Scene 负责创建和维护。

`SceneSerializer` 与 Inspector 共享同一个 `ComponentRegistry`。除 `name` 外，组件键和属性键来自 descriptor 的
stable ID，值通过 descriptor 的类型访问器读写；只有 `serializable=true` 且非 transient 的属性会进入文件。
新增一个已支持属性类型的可序列化组件时，只需注册 descriptor，不需要再给 serializer 添加组件专用分支。

descriptor 描述当前代码所使用的活动格式。引擎开发阶段可以直接调整 stable ID、字段和类型，已有开发场景可能因此
无法加载，需要重新保存或重建；暂不为这些变化编写迁移器。等项目格式进入稳定阶段后，再冻结 schema，并要求不兼容
变更提升 `version`、提供显式迁移。加载通过属性访问器恢复原值，但不会触发只属于 Inspector 编辑流程的
`on_changed` 回调。

加载会拒绝格式错误或重复的 UUID、未知字段、非法 children/组件结构、过深层级和不支持的版本。
父节点由嵌套位置推导，不再从文件查找父 UUID；JSON 树天然无父子环，保存前仍校验运行时关系的完整性。
反序列化先完整解析和校验全部节点，再构建新的 Scene，因此失败不会让已有 Scene 处于部分修改状态。
保存会自动创建目标文件缺失的父目录；目录创建或文件写入
失败会返回包含目标路径的错误。
