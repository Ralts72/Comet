# 020：物理按键配置闭环与阶段性架构回顾

## 背景与实际影响

运行时 `Input::Key` 和 Window 已支持小键盘、标点及锁定键，`InputActions::create` 也接受这些键，
但持久化名称表只有一部分。旧测试甚至明确断言 `Keypad1` 无法格式化。

这不是已有合法 JSON 项目普遍丢配置：当时 JSON 解析同样不接受这些名称，Project 保存会提前失败。
真实缺口是小键盘等按键不能配置／录制，以及 C++ 创建的有效 InputActions 进入设置面板后可能被跳过。
录制测试另外确认 macOS 的 Ctrl／Cmd 身份会反转。

## 改动前后

### Engine：既有按键必须可以保存和恢复

之前两份契约不一致：

```cpp
// create 接受全部已定义物理键。
return control > Key::Unknown && control < Key::Count;

// 但名字表不完整，合法键也可能失败。
InputActions::format_binding({Input::Key::Keypad1}); // failure
```

现在补齐既有 `key_names`，仍由原 parser／formatter 双向共用，不新增键枚举或并行注册器：

```cpp
{"Keypad1", Input::Key::Keypad1},
{"Comma", Input::Key::Comma},
{"KeypadEnter", Input::Key::KeypadEnter}
```

`Keypad1` 和 `1`、`KeypadEnter` 和 `Enter` 是不同键；名称往返不丢倍率和死区。
Unknown／Count 仍无效。Project 的原子保存、版本检查与失败返回协议没有改变。

### Editor：转换枚举，不再定义第二份持久化名称

之前私有 `key_name` 直接生成字符串；请求面板时格式化失败就跳过：

```cpp
if(control)
    draft.bindings.push_back(...);
```

现在 `physical_key` 只把 ImGuiKey 转成 Comet 的 Key，再交还 Engine 命名：

```cpp
const auto control = physical_key(key);
const auto name = Comet::InputActions::format_binding({control}).value();
m_actions[action].bindings[binding].control = name.control;
```

实际调用先检查 `control != Unknown`。面板请求接收的是已由 create 校验、对外只读的 InputActions，
现在所有有效绑定都有名字，因此不再用失败分支悄悄删掉绑定。独立 Binding 的 format API 仍返回 Result，
因为外部可以直接传入 Unknown 等无效枚举。

ImGui 的 macOS 配置在入队时交换 Ctrl／Super，项目配置却需要真实物理键；转换时做对应逆变换。
此转换不修改 ImGui 设置、编辑器自身快捷键或运行时输入采样。
没有使用 `ImGui::GetKeyName`：该接口明确只供调试，不保证其字符串适合持久化或比较。

## 调用链和架构价值

```text
录制：ImGui 事件 → Editor 物理键适配 → InputActions 名称 → 设置草稿
保存：草稿 → parse_binding/create → Project 原子保存
运行：Project 读取 → InputActions → RuntimeInput → App／Play／Lua 动作消费者
```

引擎的稳定命名与 Editor 的平台适配分开；没有在 Engine 引入 ImGui，也没有新 Manager、回调或缓存。
这是补齐同一既有能力的边界契约，不是为各入口新增不同的兜底规则。

## 016–020 阶段性回顾

| 审查点 | 结论 |
| --- | --- |
| Lua 生命周期 | Context 只借用单次调用环境；Instance 独占 VM；ScriptSystem 管运行实例；AssetManagerScripts 管源码组发布，不能简单合并两种分组 |
| 参数默认值 | 018 只删除稀疏覆盖，继续使用原事务／活动定义，无第二份默认值 owner |
| 日志 | 019 复用 Logger，预算随调用清空，Stop 可输出普通日志；修正文档中“只能输出输入组关闭”的过期表述 |
| 渲染与输入 | 016 用真实 demo 到 GPU 回读提供证据，未扩张渲染职责；017／020 修复配置与输入边界，不改变路由所有权 |
| 目录与重复 | source-only 文件操作继续共用文件事务；输入面板职责仍完整，不为行数机械拆文件；命名字符串归 Engine |
| 文档与测试 | README 只更新实际使用限制；测试沿公开入口，没有为测试开放生产内部状态；GPU 不为本次键名变更重复跑 |

双代理只读复核没有发现另一个生产阻塞；不追加没有消费者的 Lua API 或全局事件系统。

## 验证

- 修复前：两项 CPU、两项 UI 红测分别复现名称缺失、Project 保存失败、面板丢绑定及录制遗漏。
- 修复后：25 项输入／项目 CPU、13 项项目设置 UI 定向通过。
- 全键枚举逐一验证 create → format → parse；Project 保存重开后真实采样小键盘轴，值仍为 -1。
- UI 覆盖主键区／小键盘区别、标点、F24、左右 Ctrl／Super；分别测试 macOS 行为开／关。
- Oem102 不猜测成 World1／World2，仍保持录制直到 Esc 取消，原绑定不变。
- Debug 全目标、Release app 构建通过；完整 994 CPU／173 UI 通过，1 项平台条件跳过；
  Shader 构建契约与模块边界检查通过。
- clang-format 检查、`git diff --check` 通过；未改第三方源码或 CI，未推送。

本地日志前缀 `/tmp/comet-auto3-020-`，包含 red-build、red-cpu、red-ui、build、cpu、ui、
final-build、regression、release。UI 自动测试不等于真实键盘布局和设备的人工验收。

## 限制与后续

Esc 继续取消录制；`Escape`、`F25`、`World1`、`World2` 可以手填。ImGui 没有 F25，
当前后端将 World1／World2 合并成 Oem102，因此不通过猜测或新原生事件旁路来录制。
名称可保存不代表每个键盘／操作系统都能产生对应事件；不承诺布局、IME、组合键或玩家覆盖。
这些扩展仍按路线图和实际消费者独立推进。
