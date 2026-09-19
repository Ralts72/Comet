# 生产 Shader

## 目录与职责

| 目录 | 内容 |
| --- | --- |
| `material/` | 网格顶点入口与材质片元着色 |
| `common/mesh_vertex.glsl` | 共用网格输入、Frame/Object 布局与顶点变换 |
| `lighting/forward.glsl` | 前向光源布局、方向与衰减计算 |
| `debug/` | 调试线绘制 |
| `post/` | 全屏三角形与显示输出：曝光、色调映射、SDR/HDR 编码 |

## 材质与阶段配对

- `unlit_color`：`unlit_color.vert` + `unlit_color.frag`，直接输出颜色和强度。
- `unlit_texture_blend`：`unlit_texture_blend.vert` + `unlit_texture_blend.frag`，混合纹理，不计算光源。
- `lit_color`：`lambert.vert` + `lambert.frag`，使用 Lambert 漫反射与场景光源。
- 调试线与显示输出分别使用 `debug/line.vert/.frag`、`post/display.vert/.frag`。

`lit` 表示受光，`unlit` 表示不受光，和 HDR/SDR 输出模式无关。
材质模板名属于资产持久化协议；文件名描述当前算法，二者不要求同名。
三个网格入口包含同一份顶点实现；`COMET_MESH_LIGHTING` 只为受光版本启用世界位置、
逆转置法线计算和对应输出，不给不受光版本增加法线计算。
全屏顶点与调试线的输入协议不同，保持独立。

## 修改与验证

完整程序以同目录、同名 `.vert/.frag` 表示；新增程序需加入 `CMakeLists.txt` 显式配对列表。
当前生成文件使用阶段文件名，须保持全局唯一。
公共 `.glsl` 通过相对路径包含，构建依赖与编辑器热重载均跟踪实际 include。
编辑器只热重载材质的三个程序（六个阶段）；调试线与显示输出修改需重新构建。
`MaterialShaders` 按程序名持有顶点/片元字节码，允许提交任意完整程序对；
缺失单个阶段会拒绝整个候选批次，未提交的程序保留原版本，目标重建仍沿用成功发布的版本。
程序定义、默认字节码、固定契约校验和覆盖合并位于 `render/material/material_shader.h/.cpp`。
编辑器和 MaterialRenderer 共用这份程序定义；未知程序名或显式空程序同样被拒绝。
Frame 位于 set 0，材质位于 set 1，Object 使用 push constant；修改布局须同步 C++ 和契约测试。

运行 `cmake --build --preset dev-debug --parallel` 和 `ctest --preset dev-debug` 验证。
旧学习头文件与示例已移除，需要参考时可查 Git 历史。
