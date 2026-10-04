# 023：项目与输入绑定的稳定身份

## 背景

项目输入设置已经可以编辑默认按键，但还不能安全地保存玩家改键。例如玩家把 `jump` 的第一条绑定改成 K，
如果只保存动作名或数组位置，作者改名、重排绑定后，覆盖可能丢失或落到另一条绑定上。
本项先交付真实项目文件与现有编辑面板的身份闭环，不把它描述成游戏内改键已经完成。

## 项目格式与职责

之前项目只保存版本、名称、启动场景与输入语义；现在严格使用 project.json v2：

```json
{
  "version": 2,
  "id": "86c767a0-41a3-4df2-a3fa-d857e809909c",
  "name": "Game",
  "startup_scene": "scenes/main.scene",
  "input_actions": [{
    "id": "4d2e3680-adfc-44cd-a9a0-2d2e609ad75a",
    "name": "jump",
    "type": "button",
    "bindings": [{
      "id": "9783c2f0-86ba-487b-af35-f05e3a2dde95",
      "source": "key",
      "control": "Space"
    }]
  }]
}
```

项目 UUID 代表同一个项目／产品，移动目录、修改显示名不改变它。复制整个项目文件保留身份；
编辑器新建独立项目才生成新 UUID。动作名仍是 Lua 查询用的语义键，UUID 不会自动改写脚本中的动作名。
绑定 UUID 代表一条可修改的绑定槽，而不是当前物理按键：换键、调倍率、调整顺序都保持身份。

`Project::load` 要求 ID 存在、可解析且非零；`InputActions::create` 检查非零动作 ID 全局唯一、绑定 ID 在动作内唯一。
保存入口用 `validate_persistent_ids` 拒绝匿名配置，校验失败不改文件或内存。私有序列化函数只负责写出，
不再次重复入口校验。v1 明确报版本错误，不自动升级、备份或覆盖；demo 与仓库有效项目夹具一次性更新。

## UUID 复用，不增加第二套生成器

原 EntityUuid 的字节、解析、生成、格式化及 hash 实现移到 `common/uuid.h/.cpp`：

```cpp
// scene/entity_uuid.h
using EntityUuid = Uuid;
inline constexpr EntityUuid INVALID_ENTITY_UUID{};

// input/input_actions.h
struct Binding {
    std::variant<Input::Key, Input::MouseButton, Input::GamepadButton,
        Input::GamepadAxis, Motion> control;
    float scale = 1;
    float deadzone = 0;
    Uuid id{};
};
```

Scene 保留原 EntityUuid 语义名称和文件入口，没有修改场景格式。Project 与 Input 只依赖 common，
不为了取得 UUID 依赖 Scene 或 AssetHandle。纯内存配置仍可匿名，避免窗口采样与既有独立测试被迫生成持久身份；
身份生成只发生在需要创建可保存内容的作者操作中，不发生在每次校验或每帧求值中。

## Editor 草稿到保存

此前草稿只复制动作与绑定的编辑字段；现在同时复制已有 ID，新草稿字段默认生成 UUID：

```cpp
ActionDraft draft{action.name, action.type, {}, {}, action.id};
// build() 保留草稿身份，反复保存不会重建。
action.id = draft.id;
binding.value().id = draft_binding.id;
```

以上为对应逻辑的缩写，实际绑定解析仍复用 `InputActions::parse_binding`。
关闭不保存仍丢弃草稿，保存失败仍保留草稿；面板不增加 UUID 输入框或普通用户提示。
删除后再添加是新槽，新 UUID 不复用被删除的 ID。项目创建统一生成项目 UUID，再沿原加载流程校验。

本次增删回归还发现旧 UI 问题：移除第 0 行后继续绘制新的第 0 行，会在同一帧重复使用已激活的控件 ID。
现在删除后结束当帧绑定列表遍历，下一帧正常绘制剩余行，避免一次删除连续移除后续绑定。

## 验证

- 64 项输入／UUID／Project／项目创建／Shader 项目夹具定向 CPU 测试通过。
- 新 UI 增删用例先暴露连续删除问题，修复后完整 178 项 UI 通过。
- 完整 1010 项 CPU 通过，1 项原生监听相关用例按平台条件跳过。
- Debug 全目标、Release app、Shader 构建契约及模块边界检查通过，无新增编译警告。
- 覆盖项目隔离、保存后重开、改名／重排保留身份、匿名保存与非法／重复／缺失 ID 拒绝、旧版本不改写。
- 只读审查后移除了私有序列化和解析成功末尾的重复身份校验；入口仍保留完整验证。

日志前缀 `/tmp/comet-auto3-023-`：build、cpu、ui、regression、release；`ui` 保留首次红测，
最终绿色结果在 regression 中。本项不改变渲染或运行输入采样，没有用 GPU 冒烟替代数据行为验证。

## 架构价值与限制

这是后续“项目默认值 + 玩家稀疏覆盖”的定位依据，不增加输入 Manager、全局事件或另一份动作映射。
当前运行时仍只允许在停止状态设置整份配置，玩家覆盖文件、运行中重绑定和 App／Play 设置入口尚未实现。
后续必须分别验收：未覆盖字段继承新默认、非法文件不覆盖原件、更新边界清理旧输入，以及真正的用户入口。
