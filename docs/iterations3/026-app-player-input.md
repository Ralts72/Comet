# 026 App 玩家改键入口与共享 UI 后端

## 背景与前后变化

025 的个人配置、运行中重绑定和草稿面板已经可用，但只有 Editor Play 能打开设置。
App 只能在启动时读取文件；直接复用原 Editor 后端又会清空已经画好的游戏图像。

现在 App 右上角 `Input` 打开同一玩家面板；宿主仍负责文件与 Runtime，不把 ImGui 放入 Engine。
Editor 的字体、布局、docking 和 Clear 呈现保持原意，App 用内建字体、无布局文件的 Preserve 呈现。

```text
App / Editor 宿主
 ├─ PlayerInputPanel：稀疏草稿 → 待应用候选
 ├─ PlayerInputSettings：读取与原子保存个人文件
 ├─ Engine：请求下一输入更新边界重绑定
 └─ comet_ui / ImGuiContext：呈现与交换链资源
                              ↓
                     Engine Graphics + ImGui 后端
```

## 代码与调用链

### 共享的是实际后端，不是 Editor 工作流

`editor/src/ui/imgui_context.*` 移到 `ui/src/`，命名空间改为 `CometUi`。
App 与 Editor 都链接 `comet_ui`；Engine 不反向链接 UI，App 不链接 `editor_ui`。
模块边界检查同时阻止共享 UI 包含 Editor、Project 或编辑状态头。

```cpp
struct Options {
    std::filesystem::path ini_path;
    std::filesystem::path font_directory;
    bool docking = false;
    Composition composition = Composition::Preserve;
};
```

路径和模式由宿主传入，不在后端猜测运行的是哪个程序。原纹理绑定、slot 保活及后端重建继续复用，
没有平行的 App ImGuiContext，也没有增加全局 UI Manager。

### 场景之后合成，不重新清屏

原通道固定 Clear；现在只有 Preserve 使用已有 Present 图像：

```cpp
if(m_options.composition == Composition::Preserve) {
    color_attachment.description.load_op = Comet::AttachmentLoadOp::Load;
    color_attachment.description.initial_layout = Comet::ImageLayout::PresentSrcKHR;
}
```

空 UI 不开启 pass。非空 UI 的 Load 必须等待同帧前一个颜色写入；`RenderPass` 为颜色 Load 补充
ColorAttachmentOutput 的 write → read/write 依赖，acquire 等待不能代替这个依赖。

线性 HDR 交换链使用 `ui/shaders/imgui_hdr.frag`：把字体／纯色的 sRGB RGB 转成线性值后合成，
alpha 不做 gamma 解码，背景中超过 1 的值不截断。SDR 保留原后端片元阶段。
此 shader 不支持任意纹理色彩空间推断，也不代表 Editor HDR 或 HDR10/PQ 已完成。

### App 生命周期与输入授权

App 初始化后端后安装原 Renderer overlay 的 render/release/rebuild 接口；退出先解除 overlay，再销毁 UI。
重开场景关闭旧草稿、丢弃文件编辑基线并重新读取玩家配置，不把旧弹窗带入新运行域。
加载失败显示独立错误弹窗；应用失败保留草稿，不用失败候选覆盖正在运行的映射。

`on_update` 必须为 acquire 延期的帧准备输入；`on_frame_ready` 才知道当前 UI 是否占用输入。
因此先保存尚未消费的 Gate 状态，ready 时从同一个起点重算最终授权：

```cpp
// on_update：没有 UI 回调的延期帧也有正确回退。
m_input_before_ui = m_input_gate;
frame.runtime_input = m_input_gate.read(physical, keyboard_allowed, pointer_allowed);

// on_frame_ready：Runtime 尚未读取 fallback。
m_input_gate = m_input_before_ui;
// 构造当帧 UI 后，按实际弹窗与鼠标占用重新计算一次。
frame.runtime_input = m_input_gate.read(physical, !menu_blocked, !pointer_blocked);
```

不修改 Gate 对同一物理序号去重的契约。弹窗及关闭帧阻断游戏；Esc 先取消录键，再关闭面板，
不穿透为退出。菜单不自动暂停 System，输入阻断与模拟时间是两个独立选择。

## 测试与验证边界

初次 GPU 回归发现新增像素测试的通道和旧 framebuffer 依赖不兼容；修正测试为同一 image view
创建匹配的 framebuffer／后端管线后通过，没有关闭 Vulkan 验证或减少像素断言。

- 新增 Gate 快照测试验证未消费回退可重算，且键盘、鼠标和手柄授权不互相吞掉边沿。
- 生产 scene → UI 路径覆盖空／非空 UI、窗口 resize、后端重建及移除 overlay 后继续渲染。
- 离屏数值回读验证空 UI 不改图、窗口外像素不变、透明覆盖；实际 HDR shader 验证 RGB 解码、alpha 与 HDR 背景保留。
- 完整 Debug、Release App 构建通过，无编译警告。完整 10 个 CTest 入口通过：1052 CPU、197 UI、
  159 常规 GPU、43 同步验证 GPU、14 WSI 恢复、2 后处理恢复；CPU 与 GPU 各有 1 项既有条件跳过。
  Shader 构建契约、模块边界、跨进程管线缓存与渲染基准冒烟均通过，日志前缀 `/tmp/comet-auto3-026-`。
- 桌面短冒烟确认 App 的 Input 入口能打开，Esc 关闭设置但不同时退出，再次 Esc 正常退出；未修改个人文件。
  桌面录键未充分确认，保存与映射以可重复测试为证，不宣称完整人工体验验收。

交换链不带 CopySrc，因此像素测试使用匹配的离屏目标和真实后端，不能冒充生产 loadOp 的直接回读。
实际 HDR surface 切换、真实图像数 3→2 尚未覆盖；重建分支测试只强制 image_count_changed。
同步验证层提示原有 `VK_LAYER_ENABLES` 环境选项已弃用；验证仍启用，未为通过测试屏蔽警告。

## 架构价值与下一步

这次跨模块检查确认依赖方向为 App／Editor → comet_ui → Engine／ImGui；运行域输入仍不读文件、
不含 UI。原 FrameScheduler、WSI 重建及 Runtime 输入更新边界未复制。
App 和 Editor 的文件应用编排仍各自很短，不为了成员数再建总管类。

下一独立项是玩家有效绑定的共享／消费关系反馈：对默认值与草稿的合成结果复用既有比较能力，
而不是只看项目默认值，也不把合法一键多用当成错误。多人、组合键、手柄录制及 UI 框架不在本项范围。
