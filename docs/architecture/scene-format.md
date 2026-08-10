# 场景文件格式

Comet 的 `.scene` 文件使用 UTF-8 YAML。根节点必须包含 `version`；读取器会拒绝不支持的版本和 v1 未知字段，
不会静默丢弃数据。

```yaml
version: 1
entities:
  - uuid: 550e8400-e29b-41d4-a716-446655440000
    components:
      name: Root
      transform:
        translation: [0, 0, 0]
        rotation: [0, 0, 0]
        scale: [1, 1, 1]
  - uuid: 67e55044-10b1-426f-9247-bb680e5fe0c8
    parent: 550e8400-e29b-41d4-a716-446655440000
    components:
      name: Camera
      transform:
        translation: [0, 2, 5]
        rotation: [0, 0, 0]
        scale: [1, 1, 1]
      camera:
        primary: true
        fov: 60
        near_clip: 0.1
        far_clip: 1000
```

`uuid` 是实体的持久化身份。`parent` 引用另一个实体 UUID，根实体省略该字段。运行时 `EntityId`、EnTT handle、
`RelationshipComponent` 中的运行时 ID 和派生的 `WorldTransformComponent` 矩阵都不会序列化。实体按 UUID 排序输出，
保证内容稳定且便于 diff。

v1 支持 `name`、`transform`、`mesh_renderer` 和 `camera`。`name` 必填，其余组件可选。
`mesh_renderer.mesh` 和 `mesh_renderer.material` 保存 `AssetHandle` 的无符号整数值；源文件路径和 GPU 对象不会落盘。

加载会拒绝格式错误或重复的 UUID、缺失父节点、父子环、未知字段、非法组件结构和不支持的版本。反序列化始终构建
新的 Scene，因此失败不会让已有 Scene 处于部分修改状态。
