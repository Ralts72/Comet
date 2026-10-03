# 010：显式修复脚本参数覆盖

## 背景

运行时重载已经会保留同名同类型的参数覆盖，但不会回写 Edit 场景。
这是必要的编辑／运行隔离，却留下了作者工作流缺口：脚本删除字段或更换字段类型后，
Edit Inspector 因覆盖不合法而停止绘制参数，原来只能通过“恢复默认参数”清空所有配置。
一个字段改型不应让已经调好的速度、颜色和实体引用一起丢失。

本项提供显式、可撤销的修复；不在文件变更时自动迁移场景，不引入版本兼容格式。

## 修改前后

原来 Inspector 只有严格拒绝和整表清空：

```cpp
if(auto checked = script->validate_overrides(binding.parameters); !checked) {
    ImGui::TextWrapped("%s", checked.error().message.c_str());
    return;
}
```

现在失败后准备一份兼容覆盖副本，只有确实删除了失配项且其余值合法，才显示修复按钮：

```cpp
auto compatible = binding.parameters;
script->retain_compatible_overrides(compatible);
if(compatible.size() != binding.parameters.size()
    && script->validate_overrides(compatible)
    && ImGui::Button(Ui::label("Remove incompatible overrides").c_str())) {
    apply_property_edit(
        entity, component, property, compatible, {.changed = true, .finished = true});
}
```

读取参数页仍不改变场景。点击后复用既有 PropertyEditTransaction：一次修复是一条历史，
Undo 恢复原覆盖，Redo 恢复修复结果；无变化没有按钮操作，也不新增历史。
保留的“恢复默认参数”仍是全部清空，不改变原语义。

例如旧覆盖为：

```text
speed = 250       → 名称与类型不变：保留 250
removed = false   → 新源码已删除声明：移除
changed = true    → 新声明变成 string：移除覆盖，使用新默认值
player = UUID     → 仍是 EntityUuid：保留引用
score_color = …   → Color 改为普通 Vec4：仍保留四分量值
```

过滤不填入默认值，`.scene` 仍保存稀疏覆盖；新默认值继续由 resolve_parameters 合成。
改名不猜测新旧字段映射，Vec3 与 Vec4 不自动转型。

## 共用规则与职责

此前声明兼容过滤内联在 ScriptSystem 的换版循环里，现在移到定义所有者 Script：

```cpp
void Script::retain_compatible_overrides(ParameterMap& overrides) const {
    std::erase_if(overrides, [&](const auto& value) {
        const auto property = m_properties.find(value.first);
        return property == m_properties.end()
               || property->second.default_value.index() != value.second.index();
    });
}
```

运行时的原有调用点简化为：

```cpp
auto overrides = entity.get_component<ScriptComponent>().parameters;
if(existing != m_entries.end())
    script->retain_compatible_overrides(overrides);
auto candidate = prepare_entry(entity, script, std::move(overrides));
```

仅已有实例换版会过滤；新挂载组件仍严格验证，不能借本次重载吞掉它原本错误的配置。
非有限数值、过长字符串等匹配类型但非法的值不会被自动删除；validate_overrides 和
resolve_parameters 保留原有严格失败行为。

职责仍是：Script 定义兼容性；ScriptSystem 决定运行换版时机；Inspector 决定何时向作者提供操作；
PropertyEditTransaction 负责 Edit 历史。没有新增 Manager、命令类型、缓存或事件总线。
Play 的面板继续读取 running_script，不能用 Registry 中尚未启用的定义提前删除运行覆盖。

## 验证记录

实现前先增加真实 Inspector 操作回归，首次运行按预期失败：旧覆盖仍含 removed／changed，
没有修复入口。日志保留在 `/tmp/comet-auto3-010-red-{build,targeted}.log`。

新增三个 ScriptSourceTest，覆盖过滤、默认值不变、Color／Vec4／EntityUuid、重复无变化及非法值不静默清除。
新增一条 UI 回归覆盖修复、保留兼容值、一次 Undo／Redo；扩展两条现有 UI 回归，
检查定义变化取消旧手势后不恢复旧值，以及 Play 新资产发布但实例尚未换版时不误删旧定义参数。

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='Script*:EditorSceneSessionTest.Script*'
build/tests/editor_ui_testing --gtest_filter='EditingUiTest.*'
cmake --build --preset app-release --parallel 6
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure -V
```

- Debug 全目标、Release app 构建通过，无编译 warning/error。
- 首次修复后 105 项定向 CPU、30 项 Inspector UI 通过，原红测转绿。
- 完整回归 965 CPU、160 UI 通过，既有原生监听平台的轮询回退用例跳过 1 项。
- shader_build_contract、module_boundaries 通过。
- 两个实现代理分别只读交叉审查另一侧，无确定的未解决问题。
- 本项不改绘制、窗口或声音，不重复 GPU 冒烟；完整 demo 音画仍待手工验收。

日志为 `/tmp/comet-auto3-010-first-{build,targeted,ui}.log`、
`/tmp/comet-auto3-010-final-{release,regression}.log`。初始红测没有删除或改写。

## 006–010 阶段性架构回顾

- **目录与职责**：006 的辅助方法与 007 的调用栈都落在原 Script::Instance 内；009 仅补真实监听链路测试；
  010 在原 Script／ScriptSystem／Inspector 中接通，无新增顶层服务、文件夹或平行脚本管理器。
- **依赖与调用链**：Script 不依赖 Editor，Inspector 通过公开定义接口调用；008 的鼠标授权从 Gate 下传给
  RuntimeInput，Editor 不清理运行时私有缓存。模块加载、资产发布、运行换代仍各有唯一入口。
- **状态与生命周期**：每 VM 的定义与 self 隔离不变；traceback 在原保护调用中生成并恢复原栈；
  参数修复保留 Edit 历史和 Play 活动定义边界。未为按钮新增版本号、缓存或持久迁移状态。
- **冗余与可读性**：010 去掉 ScriptSystem 内联过滤，由 Script 提供可复用规则；保留严格校验与过滤不同语义，
  不为减少方法数量合并成“校验时顺便修改”。没有发现需要额外抽象的新重复 owner。
- **测试／文档**：复用已有场景、资产和无窗口 UI 夹具；不增加生产测试访问器。当前使用规则集中在 README／
  架构文档，具体红绿测试过程保留编号记录，路线图仅同步真实已接通的作者能力。

本次回顾没有凭文件长度拆分 Script 或强推事件框架。下一项优先闭合 source-only 模块的文件操作，
不把模块伪装成可挂载资产，也不增加无消费者的 Lua API。

## 使用与限制

1. Edit 中给组件脚本配置多个参数并保存场景。
2. 外部修改脚本，删除一个字段或改变其类型，保留另一个字段。
3. 文件刷新后 Inspector 显示诊断及“移除不兼容参数覆盖”。
4. 点击后，未变字段保持配置，新字段采用声明默认值；Undo／Redo 能来回恢复。
5. 按正常场景保存流程持久化，不会因为查看或刷新脚本自动保存。

这是可执行的手动步骤，不表示本轮已完成真实 GUI 交互验收。
不支持任意 Lua self 状态迁移、自动字段改名映射、全项目批量场景修复或单字段恢复默认；
这些能力不因一个明确的作者缺口而一并引入。
