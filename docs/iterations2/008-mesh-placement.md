# 008：Mesh 拖入 Viewport

## 背景与验收边界

007 已允许非示例模型生成 Mesh Artifact，但仍需手工创建实体、添加 MeshRenderer 和填写引用。
本项让 Project 中的 Mesh 拖入 Edit Viewport，一次创建带位置和资产引用的实体，并可整体撤销、重做、保存。
不把材质／纹理引用编辑、Prefab 或文件导入对话框混入同一次验收。

## 代码前后对比

| 位置 | 之前 | 现在 |
| --- | --- | --- |
| `panels/project.cpp` | Selectable 只选择资产 | Mesh 行提供 ImGui drag source，携带 Handle 与文档 generation |
| `asset_drag_drop.h` | 无跨面板资产协议 | editor-only 的值载荷；不保存 AssetRecord／Entity／GPU 指针 |
| `panels/view.cpp` | Image 只处理相机、Gizmo 和拾取 | 图像区域接收 Mesh 交付，暂存位置请求；拖拽期间不导航或拾取 |
| `camera_controller.cpp` | 仅导航和聚焦 | `camera_focus_plane_point()` 将图像 uv 转为相机关注平面的世界坐标 |
| `editor.cpp` | 没有 Mesh 放置入口 | UI 尾部验证文档／资产类型，加载已发布资源，再执行场景命令 |
| `scene_commands.cpp` | 创建普通实体 | 提取公共快照创建逻辑；`create_mesh_entity()` 在同一快照中放入 Transform、MeshRenderer |

## 实际调用链

1. Project 通过 `AssetDragPayload{handle, history.generation()}` 开始拖拽。
   `ImGuiCond_Once` 固定开始时的 generation，不能在 New/Open 后把旧拖拽伪装成新文档请求。
   行 ID 包含 Handle，不依赖容易重名的文件名。
2. Viewport 在图像 item 上接收正确类型、正确大小的 payload；Play、图像外及错误载荷不接受。
   `map_viewport_point_to_pixel()` 继续负责可见区域边界；位置采用完整图像 rect 的 uv，不能误用整个窗口。
3. 关注平面经过 `camera.target`、平行于画面。透视高度为 `2 * depth * tan(fov / 2)`，
   正交高度直接取 `orthographic.height`；逆 view 将平面偏移变为世界方向。
   左上角 uv 的 Y 向下，世界平面 Y 向上；非法相机、裁剪范围或坐标返回空结果。
4. 面板不修改 Scene。Editor 在所有面板渲染后取走请求，检查 Edit 和 generation，结束未完成的属性编辑。
   查询数据库类型后调用 `load_mesh()`／`load_material()`；资源未就绪则记录 Log，不创建半成品实体。
5. `SceneCommands::create_mesh_entity()` 只接收值，不依赖 ImGui、AssetManager 或 Vulkan。
   与普通创建复用 `EntityTreeCommand`，单条历史同时管理名称、Transform 和 MeshRenderer。
   Undo 删除该实体；Redo 恢复同一 UUID、位置、Mesh／Material Handle；SceneSerializer 已覆盖这些字段。

## 设计理由与架构价值

- UI 输入、资产加载、Scene 命令仍分别属于面板、应用编排与编辑命令层，不为一个拖拽引入全局 EventBus。
- 跨帧数据只保存稳定身份；文档 generation 防止旧 UI 请求写入新 Scene。
- 创建快照复用已有失败回滚和 UUID 机制，不先“创建空实体”再追加多个历史命令。
- 放置材质在启动时从项目 demo 材质取得 Handle，并保存为 `m_placement_material`。
  同次运行中移动／重命名材质不破坏引用；没有创建引擎专用的隐式材质类。
- `asset_drag_drop.h` 是跨面板通信协议，不是资源 owner；后续引用拖拽可复用，未放入 engine。

## 测试结果

- Debug 和 Release 完整构建成功；各 **377 tests** 通过。
- 新增 6 tests：完整实体撤销／重做／序列化、无效创建不破坏 Scene 和 redo、两种投影及旋转后的关注平面、
  非法输入、ImGui 拖放交付只产生一次值请求、Play／图像外／错误 payload 拒绝。
- 首轮 UI 测试曾把期望位置固定为精确 uv；ImGui 将鼠标坐标取整，产生亚像素差异。
  测试改用实际 IO 鼠标位置求期望，没有放宽产品算法或随意增大容差。
- `git diff --check` 和变更 C++ 的 clang-format 检查通过；完整图形测试日志未出现 VUID。
- UI 验证使用真实 ImGui 帧和输入事件，但没有进行桌面端人工拖拽验收；本项 Linux CI 由推送触发，尚不提前声称通过。
  前序 006／007 Linux CI 分别为 34048968491／34049335440，均已成功。

## 限制与后续方向

- 放置是相机关注平面，不是模型表面射线吸附、地面碰撞或放置预览。
- 首次 GPU 创建仍在 owner 线程。只读已发布 Artifact／已驻留资源，绝不隐式解析 glTF。
  已驻留或旧 Artifact 可继续使用；想应用源文件变化需完成 Reimport。后续上传预算与资源调度独立推进。
- 默认材质来自示例项目，不是通用项目设置；重启时的示例启动资产路径约定保持原样。
- 当前只支持 Mesh → Viewport；下一项处理资产引用拖拽，不把本项宣称为所有资产编辑完成。
- README 与路线图已同步实际用法；下一次定期架构审查仍为 010。
