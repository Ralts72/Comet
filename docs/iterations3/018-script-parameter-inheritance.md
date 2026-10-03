# 018：单个脚本参数恢复默认继承

## 背景与前后区别

脚本参数本来采用稀疏覆盖：未编辑的字段跟随 Lua 的默认值。原面板只能清空整份覆盖，
或者移除已失配字段，无法单独取消一个合法字段的覆盖。
例如同时配置 spin 的 speed 和 score_color 后，希望 speed 跟随以后源码默认值，
只能清空全部后重新配置颜色；手填当前默认数值仍会留下显式覆盖。

现在右键参数名称 → **使用脚本默认值**，仅删除该键。未覆盖时菜单禁用；
即使显式值恰好等于默认值，也仍允许取消覆盖。原“恢复默认参数”继续清空全部。

## 代码与调用链

`PropertyEditorRegistry::edit_parameters` 保留原值控件与结果，用 group 表示一整行，
不修改 Float／Vec／Color／Entity 等类型编辑器。核心变化是：

```cpp
if(item.changed)
    overrides.insert_or_assign(name, std::move(value));
if(ImGui::BeginPopupContextItem("Parameter actions",
       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup)) {
    if(ImGui::MenuItem(Ui::label("Use script default").c_str(), nullptr, false,
           overrides.contains(name))) {
        overrides.erase(name);
        item = {.changed = true, .finished = true};
    }
    ImGui::EndPopup();
}
```

恢复操作优先于本帧控件写值，避免删键后又插回；结果明确是一次离散编辑，
不把旧控件的 active 状态带入。`NoOpenOverExistingPopup` 保留 ColorEdit 原有的分量／色块右键菜单，
颜色参数名称仍可打开恢复菜单，不增加每行按钮，也不按像素估算标签区域。

后续仍走原链路：

```text
参数行删除草稿中的一个键
  → Inspector::apply_property_edit
  → Edit：ParameterMap 预览／提交 → CommandHistory
  → Play：只赋给 Runtime 场景的 ScriptComponent
  → 下次脚本调用 resolve_parameters：缺少的键使用活动定义默认值
```

Play 面板仍读 `running_script`。暂停期间候选资产已发布但实例未换代，恢复后显示旧活动定义默认值；
单步／继续安装候选后才显示新版默认值。没有新加载请求，也不会为了恢复参数触发重载。

## 架构价值

补齐已有稀疏覆盖的编辑闭环，UI 表达“值覆盖”和“继承默认”的区别，
而不是给引擎另建默认值同步器、Reset 命令类或运行状态 owner。
复用原 ParameterMap、序列化、属性事务与 Play 隔离；引擎及公共 API 零改动。
README 补操作入口，架构文档与路线图只同步语义；中文菜单已加入现有 YAML。

## 验证

- 首个 UI 红测确认原代码没有参数行恢复入口，日志 `/tmp/comet-auto3-018-red.log`。
- 新增两个 UI 用例：单项删除保留其他覆盖、一次 Undo／Redo、后续默认变化自然继承、无覆盖重选不产生历史；
  Play 候选／活动定义分离、暂停与单步、Edit 配置及历史不变。
- 扩展现有颜色用例，确认原 Options 保留、单项恢复与先前拖动分别撤销；
  扩展实体引用用例，确认恢复删除键而非写入空 UUID，并能撤销。
- 32 项 EditingUi 定向用例、完整 989 CPU／171 UI 通过；一个原生监听平台不适用的轮询回退用例跳过。
  Debug 全目标无新增编译警告，Shader 构建契约与模块边界检查通过。
- 只读复核无阻塞；未新建生产测试入口或重复类型组合测试。
- 日志：`/tmp/comet-auto3-018-{red-build,red,build,ui,regression}.log`。

## 限制与后续

这是 Lua 显式属性的单项覆盖操作，不扩成所有组件／材质字段的通用重置框架。
只适用于当前定义可显示的合法参数；失配项仍走已有显式修复入口。
源码变化和运行状态迁移协议不变，菜单操作不会保存场景、重编译 Shader 或改写共享材质。
UI 自动测试不等于真实窗口尺寸、字体或完整玩法手感验收；本项无需重复 GPU／性能测试。
