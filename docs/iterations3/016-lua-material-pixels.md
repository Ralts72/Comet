# 016：真实 Lua 材质输出贯通 GPU 像素

## 背景与前后对比

此前已有三段证据：真实 demo Lua 修改 Scene 值；SceneExtractor 保留不可变覆盖快照；
手工构造覆盖值的 GPU 用例验证材质隔离与重载。它们没有一起验证 Lua 的输出确实送进渲染器。
本次没有发现生产缺陷，补一项路线图要求的纵向验收，不新建玩法、渲染能力或测试专用生产入口。

## 代码与逻辑

新增 `tests/render/test_script_material.cpp`，仍用已有 `RenderGraphGpuTest` 夹具。
独立文件按脚本与渲染的交汇场景命名，不再扩充近千行的通用 pipeline 测试；
只在 `tests/CMakeLists.txt` 的 integration 清单登记，沿用同步验证执行组。

```cpp
const std::array<std::filesystem::path, 1> roots{"scripts/spin.lua"};
auto scripts = Script::load_group(project.value().paths().assets(), roots);
runtime.set_input_actions(project.value().input_actions());
runtime.add_system(std::make_unique<ScriptSystem>(assets, &materials));
runtime.start(scene);

// 输入进入运行时，测试不直接填写材质覆盖。
input.key_event(Input::Key::Tab, true);
runtime.advance(0, &input.publish_frame());
render_and_copy(renderer, SceneExtractor::extract(scene), frames, readback);
```

以上为省略结果校验的链路示意；实际测试检查每一步返回结果。脚本和模块取自 demo，
输入绑定也从 demo 项目加载；仅关闭旋转，保留原调色及事件方法。
两个 quad 共用同一 PBR Material，仅左侧挂脚本。正交相机、固定白色平行光、无环境光、
无 bloom、曝光 1，避免物体位置或场景默认效果改变取样条件。

| 阶段 | 挂脚本物体 | 共用材质的另一物体 |
| --- | --- | --- |
| Start | authored 色 | authored 色 |
| Tab | 蓝色 | 不变 |
| 下一输入准备后 Right | 橙色 | 不变 |
| 暂停、排队计分事件、advance | 仍为橙色 | 不变 |
| 单步交付事件 | 绿色，同时上移 0.4 | 不变 |
| Stop 后再次绘制 | authored 色 | 不变 |

颜色期望写成独立的玩法常量，再使用已有 PBR 参考与输出色彩映射计算字节期望，
不从实际 Scene 覆盖值生成期望。取样点位于物体上移前后共同覆盖区域。
每阶段独立读回 owner，完成全部阶段后检查所有输出，包含 Stop 前产生的像素；
帧槽按原 helper 复用，并不声称六帧同时在途。另检查共享 Material 的值和 revision 未变。

## 架构价值

证明既有 Input／Runtime／Script／Scene／Renderer 边界可以通过公开接口组合，
无需 friend、测试访问器、平行渲染入口、事件总线或生产代码条件分支。
没有新增 CPU／GPU owner；复用既有 Readback 和 FrameWait，断言失败也会完成必要等待。
README 的使用入口与项目架构没有变化，复核后不添加测试过程。路线图区分自动像素证据和人工交互待办。

## 验证

- Debug 全目标构建通过；新增 GPU 用例首轮通过。
- 55 项 ScriptSystem／SceneExtractor／MaterialRuntime CPU 回归通过。
- CTest `render_graph_sync_validation` 的 40 项 GPU 用例通过，包含新增用例、相关图形／后处理／诊断
  和缺屏障对照，确认同步检查确实生效。
- 初次手动使用新 layer 环境变量时，既有对照用例仅识别旧变量而跳过；未把该次跳过宣称为对照通过。
  CTest 旧配置运行正常，但本机 Vulkan layer 会提示旧配置弃用；不是新增代码警告或同步错误。
- 日志：`/tmp/comet-auto3-016-{build,cpu,gpu,gpu-suite,sync}.log`。

## 限制与后续

这是固定小画面的集成验收，不是性能测量、文件监听／整组重载测试、完整 demo 物理／音频验证，
也不替代 App／Editor Play 的人工交互。事件通过公开 Scene API 排队，计分生产者已有 CPU 覆盖。
后续仍按真实消费者扩展，不因为本项通过就开放全局材质或任意 Shader uniform 修改。
