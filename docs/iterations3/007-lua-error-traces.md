# 007：保留 Lua 错误的调用链

## 背景

006 支持 `self:helper()`，002 支持项目模块后，真实脚本调用已经不再只是一层 update。
原来 Log 虽然能报告最内层错误和实体 UUID，却无法解释“哪个入口、哪个辅助方法调用了这个模块”。
本项补齐现有错误信息，不创建调试面板、诊断 Manager 或另一套错误类型。

## 修改前后

原来的保护调用没有消息处理器：

```cpp
lua_pushcfunction(state, function);
lua_pushlightuserdata(state, argument);
const int status = lua_pcall(state, 1, 0, 0);
```

`lua_pcall` 返回时，发生错误的 Lua 调用帧已经展开。此时再在 C++ 外层生成 traceback，
只能看见保护调用外部，无法恢复丢掉的脚本调用链。

现在把私有消息处理器压在被调用函数下面，将它的绝对栈索引交给 `lua_pcall`：

```text
调用前栈高度
  ↓
消息处理器
待执行的 C 入口
入口参数
  ↓ lua_pcall(..., handler_index)
成功：恢复原始栈高度
失败：先在 Lua 帧展开前生成调用栈，再复制 Error 并恢复原始栈高度
```

处理器只读取字符串错误，其他类型采用固定说明，然后使用 Lua 自带的 traceback 构建函数：

```cpp
const char* message = "Lua raised a non-string error";
if(lua_type(state, 1) == LUA_TSTRING)
    message = lua_tostring(state, 1);
luaL_traceback(state, state, message, 1);
return 1;
```

不调用项目的 `tostring`，也不启用 `__tostring`，防止诊断错误时再次执行项目逻辑。
这里仅有普通指针局部变量，不让需要析构的 C++ 对象跨越 Lua 的 longjmp。
生成 traceback 的 Lua 分配仍处于保护调用的消息处理阶段，而不是移到 C++ 返回后执行。

成功和失败都恢复调用前的栈高度，消息处理器不会残留到下一次 Update。
原有指令 hook 在保护调用返回后关闭；内存和指令限制没有提高，脚本权限没有扩张。

## 使用效果与调用链

例如：

```lua
-- actor.lua
local fault = require("scripts.fault")
local script = {}
function script:helper() fault.fail() end
function script:update() self:helper() end
return script
```

模块中的 `error("module exploded")` 原先只能定位模块报错的一行。
现在同一条错误还包含 actor 的 helper、update 和模块帧。顶层 require 的执行错误、
嵌套模块语法错误也会保留尚在执行的 require 调用链；语法错误本身并没有可运行的函数栈。

两条消费链保持原样：

```text
模块候选失败 → Script::load_group → AssetManager 警告 → Log
运行回调失败 → Instance Result → ScriptSystem 附加实体 UUID → Runtime 停止 → Editor 恢复 Edit
```

Console 已经用只读多行文本呈现日志，不需要为 traceback 增加 UI 状态。
模块候选失败仍保留旧 Script，修复源码后沿原资产刷新恢复。
运行回调失败仍按既有 Runtime 策略清理，不因为错误文字更详细就改成忽略失败或继续运行。

## 架构价值

- 在拥有 Lua VM 和保护调用的 Instance 实现内捕获栈，调用方不需要了解 Lua API。
- 复用 Error／Result 和现有日志，不向 Engine、Editor 或公共头暴露 `lua_State`。
- 不复制脚本调用栈，不新增跨帧诊断缓存，也不保存失效的 Lua 帧引用。
- helper、模块和生命周期共用一处边界，避免各消费者重复捕获错误。
- 顺带修正文档里仍写着“不开放 require”的旧描述，明确仅开放项目内受控模块。

## 验证

实现前先运行三个针对调用链的测试，全部按预期失败：错误只含最内层信息，缺少 traceback 和调用者源位置。
首次证据保留在 `/tmp/comet-auto3-007-red-{build,targeted}.log`。

新增或扩展的断言覆盖：

- 入口 → helper → 模块的源位置；连续成功／失败调用后仍可 Stop，检查消息处理器没有累积。
- 嵌套 require 的初始化错误及模块语法错误。
- table、数字、bool、nil 错误采用固定说明，不调用项目重写的 tostring。
- 既有指令／内存预算失败后能执行受保护清理，debug／元表／pcall 权限仍未开放。
- EditorAssets 日志完整透传调用链，错误候选不替换旧脚本，修复后成功换版。
- Play helper 出错后清空待处理结构命令、会话值和重开请求，回到 Edit；修复后可再次 Play。
  事件清理由既有 ScriptSystem 阶段回归验证，不从 Editor 测试访问 Scene 私有队列。

首次全目标构建发现新增 Editor 测试误用了私有 `Scene::take_events()`，已删除越界断言，
不为测试扩大生产接口；证据在 `/tmp/comet-auto3-007-first-build.log`。

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='Script*:EditorAssetsTest.ModuleReloadLogsTheCompleteTraceAndKeepsLastGoodVersion:EditorSceneSessionTest.ScriptFailureRestoresEditSceneAndAllowsAnotherPlay'
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure -V
cmake --build --preset app-release --parallel 6
```

- 修正后 Debug 全目标、Release app 构建通过，无编译 warning/error。
- 102 项定向测试通过；最初三个失败用例均转绿。
- CPU 959 项、无窗口 UI 159 项通过；原生监听平台不执行的轮询回退用例跳过 1 项。
- `shader_build_contract`、`module_boundaries` 通过。
- 未改渲染、窗口或声音，本项不重复 GPU 冒烟；真实 Log 面板交互仍待人工验收。
- 两个辅助代理分别实现和只读交叉审查，主代理统一构建、修复测试并验收。

最终日志：`/tmp/comet-auto3-007-fixed-build.log`、`/tmp/comet-auto3-007-first-targeted.log`、
`/tmp/comet-auto3-007-final-{regression,release}.log`。

## 手动验证

1. 在自建组件脚本的 update 中调用辅助方法，辅助方法再调用项目模块。
2. 在模块中临时加入 `error("test trace")`，Play 后查看 Log。
3. 应能找到模块报错位置、helper 和 update 的调用位置，以及所属实体 UUID。
4. 改为模块顶层报错并保存：已有有效运行版本应继续使用，Log 显示 require 链。
5. 修复模块后保存，按现有规则继续或单步更新；Stop 返回 Edit，不保留失败的运行修改。

此处是人工验证步骤，不代表本轮已经完成真实 GUI／音画交互验收。

## 限制

Lua 内存耗尽会跳过消息处理器；消息处理器自己失败可能只能返回 `error in error handling`。
不保证所有资源耗尽场景都有完整 traceback，也不为此建立备用 VM、额外内存池或重试体系。
Lua 可能省略尾调用帧或截断很深的栈；本项不改变其原生调试信息规则。
源码位置是项目相对位置，不新增编辑器跳转、断点、变量查看、单步 Lua 指令或远程调试协议。
完整调试器仍是后续独立能力，不把调用链日志等同于脚本调试已全部完成。
