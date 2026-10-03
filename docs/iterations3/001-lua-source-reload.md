# 001：Lua 源码受控重载

本项收尾 main 工作区已有的 Lua 重载改动，代码与本文一起提交到本地 `codex/auto3`。

## 1. 原来的缺口

编辑器已经能监视 Lua 源文件，但原先的资产刷新把 Script 与 Audio 一起从 Registry 失效移除，
等待后续场景引用加载新版；活动实例仍保留旧资源。ScriptSystem 的活动实例只按下面的身份维护：

```cpp
struct Key {
    EntityUuid entity;
    uint64_t lifetime;
    AssetHandle asset;
};
```

修改文件不会改变 Handle、组件寿命或实体 UUID，因此旧实例一直运行旧代码。
此前必须重开本局或 Stop／Play，才能创建使用新版 Script 的实例。

本次复用资产监听，在 AssetManager 原有刷新入口中接通 Script 候选加载、验证和替换发布，
失败不移除旧资源；再接通已发布 Script 到活动实例的更新。Audio 的策略不变，不建立新的源码缓存。

## 2. 检测与准备放在哪里

`engine/src/scene/systems/script_system.cpp` 的 `synchronize()` 仍是实例维护入口：

```text
清理已删除／解绑的实例
→ reload_changed_scripts：处理同 Handle 的新版 Script
→ 为新挂载组件创建实例
→ 执行本次 fixed_update 或 update
```

换版依据是不可变 Script 对象的身份，不是文件名、时间戳或另一套 revision：

```cpp
auto script = m_assets.resolve<Script>(key.asset);
if(script && script != entry.script && script != entry.failed_reload.lock())
    changed.try_emplace(key.asset, std::move(script));
```

这里检查的是内存中的已发布资产，不是每帧扫描文件。暂停时没有脚本更新；继续或单步才会处理新版。

## 3. 为什么按同一资产批量处理

同一个脚本可能挂在多个实体上。若每个实体独立“停止旧实例 → 尝试创建新版”，后面的创建失败时，
前面的实体已经换版，容易出现同一资产部分成功、部分失败的状态。

现在先在局部 `prepared` 中创建这一组的全部候选：

```text
准备全部候选 VM 与参数
  ├─ 失败：销毁候选、保留全部旧实例、记录本次失败版本
  └─ 成功：逆序停止旧实例 → 安装候选 → 执行新的 on_start
```

`prepare_entry()` 复用新组件首次创建和重载两条路径的参数解析、VM 创建，避免维护两套创建流程。
准备期间不调用新脚本的生命周期方法，不把候选的活动定义暴露给 Inspector。

`Entry::failed_reload` 是弱引用，仅避免同一失败候选反复尝试和刷日志；不会额外保活源码。
发布另一个 Script 对象后可以再尝试。

## 4. 参数覆盖如何迁移

只保留“名称仍存在，且 ParameterValue 类型一致”的覆盖：

```cpp
std::erase_if(overrides, [&](const auto& value) {
    const auto property = script->properties().find(value.first);
    return property == script->properties().end()
           || property->second.default_value.index() != value.second.index();
});
```

例如旧声明 `speed = 10`，实体覆盖为 `25`，新版默认改成 `20`：仍使用覆盖 `25`。
若新版改成 `speed = "fast"`，旧的浮点覆盖被移除，使用字符串默认值。
删除的字段不继续携带，新字段直接使用新默认值；Color／普通 Vec4 的编辑语义变化不改变存储类型。

过滤只修改活动 Scene 中的 ScriptComponent；Editor 的 Edit 场景副本、文件和 Undo 历史不被改写。
因此 Stop 后若 Edit 场景的旧覆盖已不兼容新声明，仍需在 Edit 中修正或恢复默认参数，不会静默改写文档。

## 5. 两类失败不能混为一谈

语法／声明验证失败由资产加载层拒绝发布。VM 或参数准备失败时，ScriptSystem 不停止旧实例。

但新版 `on_start` 已经可以创建实体、写会话值或修改 Transform：

```cpp
if(auto started = invoke(key, entry, Script::Phase::Start); !started)
    return started;
```

这种执行失败仍沿既有 SceneRuntime 错误路径停止并清理；Editor 恢复 Edit，独立 app 返回运行错误。
本次不引入整个世界的事务，也不声称可以恢复已执行了一部分的新脚本副作用。

重载只重建脚本实例，不重开场景：任意 `self` 状态会清空，但 Scene 会话、物理、实体和待派发通知保留。
事件声明随新实例换版，未消费通知使用新声明，已经消费的通知不会重放。

## 6. Inspector 为什么也有修改

Play 面板继续使用 `running_script()`，不会仅因 Registry 中出现新版就提前更换控件。
真正切换实例后，旧浮点输入框可能已变成字符串或消失，因此需要结束旧控件的输入状态。

```cpp
if(m_script_active_item && ImGui::GetActiveID() == m_script_active_item)
    ImGui::ClearActiveID();
```

`m_script_active_item` 只记录脚本参数的活动控件，不复用代表全部属性的 `m_active_item`。
因此既不打断同面板中的 Transform／刚体拖动，也不清其他面板焦点；Edit 中已有参数事务仍按原逻辑取消。

## 7. 如何观察效果

1. 打开 demo，进入 Play，观察旋转方块。
2. 修改 `demo/assets/scripts/spin.lua`：把旋转表达式改成 `-self.parameters.speed * dt` 并保存。
3. 资产验证和运行更新完成后，方块应直接反向旋转；Console 会记录实例重载。
4. 暂停后再次修改，画面保持；单步或继续时应用新版。
5. 制造语法错误保存，旧版应继续运行；修正后再保存即可发布新版。

独立 app 共用实例换代机制，但本次不为它增加源文件监听。
本轮不实现 require、任意 Lua 状态热迁移、跨场景状态迁移或世界副作用回滚。

## 8. 自动验证

CPU 回归覆盖共享实例批量换版、兼容参数、self 重置、暂停／单步、多固定步和事件声明。
真实文件回归覆盖“编辑 Lua → 资产刷新 → 拒绝无效源码并保留旧版 → 修正后发布 → 更新边界切换”。
另验证第一个新版实例启动成功、第二个失败后的清理；再次启动时检查会话值和旧通知不残留，
以及所有旧实例停止后才启动新实例。不为这些断言开放 Scene 的私有事件队列。
无窗口 UI 回归覆盖边界前后的定义切换、旧拖动失效、Edit 隔离，以及同面板其他属性和跨面板焦点保留。
候选 VM 准备失败的去重分支没有增加生产故障注入接口，未做直接故障注入验证。
本项的代码边界以 CPU／无窗口 UI 回归验收，完整画面体验仍按路线图保留。

2026-10-03 本轮复核执行：

```sh
cmake --build --preset dev-debug --parallel 6
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure
cmake --build --preset app-release --parallel 6
```

Debug 全量构建与 Release app 构建通过；895 项 CPU 与 156 项无窗口 UI 测试通过。
1 项轮询回退测试因当前启用了原生文件通知而跳过；Shader 构建契约和模块边界检查通过。
本轮没有运行 Release 测试套件、GPU／真实窗口验收，不能据此确认画面和手感。

验证日志位于本机 `/tmp/comet-auto3-001-recheck-build.log`、
`/tmp/comet-auto3-001-recheck-tests.log`、`/tmp/comet-auto3-001-release-build.log`，不作为仓库产物提交。

跨模块复核未发现新的生产阻塞：文件解析归资产层，实例切换归 ScriptSystem，控件手势归 Inspector。
后续共享模块必须把当前按 Handle 分组扩展为依赖关联组，不能直接复用单资产换版就宣称关联更新完成。
