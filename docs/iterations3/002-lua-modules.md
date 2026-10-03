# 002：项目 Lua 模块与关联重载

## 背景与本项边界

001 已能在更新边界替换同一 Script 的活动实例，但每个脚本仍只能是一个独立源文件。
demo 的目标脚本负责加分，旋转脚本监听分数；二者重复约定会话键和事件名。
直接开放标准 Lua `require` 会让磁盘加载、模块缓存和热重载依赖落到另一套体系中，
也无法保证一个共享模块的消费者使用相同版本。

本项沿用 Script、AssetManager、AssetDatabase 和 ScriptSystem，接通源码依赖与整组切换。
不添加 ScriptManager、包管理器或任意 Lua 状态迁移。独立 app 和 Editor Play 使用相同加载能力，
源码监视仍属于编辑器工作流。

## 1. 组件脚本与模块是不同角色

之前组件脚本自己写会话键和通知：

```lua
local score = (comet.session_get("demo.score") or 0) + 1
comet.session_set("demo.score", score)
comet.emit("demo.score_changed", score)
```

现在项目内 `demo_score.module.lua` 提供复用逻辑：

```lua
local score = {}
score.changed_event = "demo.score_changed"

function score.get()
    return comet.session_get("demo.score") or 0
end

function score.add(amount)
    local value = score.get() + amount
    comet.session_set("demo.score", value)
    comet.emit(score.changed_event, value)
    return value
end

return score
```

`collect_goal.lua` 调用 `demo_score.add(1)`；`spin.lua` 使用相同事件名和 `get()` 初始化当前分数。
玩法仍在项目内，不向引擎添加计分概念。

```lua
local demo_score = require("scripts.demo_score")
local script = {}
script.events = {[demo_score.changed_event] = "on_score_changed"}
return script
```

点分名称从项目 assets 根解析为 `scripts/demo_score.module.lua`，不强制模块都在 scripts 目录。
普通 `.lua` 仍是有 Handle／`.meta` 的可挂载 Script；`.module.lua` 仅是源文件依赖，
不生成 `.meta`，不占资产类型和 Registry 项，也不能误挂到实体。

Project 当前把模块显示为普通文件；创建、改名和删除由外部编辑器完成。
New Script 和资产 Rename 会拒绝把组件改成模块后缀，Finder 导入也不把模块作为独立资产导入。
多文件 Lua 导入包、专用模块创建菜单不在本项中；依赖模块的组件请直接在项目 assets 内编写。

## 2. 加载一次捕获源码，实例不再访问磁盘

单文件入口仍保留给无项目依赖的内存／独立脚本；资产路径改用：

```cpp
Script::load_group(assets_root, relative_paths);
```

返回值按输入入口顺序包含 Script；失败返回诊断和每个入口已经尝试的模块路径。
私有 `SourceSet` 捕获本批真实读取的字节，所有入口共享只读快照。
初始化执行顶层代码时收集传递依赖，捕获完成后冻结。

```text
项目根与入口路径
  → 捕获入口／模块字节
  → 顶层执行，收集 properties、events 和 require 闭包
  → 检查整批输入是否仍然相同
  → 不可变 Script
      → 每实体独立 Instance／Lua VM／模块 table 缓存
```

同一实例重复 require 返回同一张表，不同实体的表不共享。
Scene 共享分数仍通过 session 接口显式保存；模块复用不是另一个全局可变状态容器。
旧实例即使磁盘变化，也继续使用它持有的旧字节。

运行回调只能再次 require 初始化时已经加载的模块，不能首次发现新依赖；
一个入口也不能借用同批其他入口捕获的模块。这使运行阶段没有隐藏文件读取。
循环、缺失文件和非 table 返回值都有明确诊断。

路径只允许点分标识符，不开放 package／io／os／原生加载；不允许路径穿越或符号链接别名。
拒绝别名是为了让逻辑路径、依赖索引和文件通知使用同一个身份，不是新增虚拟文件系统。
单文件限制 1 MiB，整批 8 MiB／256 文件／128 入口，每入口 64 模块、深度 16；
模块继续受已有 VM 内存和指令预算约束。该运行环境不是不可信代码的安全沙箱。

## 3. 资产发布复用现有依赖索引

之前 `load_script()` 逐个加载文件，刷新也逐个替换。现在首次加载与刷新共用
`AssetManager::publish_script_group()`，实现放在 `asset_manager_scripts.cpp`，
仍是同一个 AssetManager 的职责，不额外建立服务或缓存 owner。

```text
源文件通知 → AssetDatabase 标记依赖脚本变化
  → 以变化资产为种子，按旧依赖扩组
  → load_group 捕获候选
  → 若新依赖连接其他已加载脚本，扩组后整批重读
  → 更新 import dependency 索引
  → 验证输入字节、revision 和 Registry 身份
  → 在 owner 线程无外部回调地连续发布关联 Script
```

成功只保留实际依赖；失败保留旧依赖与已尝试路径的并集。
因此模块暂时缺失或语法错误不会替换旧资源，补齐后仍能通过原有通知链恢复。
首次加载失败也登记缺失路径，让 SceneAssetReferences 有机会重试未解析引用。

依赖索引更新会重新读取文件签名，所以必须在更新之后再核对捕获字节。
否则文件刚好在准备期间变化，新的索引基线可能吞掉那次通知。
仅已证实过期的快照进入既有刷新请求队列；稳定语法错误等待下次文件变化，不每帧重读。
重试成功把相关 Handle 加入 `process_completions()` 的结果，沿已有编辑器引用恢复链通知消费者。

新消费者加入时不能无故重启旧脚本：

```cpp
if(previous && previous->has_same_sources(*script))
    continue;
```

比较的是入口字节及它自身的模块闭包，不是整个批次对象身份。
新加入的其他入口不应使字节未变的旧 Script 换指针。

## 4. 运行实例按旧、新依赖的关联组切换

001 只按 Handle 分组，现在共享模块的不同脚本也需要一起准备。
`ScriptSystem` 合并活动旧定义和候选新定义的依赖，再求关联组。
候选删掉某个 require 时，运行旧版本仍在使用它，不能提前丢掉这条边。

```text
synchronize
  → 清理已解绑／删除的实例
  → 收集新挂载的 pending 实例
  → 按旧、新依赖建立关联组
  → 为需要换版及同组 pending 实例全部预检
      失败：旧实例继续，pending 延后
      成功：逆序停止受影响旧实例 → 安装候选 → on_start
  → 启动剩余独立 pending 实例
```

旧实例沿用 001 的兼容参数过滤；新组件仍严格检查参数，不借热重载偷偷删除错误覆盖。
准备失败之前没有停止旧实例，也没有执行新的生命周期副作用。

失败缓存从单个候选弱引用变为完整输入快照：实例身份、候选弱引用和参数。
只有三者都没变化才跳过重试。修复参数、删除阻塞组件或恢复资产，都允许同一候选再次尝试。
Registry 缺失的活动脚本也保留为阻塞种子；即使其他旧实例已经删除，
新实例也不能绕过仍运行旧模块的缺失资产同伴。

暂停期间不切实例，单步／继续的更新边界应用候选；Edit 场景、文档和 Undo 不被回写。
`on_start` 的世界副作用不做事务回滚：执行失败仍由既有 Runtime 整体停止清理。
实例 self／模块表重建，Scene 会话、实体和物理状态不因此重开。

## 5. 验证与修复记录

本项覆盖模块缓存与隔离、传递依赖、冻结源码、缺失恢复、循环、非法路径、符号链接、
资源预算、候选依赖合并、独立组不受影响、暂停／单步和真实 demo 脚本。
资产／Runtime 联合测试还覆盖新消费者不重启同伴、pending 参数失败保旧、删除阻塞实例后重试，
以及删除已变化实例后，缺失同伴仍阻止新版 pending 启动。

初次定向测试的三个失败来自 demo 测试仍使用无项目上下文的 `Script::load()`；
已改用真实项目 `load_group()`，保留原有行为断言，没有为测试放宽生产模块解析。
新增测试中一次构建失败来自把需要编辑参数的 Entity 声明为 const，修正了测试局部变量。
最终交叉审查发现缺失资产不参与变更种子的漏洞，先补回归复现，再修复判断。

2026-10-03 最终执行：

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='ScriptModulesTest.*:ScriptGroupTest.*:ScriptAssetTest.*:ScriptSystemTest.*'
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure
cmake --build --preset app-release --parallel 6
```

Debug 全量构建、58 项定向测试、921 项 CPU 和 156 项无窗口 UI 通过。
1 项轮询回退测试因当前启用原生文件通知而跳过；Shader 构建契约、模块边界与 Release app 构建通过。
最终两次构建日志未见 warning／error；没有运行 Release 测试套件或 GPU／真实窗口验收。
本项不把 CPU／无窗口结果冒充完整画面验收。

本机日志为 `/tmp/comet-auto3-002-final-{build,targeted,regression,release}.log`。
缺失同伴修复前的红测试证据保存在 `/tmp/comet-auto3-002-missing-peer-red.log`，不作为仓库产物提交。

## 6. 手动验证

1. 打开 demo，进入 Play，观察旋转方块和既有目标交互。
2. 暂停，修改 `demo/assets/scripts/demo_score.module.lua`，把 `score.get() + amount`
   改为 `score.get() + amount * 2` 并保存；暂停中不应执行玩法更新。
3. 单步或继续，Console 应显示关联组切换。触碰尚未收集的目标后，
   分数增加 2、标记名使用同一分数，旋转方块按差值上升 0.8。
4. 制造模块语法错误，保存后旧组应继续；修正并保存后恢复重载。
5. Stop／Play 重开，Scene 会话分数回到初始值；测试结束恢复模块原表达式。

真实画面、音频和完整交互仍待人工确认。独立 app 需重新加载项目，不承诺自动监视源码。

## 7. 阶段性架构复核与后续

文件读取和依赖身份归 Script／AssetDatabase，发布归 AssetManager，
实例生命周期归 ScriptSystem，编辑器只复用已有源变化及引用恢复工作流。
未增加模块资产类型、第二套监听器、跨实体共享 VM 或全局模块可变缓存。
SourceSet 的只读字节与每 VM 的 Lua table 缓存是不同层次，不是两份玩法状态。

下一步按真实 demo 需要推进受控组件 API；模块编辑入口和多文件外部导入留在路线图独立验收。
Script 初始化仍同步且有预算，本项未宣称解决大型项目的后台编译或任意数量脚本加载。
