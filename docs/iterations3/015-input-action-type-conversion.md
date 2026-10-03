# 015：输入动作类型切换不遗留隐藏的非法倍率

## 背景与复现

demo 的 `demo.move_x` 是 Axis，Right 为 1、Left 为 -1。项目输入面板允许切换动作类型，
但之前只写入 type；改成 Button 后隐藏倍率输入框，却保留 -1，最终校验拒绝保存。
重新录入按键也只改 control。作者必须切回 Axis 或重建绑定，才能处理已经看不到的数值。

实际 UI 红测从有效的 A=-1、D=2 切到 Button，点击 Save 未产生有效配置，按预期失败：
`/tmp/comet-auto3-015-red.log`。这不是给校验增加宽松兜底，而是修复类型编辑操作本身。

## 代码前后

原来：

```cpp
if(ImGui::Selectable(type_name, action.type == type))
    action.type = type;
```

现在仅在用户选择不同类型时，处理新类型不再具有的倍率语义：

```cpp
if(ImGui::Selectable(type_name, action.type == type) && action.type != type) {
    action.type = type;
    if(type == Type::Button)
        for(auto& binding : action.bindings)
            binding.scale = 1;
}
```

操作发生在面板草稿；没有保存之前，Project 与运行输入不改变。
保留 control、source、deadzone、组、顺序及绑定数量；Axis↔Delta 也不删除配置。
例如键盘绑定改到 Delta 后仍需明确选择 motion，不能为了让保存成功而自动删除所有键盘绑定。
Button 再改回 Axis 不恢复过去的反向倍率，没有增加第二份隐藏草稿。

InputActions::create 和项目反序列化的严格校验不变；非法手工项目文件不会被静默修复。
README 只补类型切换规则，不把这条 UI 缺陷另列为路线图能力阶段。

## 验证

- 首次红测确认 Button 转换后 Save 不产生请求，失败日志保留；未把测试改成接受旧行为。
- 修复后 10 项项目输入／设置 UI 测试通过，覆盖倍率归一、原配置隔离、绑定及组保留、返回 Axis，
  以及不兼容来源仍保留且明确拒绝保存。
- Debug 全目标构建通过，无新增编译警告。
- 里程碑完整回归：989 CPU、168 无窗口 UI 通过；1 个既有轮询回退用例因原生监听平台条件跳过。
  Shader 构建契约与模块边界通过。
- 本阶段 Release 已在 014 完整构建；015 再次验证 `app-release` 为最新、无工作可做，
  因为本项只改 Editor，不把无变更构建描述为重新编译 Runtime。
- 两份只读审查未发现阻塞问题。本项不触及渲染／音频，不重复 GPU 或性能测量。
- 日志：`/tmp/comet-auto3-015-{red,build,ui,regression,release}.log`。

## 011–015 阶段性架构回顾

- **职责与目录**：011–012 的模块改名／删除仍在 Editor 文件操作层，模块使用路径、资产使用 Handle／revision。
  source-only 没有挤入 Registry 或 Selection，也没有给模块创造场景历史。
- **复用与冗余**：012 共用资产双文件与模块单文件删除事务；模块改名和资产移动的身份／metadata 契约不同，
  保留明确入口。相似的几段路径验证没有足够证据支持新的 FileSystem、Manager 或虚基类。
- **上层编排**：ProjectPanel 只持文件树、modal 和请求，扫描发布后重建树；不逐帧扫描文件。
  InputSettingsPanel 只拥有草稿与显示，014 的消费判定复用 Engine 规则，015 不新增运行配置 owner。
- **生命周期**：013 的关闭组名只是调用期间借用输出；ScriptSystem 决定转交／丢弃，仍走原 Scene 队列和下一 prepare。
  没有为 Stop 开放世界访问、保存长期 Lua 引用或引入输入所有权栈。
- **能力边界**：014 的两两比较不是最终路由预测，默认禁用与当前 Play 分开；015 只修复类型编辑，
  引擎严格校验不变。RuntimeInput、项目格式、资产加载 owner 与编辑命令历史没有平行体系。
- **文件规模**：source_operations、project_panel、input_settings_panel 的代码量增加，但新增内容仍围绕同一入口职责，
  已使用具名阶段／函数收敛。当前没有为了减少行数而拆类、把几个变量套进新结构或另建目录的理由。
- **测试与文档**：沿用公开类型和 UI 交互夹具，没有生产测试访问器；仅新增有真实反例或新功能契约的检查。
  规则放 README／架构，过程证据放迭代文档，路线图保留真实交互和未实现扩展，没有用自动回归替代手工验收。

专项复核模块模板／创建／改名／删除补回、运行参数定义隔离、暂停与最后实例清理，未确认新的阻塞缺陷。
后续优先补实际 app／Editor Play 的整体验收证据或有真实消费者的功能，不为编号、文件长度或凑时长制造重构项。

## 限制与后续

这只是明确类型转换，不是通用配置迁移或自动修复器。非法来源仍由可见的 Source 控件和既有诊断处理，
不猜测手柄轴应替换成哪个按钮。UI 自动回归不代表完整 GUI 布局与设备手感验收。
本项无新类、文件夹、缓存、Engine API 或回调；额外代码只覆盖有证据的编辑路径。
