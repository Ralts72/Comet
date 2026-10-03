# 009：验收外部补回模块后的自动恢复

## 背景与范围

002 已接通模块缺失路径监听，005 已证明通过 Project 创建模块能恢复消费者。
但用户也会直接在外部编辑器中新建 `.module.lua`，这条路径不能借助显式 Refresh 来证明。
此前测试分别证明了索引依赖变化、显式扫描和编辑器创建事务，没有直接覆盖外部写文件到当前 Scene 引用恢复的整条链。

本项只增加一条精炼的集成回归。首次运行即通过，没有发现生产缺陷，因此不新增服务、回调或重构。

## 前后对比

原有验证主要是：

```text
创建缺失模块 → 调用 create_script 或 refresh／scan → 加载脚本成功
```

新增回归通过真实临时项目与现有 EditorAssets 入口：

```text
外部原子写 first.lua／second.lua，均 require("shared")
  → EditorAssets::update 自动发现组件脚本
  → track_scene + restore_references 尝试加载
  → 两者失败，但 shared.module.lua 被记入依赖索引

外部原子写 shared.module.lua
  → EditorAssets::update 收到文件变更并发布扫描
  → 两个脚本均得到 modified 通知
  → restore_references 自动重新加载
  → ScriptSystem 启动并实际执行两个消费者
```

测试内不调用 `create_script`、`refresh` 或手动 `scan`。fixture 的首次基础项目初始化仍沿用既有公共准备逻辑，
不把正常初始化与后续自动恢复混为一谈。

核心等待与消费者断言为：

```cpp
const auto updated = assets->update();
assets->restore_references({.max_results = 8, .max_time = std::chrono::seconds(1)});
// 两个有效 Script 都恢复后，再启动既有 ScriptSystem。
```

不是只检查“存在一条文件通知”：两个实体实际各移动 7，证明新模块代码被运行。
同时检查模块没有 AssetRecord、没有 `.meta`，资产总数不增加，维持 source-only 边界。

## 设计理由

- 生产链已正确连接，就保留现有结构，不为了有生产差异另建 ModuleManager。
- 测试从文件系统到 EditorAssets、依赖索引、引用恢复、ScriptSystem，覆盖的是跨层调用关系。
- 通过既有公开接口验证，不增加测试访问器、手动注入通知或并行监听器。
- 文件通知等待有 3 秒上限、10 毫秒检查间隔，不靠无界等待或固定睡眠假定完成。
- 这是正确性验收，不是扫描性能测试，不引入大型合成项目或基准工具。

## 验证

首次单项测试通过，日志在 `/tmp/comet-auto3-009-first-targeted.log`。
两个首次缺失模块的警告是预期证据，恢复后没有用占位脚本掩盖错误。

```sh
build/tests/unit_testing --gtest_filter='EditorAssetsTest.SourceMonitorRestoresBothConsumersAfterExternalModuleCreation'
```

单项约 106 毫秒通过。随后与 008 同步接受完整当前工作区回归，已核对 CTest 日志中本用例实际执行：
962 CPU／159 UI 通过，1 项平台条件跳过；构建契约与模块边界通过。
Debug 全目标、Release app 构建及 3 项 GPU 生命周期冒烟均通过，详细命令见 008。
本项不改生产源码，不为同一份工作区重复所有昂贵验证。

回归证据为 `/tmp/comet-auto3-008-final-regression.log`；它覆盖的是最终工作区，
包含本次独立提交的测试，不只覆盖 008 文件。首次和最终测试均通过，没有重跑掩盖失败。

## 手动验证

1. 在测试项目中创建两个组件脚本，顶层都 require 同一个尚不存在的模块，将它们分别挂到实体。
2. 打开场景，Log 应报告缺失模块。
3. 在外部编辑器中补上模块并保存，不点击 Project Refresh。
4. 稍候文件变更合并和引用恢复，Play；两个实体应都使用模块中的行为。

自动测试使用真实临时文件与当前平台的文件监听路径，但没有模拟 Finder／外部 IDE 的完整 GUI 操作，
也不代替跨平台文件系统、完整 app／Play 音画交互的手工验收。

## 限制与后续

不新增模块改名、删除或多文件导入包，不为模块生成资产身份。
不扩张 app 的源码监听；独立 app 仍在项目加载时消费同一模块加载能力。
README 与路线图已描述自动恢复行为，本项仅补直接证据，不重复添加一份功能说明。
