# 019：Lua 正常执行日志

## 背景与前后区别

Lua 原先禁用了原生 `print`，`comet` 也没有日志入口。错误调用栈能解释“为什么失败”，
却不能让项目作者在不打断 Play 的情况下观察分数、状态等运行值。
这属于本轮脚本开发工作流，不扩展断点调试器或新增编辑器面板。

现在在生命周期／事件方法及其调用的辅助方法中使用：

```lua
function script:on_trigger_enter(other)
    -- 原有收集处理……
    comet.log("Goal collected; score=" .. score)
end
```

demo 的 collect_goal 在成功收集、提交原有玩法请求后记录一次分数，不逐帧打印。
消息走现有 Info 日志，Editor 的 Log 面板与 app 的日志输出共用同一条链路；宿主过滤配置仍有效。

## 代码、边界与设计理由

```text
Script::Instance::invoke
  → 为本次调用初始化 LuaBindings::Context，授予 can_log
  → Lua 生命周期／事件／辅助方法调用 comet.log
  → 检查单个字符串与调用阶段，读取 Lua 文件和行号
  → Logger → 已有控制台／文件／Editor Log sink
  → 调用结束清空 Context
```

Script 定义预备和实例构造仍会执行顶层代码，因此不能把普通日志直接开放给初始化过程，
否则资产检查、候选失败或实例预备都可能产生日志副作用。Context 默认不授予输出权限，
只有真实 `invoke` 开放，包括 `on_stop`；不依靠 Scene／Entity 是否为空判断是否能打印。
顶层调用会沿原 Lua 错误边界诊断，没有静默执行假日志。

绑定的核心是固定格式串和借用的消息，不把项目文本解释为格式：

```cpp
if(lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING)
    return luaL_error(state, "comet.log expects exactly one string");
// 完成 Lua 检查和源位置读取后，再进入 C++ Logger。
LOG_INFO("[Lua] {}:{}: {}", source, line, std::string_view(message, length));
```

不实现原生 print 的可变参数／隐式 tostring；数字由项目显式拼接或格式化。
绑定回调中只有平凡局部；所有 Lua API 调用都发生在 LOG 宏产生 C++ 所有者之前，
因此没有新增跨 longjmp 的字符串、Result 或 shared_ptr 生命周期。

输出预算和调用错误分开：每次 invoke 最多 16 条，每条最多 4096 字节。
超限消息省略、该次 invoke 最多一条固定 Warning，仍继续执行；不会因为循环打印而退出 Play。
计数与告警标记随 Context 初始化／清空，辅助方法和模块共享本次额度，下一次调用重新计数。
参数个数／类型错误仍按既有 API 契约报错，而不是猜测或自动转换。

## 架构价值

复用 Logger、Lua 保护边界及现有 Log sink，没有新日志队列、回调服务、事件总线或独立 owner。
Script 公共 API 和 Scene／Runtime 协议不变；LuaBindings 只在实现文件包含 Logger。
独立测试文件按脚本日志职责归入现有 scripting 目录，不继续扩充大脚本测试文件。
README 同步真实用法；架构文档顺便修正 013 后已过时的“绑定层不依赖 InputActions”表述：
它复用组名校验与容量，但不持有或采样 RuntimeInput。

## 验证

- 新增三个用例先在原代码上全部失败，确认缺少正常日志入口；红测日志保留。
- 57 项 ScriptLogging／ScriptSystem／ScriptModules 定向 CPU 测试通过。
- 日志测试覆盖准备期零输出、入口及模块源位置、中文和花括号作为普通内容、事件／Stop、
  宿主级别过滤、严格字符串契约、超限后继续变换实体以及下一次调用恢复额度。
- 既有真实 demo 玩法用例确认收集只记录一次，后续动画、暂停／单步和脚本换代不重复记录。
- Debug 全目标、Release app 构建通过，无新增编译警告；完整 992 CPU／171 UI 通过，
  一个既有轮询回退用例按平台条件跳过；Shader 构建契约与模块边界通过。
- 只读审查无生产阻塞；补齐 demo 日志捕获的 Logger 自建／清理配对，避免测试遗留全局状态。
  修正后 11 项 Logger／脚本日志／真实 demo 用例以乱序连续运行两轮，均通过。
- 日志：`/tmp/comet-auto3-019-{red-build,red,build,cpu,release,regression,final-build,log-isolation}.log`。

## 限制与后续

这是普通 Info 诊断，不是日志级别 API、实时变量查看器或跨实例／跨帧的全局限流。
每帧不断输出仍有成本，项目应在状态变化时记录；单条超限是省略而非截断，避免切断 UTF-8。
不重复 GPU／性能测量，也不把 CPU 日志捕获说成完整 App／Play 人工交互验收。

本项前还复核了模块失败恢复：普通缺失／改名／删除补回已覆盖；符号链接仍不受支持。
“被拒绝链接替换为普通文件”缺少自动重试，需要同时处理词法依赖和不跟随链接的签名，
当前没有实际消费者，本轮未为此扩建文件系统或失败请求缓存。
