# 001：平移 Gizmo 与连续迭代基线

基于 main 的 `6728ee7` 创建 feat/auto2，将已验证的平移 Gizmo 作为第一个独立验收项。
本系列文档与对应代码共同提交，不直接移植旧分支的命令和编辑器编排。

## 1. 实际效果与范围

之前：Viewport 左键只能选择实体，位置需要在 Inspector 中修改。

现在：Edit 中选中实体后出现世界 X／Y／Z 轴箭头，左键拖动箭头实时修改本地 `TransformComponent::translation`。
释放鼠标只产生一条撤销记录，Escape 恢复拖动前的位置。相机导航、拾取和 Gizmo 不会同时响应同一次按下。

本次只实现平移，不包含旋转、缩放、本地轴、吸附或多选。无 Mesh 的实体也能平移；包围盒仍只服务有可用 Mesh 的实体。
正对视线、投影长度过短的轴不显示，也不能拖动；例如默认正视相机下 Z 轴不可用，环绕观察后可以操作。

## 2. 文件和职责

| 文件 | 本次职责 |
| --- | --- |
| `editor/src/translation_gizmo.h/.cpp` | 投影、轴命中、拖动计算、事务生命周期；不依赖 ImGui/Vulkan |
| `editor/src/panels/view.h/.cpp` | 采样 ImGui 输入、输入优先级、绘制屏幕箭头、结束 UI 捕获 |
| `editor/editor.cpp` | 组合共享历史与两个事务来源，协调菜单／模式／相机和绘制时点 |
| `tests/editor/test_translation_gizmo.cpp` | 纯逻辑的坐标、父子变换、历史和取消边界 |
| `tests/editor/test_viewport_gizmo_ui.cpp` | 使用真实 ImGui 帧验证按下、拖动、释放和失焦等交互 |

没有修改 engine 的渲染接口、Scene 数据结构或 Shader，也没有新增事件总线。
CI 的 push 分支新增 feat/auto2，沿用现有 Linux 构建与测试流程；不创建 PR，也不修改 main。

## 3. 为什么独立一个 TranslationGizmo

它承担一项完整的编辑工具职责，不是为了包装几个成员变量：

```text
TranslationGizmo
├── handles()：当前实体／相机／布局 → 可绘制的屏幕轴
├── update()：输入 → 命中／拖动／提交或取消
└── PropertyEditTransaction：复用现有组件写入与撤销协议
```

`Axis`、`Handle`、`Input` 都是它的公开嵌套类型；`Context`、`Drag` 是私有实现状态，不再拆成多个独立头文件。
这是编辑器专用工具，放在 editor，不放进 engine。未来旋转／缩放可以沿用输入与事务原则，但本次不预建通用 Gizmo 框架。

## 4. 事务：复用类型，不共享活动状态

Editor 的组合关系是：

```text
CommandHistory
├── Inspector 的 PropertyEditTransaction
└── TranslationGizmo 内部的 PropertyEditTransaction
```

Inspector 在隐藏、折叠或属性控件失活时，会提交自己的事务。
若两个入口共享同一个事务实例，折叠 Inspector 就可能提前提交尚未结束的 Gizmo 拖动。
因此两者共享历史和组件注册表，分别拥有手势状态。

正常左键开始视口操作前，ViewPanel 先完成 Inspector 的事务，再尝试 Gizmo 命中。
Gizmo 实际写入仍使用既有目标描述：

```cpp
m_edit.begin({selected, "transform", "translation"});
m_edit.preview(translation);
m_edit.commit();  // 释放鼠标时
m_edit.cancel();  // 取消时恢复 before
```

上面是互斥阶段，不是每帧依次调用四个函数。`begin` 保存 before，多个 `preview` 只更新组件，
`commit` 比较最终值后记录一个已生效命令；没有变化就不入栈。
没有新增专用 TranslationCommand，也没有直接写组件后再手工拼 before／after 的第二套逻辑。

## 5. 世界轴如何修改本地 translation

`make_context()` 从历史当前绑定的 Scene 解析选中 UUID，读取实体世界原点、父节点世界矩阵以及本地 translation。
开始拖动时固定这些参考值，不保存可失效的组件地址。

`axis_parameter()` 将鼠标反投影成 near／far 射线，计算射线与指定世界轴最近处的轴参数。
拖动过程使用当前参数与起始参数的差：

```cpp
world_delta = axis_direction * (current_parameter - start_parameter);
local_delta = world_to_parent * Vec4(world_delta, 0.0f);
translation = original_local_translation + Vec3(local_delta);
```

`w = 0` 表示变换的是位移，不把父节点自身的平移再加进去。
因此父节点有旋转或非均匀缩放时，沿世界 X 拖动仍然沿世界 X 移动，实际写入的本地分量可能不是 X。
每帧从拖动起点计算，不累计逐帧增量，避免误差累积；父矩阵不可逆时不开始操作。

## 6. 屏幕坐标和箭头绘制

投影纵横比来自实际 `image_resolution`，不是尚未生效的 resize 请求。
NDC 通过 `image_display_rect` 映射为 ImGui 逻辑屏幕点，Y 方向与引擎的负高度 Vulkan Viewport 对应：

```cpp
normalized = Vec2(ndc.x + 1.0f, 1.0f - ndc.y) * 0.5f;
screen = display_rect.min + normalized * display_rect.size();
```

轴的世界长度由深度／正交高度换算，基准为约 90 个逻辑点；斜视轴会透视缩短。
命中半径使用 7 个逻辑点，不随着 Retina 物理像素数翻倍。
可见裁切区用于开始命中；开始后允许鼠标拖出图像，仍然接收释放。
物体自身跨过 near／far 裁剪面只隐藏箭头，不取消已经开始的拖动。

箭头由 ViewPanel 使用 ImGui draw list 绘制，是可操作的 UI 覆盖层，不做场景深度测试。
原因是普通深度线可能完全藏在模型内部，而屏幕命中仍然成功，出现“看不见却能拖动”的区域。
选中包围盒继续使用 `LineDrawList → DebugRenderer`，保留真实深度遮挡；两者职责不同。

## 7. 同一帧内的编排变化

之前：

```text
ImGuiContext::update_frame
  → UI callback 生成面板
  → ImGui::Render 固化 UI 数据
→ Editor 处理菜单、相机、RenderView
→ SceneExtractor → 场景绘制
```

现在：

```text
ImGuiContext::update_frame
  → UI callback
      → 面板输入，Gizmo 预览组件
      → 菜单／模式请求、相机输入、F 聚焦、RenderView
      → ViewPanel::draw_gizmo 使用最新状态追加箭头
  → ImGui::Render
→ 提交选中包围盒／拾取请求
→ SceneExtractor → 场景绘制
```

把 Editor 原有的命令／相机处理移到 UI callback 尾部，是为了在 `ImGui::Render` 前画出当前相机与实体对应的箭头，
不是引入新的渲染生命周期。修改依然发生在 `Renderer::prepare_frame` 内，先于场景提取。
ViewPanel 仅在这段 UI 帧内借用窗口 draw list，绘制后清空指针，不跨帧或跨线程交付它。

点击场景拾取需要稍后由 Renderer 返回结果，因此该点击帧暂不画旧选择的箭头，新箭头在下一 UI 帧出现。
原有选中包围盒仍在拾取回调中当帧提交新选择，未改成延迟一帧。

## 8. 输入优先级与取消边界

- Alt/Option＋左键、右键、中键仍走相机导航，不开始 Gizmo。
- 普通左键先测试 Gizmo；命中则消费，未命中才发出原来的场景拾取请求。
- Gizmo 拖动占有 ImGui active ID，期间不响应相机输入、F 聚焦、投影切换或历史快捷键。
- 释放提交；Escape、应用／视口失焦、隐藏、Play 或菜单场景操作会结束捕获并取消未完成拖动。
- UUID、历史 generation、父关系／矩阵、相机或布局发生变化时取消，不能把旧拖动坐标继续套在新上下文上。
- 目标删除或换 Scene 后只清理失效状态，不向新 Scene 写入旧值。

同时修正了滚轮所有权：`ImGui::Image` 的 item ID 为 0，原 `SetItemKeyOwner` 会直接返回。
现在使用显式 owner ID，悬停图像或正在拖动 Gizmo 时，滚轮不会同时触发窗口滚动。

## 9. 验证方法

自动化包含纯几何／事务测试及不依赖桌面后端的 ImGui 输入测试。
全量回归另外覆盖既有的 Vulkan 线段绘制、选中框和帧准备顺序。

本次新增 18 项纯逻辑、7 项 ImGui 交互测试；最终 Debug、Release 均通过全部 340 项测试。
已通过根目录 `.clang-format` 检查和 `git diff --check`，没有格式化 Shader 或第三方源码。

```sh
cmake --build --preset dev-debug --parallel 6
ctest --preset dev-debug --output-on-failure --timeout 120
cmake --build /tmp/comet-audit-release.lfw8ZZ --parallel 6
ctest --test-dir /tmp/comet-audit-release.lfw8ZZ --output-on-failure --timeout 120
```

全量测试需要可访问 macOS 图形会话的环境；受限环境下的首次运行停在 GLFW 初始化，已终止并重新验证。

手动检查：选中 Editor Cube → 拖红／绿箭头 → 释放后撤销／重做 → 拖动中按 Escape；
再切 2D、环绕到斜视操作蓝轴，并检查 Alt/Option 相机操作不移动实体。
本次没有实现旋转／缩放，也不承诺轴与视线平行时仍能用该轴拖动。
