# 024：玩家输入覆盖与启动合成

## 背景与前后变化

023 只给项目、动作、绑定建立稳定身份。此前 App／Play 都直接安装 `project.input_actions()`，
修改按键等同于修改项目的默认配置。本项分开作者默认值与玩家个人意图，接通用户文件到实际 Runtime 的路径：

```text
之前：project.json → InputActions → RuntimeInput

现在：project.json ───────────────┐
                               ├→ InputOverrides::resolve → InputActions → RuntimeInput
      用户目录 input.json → 补丁 ┘
```

App 在启动时读取；Editor 在每次进入或重开 Play 的停止边界读取，使用最新已保存的项目默认值。
运行中不扫描文件，不调用原 stopped-only API 强行修改活动 Runtime；Edit 的设置面板仍只编辑项目默认值。

## 数据不是整份默认配置的副本

`InputOverrides` 保存按 UUID 定位的补丁，不持有 Project、文件、Scene 或设备：

```cpp
struct Binding {
    Uuid id;
    std::optional<InputActions::Control> control;
    std::optional<float> scale;
    std::optional<float> deadzone;
    bool disabled = false;
};

struct Action {
    Uuid id;
    InputActions::Type type = InputActions::Type::Button;
    bool disabled = false;
    std::vector<Binding> bindings;
};
```

例如玩家只把 LeftX 换成 RightX，就只写 `control`，项目日后调整倍率／死区仍然生效。
动作名、上下文、默认优先级不写入玩家文件；新动作、新绑定、改名和排序都来自当前项目默认值。
`type` 是兼容性检查，不允许玩家将按钮改成轴。

显式禁用整个动作会清空合成结果中的所有绑定，包括项目后来新增的绑定；禁用单槽只移除对应 UUID。
恢复默认则删除对应补丁。空补丁不合法，删除最后一条槽补丁时应同时删除动作记录。
合成结果不能反写为补丁，否则会把所有默认值固定下来、丢失未来继承关系。

## 校验与兼容性

`create` 检查补丁身份、重复、数量、控制名称和有限值；不复制 InputActions 的数值／类型组合规则。
`resolve` 从当前默认配置复制候选，对单条绑定赋值后复用 `InputActions::create` 验证：

```cpp
auto candidate = *binding;
if(binding_patch.control)
    candidate.control = *binding_patch.control;
if(binding_patch.scale)
    candidate.scale = *binding_patch.scale;
if(binding_patch.deadzone)
    candidate.deadzone = *binding_patch.deadzone;
```

optional 的零值也是显式覆盖，不把 0 错当成缺省。未知动作或类型变化跳过该动作；未知绑定、合成后非法的组合
只跳过该绑定补丁并保留默认。其他兼容补丁继续生效，诊断带动作／绑定身份返回给宿主。
原始补丁始终保留，项目日后恢复相同身份或类型时仍可重新生效；合成过程不“修复”或写入文件。

## 文件责任与宿主

`PlayerInputSettings` 负责严格 JSON、用户路径、加载原文基线与原子保存，不负责动作求值。
文件含自己的 `version: 1`、`project_id` 与 `actions`，不是 project.json v2，也不复用资产 `.meta`。
默认路径是用户配置根的 `players/<project UUID>/default/input.json`；各平台路径和 demo 示例见 README。
`default` 只代表当前唯一的本地玩家，不绑定手柄序号或项目目录位置。

- 文件不存在：空覆盖，不创建文件／目录。
- JSON、结构、重复字段、版本或项目身份错误：加载失败；宿主告警并使用项目默认值，不覆盖坏文件。
- 相同补丁保存：不重写原文本和时间戳。
- 保存前外部新增／删除／改写文件：拒绝覆盖；写失败也不改变内存补丁和原文基线。
- 成功保存：复用 `write_text_file_atomic`，然后一起更新内存补丁和基线；全部恢复保存空 actions，不删除其他用户文件。

App 的 `configure_player_input` 在安装默认 System 前完成合成；场景重开复用当前配置。
Editor 的 `start_play_runtime` 是短回调的具名实现：候选场景已替换且旧 Runtime 已停止，先合成／安装再启动。
Editor 项目输入设置保存仍走原默认配置链路，下次 Play 重新加载玩家文件，避免保存默认后丢掉覆盖效果。
没有把 Editor 偏好路径反向引入 Engine，也没有在 Runtime 中加入文件读取或项目 owner。

## 验证

- 30 项定向 CPU 通过：13 项稀疏合成、16 项当前平台文件边界、1 项实际 Runtime 按键消费。
  Linux 另有一项 XDG 回退测试，当前 macOS 未执行，不冒充跨平台运行结果。
- 消费者测试先保存个人按键，再给默认动作新增 J 绑定，重开文件后安装到 Runtime：Space 不再触发，K 与新 J 可用；
  清空覆盖后恢复默认。保留活动 Runtime 拒绝原整份配置替换的约束。
- 完整 1040 CPU／178 UI 通过，1 项既有原生监听相关用例按平台条件跳过。
- Debug 全目标、Release app、Shader 构建契约、模块边界检查通过，无新增编译警告。
- 只读审查核对两个宿主的停止／加载顺序，并修正路线图中的“空绑定禁用”旧表述及消费者测试的默认升级步骤。

日志前缀 `/tmp/comet-auto3-024-`：build、targeted、regression、release、final-build。
最终定向测试包含增强后的默认升级步骤。宿主加载时点有源代码核对，未执行 App／Play 桌面交互，不将其记为人工验收。

## 限制与后续

当前个人设置可由 JSON 或公共保存接口提供，尚没有游戏内改键 UI；运行中重绑定仍未开放。
下一项需将配置应用与固定步历史、活动上下文、按住基线一起处理，并接入实际设置入口，不能简单清空全部输入。
文件冲突检查与原子替换不是跨进程锁或断电事务；不引入多玩家／多设备分配和自动版本迁移。
