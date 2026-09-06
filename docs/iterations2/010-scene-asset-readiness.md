# 010：场景资源加载闭环与第二次架构回顾

## 背景

008／009 完成了放置模型和编辑引用，但启动代码只加载 demo 资产。保存文件虽保留新模型的 Handle，
重启后打开文档时，Runtime Registry 为空的非示例模型仍不可渲染。
另一个缺口是打开缺缓存的场景后补 Import：007 为未驻留模型只生成 Artifact，不知道当前 Scene 需要它。

本项验收：打开文档加载正常引用、保留坏引用；补导入后恢复当前场景资源；不每帧扫描、不隐式导入 glTF。

## 前后对比

| 职责 | 之前 | 现在 |
| --- | --- | --- |
| 引用发现 | 手工加载示例路径；没有场景引用收集入口 | ComponentRegistry 按属性元数据收集并去重 Handle／类型 |
| 类型分发 | editor.cpp 的引用赋值处理器自己 switch | AssetManager::ensure_loaded 统一类型核验、缓存命中和失败边界 |
| 场景替换 | 反序列化后替换 Scene | 先准备候选 Scene 的资产，失败只保留诊断，不删引用或拒绝整个文档 |
| 后台发布 | 调用方不能获知本批发布了哪些导入结果 | process_completions 返回成功发布的 Handle 值列表 |
| 缺失资源恢复 | 重开或另行手动加载 | 发布／刷新／显式纹理重导入成功后，重查当前 Scene 引用 |

## 代码逻辑

### ComponentRegistry::collect_asset_references

遍历现有实体和注册组件，只读取带 `asset_type` 的 AssetHandle 属性，忽略零 Handle。
结果为嵌套 `AssetReference{handle, type}` 的自有 vector，排序去重，不返回组件地址或数据库借用 span。
同一 Handle 如果被声明为两种类型，不合并掉冲突信息；加载时分别核验。
不要求字段可编辑或可序列化：当前 Scene 中的只读／运行时引用也可能需要资源。
方法使用现有 `Scene&` 查询接口，但不写入 Scene，不创建资源、不做文件读取。

### AssetManager::ensure_loaded

先检查非零 Handle、数据库条目及期望类型，即使 Registry 已缓存也不能绕过类型检查。
之后分派至现有 load_mesh／load_material／load_texture；捕获创建路径的标准异常并记录 Log，返回 false。
该方法不建立第二份缓存，也不代替返回具体对象的类型化加载函数。
Mesh 沿用 Artifact-only 路径；Material 继续加载自身引用的 Texture。

### 编辑应用触发时点

- SceneDocument／EditorSceneSession 共用的现有 replacer 在激活候选 Scene 前准备资源；没有新增回调类型。
- 资产数据库刷新提交后重查，便于修复源文件、补回引用或清理缓存后的显式操作。
- on_update 接收非空导入发布列表后重查；没有发布时不遍历 Scene。
- 显式纹理重导入成功后重查，让此前因纹理加载失败而缺失的场景材质也能恢复。

`prepare_scene_assets()` 只编排收集和加载并汇总缺失数量；Scene 文档、History、Selection 不因加载失败被清空。
AssetManager 不追踪 Scene；未被当前场景引用的 Mesh 导入仍不会隐式分配 GPU。
引用赋值处理器改为复用 ensure_loaded，删除应用层的同类类型分发。

### 发布事实的准确含义

返回值只描述本次 owner 调用内的成功发布：Mesh 表示 Artifact 已原子发布，Texture 表示 Runtime 已替换。
检查、过期结果、解码失败不产生成功项。Mesh GPU 刷新失败时可以有 Artifact 成功项，不能把它误读成 GPU-ready。
这个事实列表在同一 owner 阶段消费，没有全局 EventBus、持久订阅或跨线程业务回调。

## 第二次阶段性架构回顾（006—010）

| 审查项 | 结论／处理 |
| --- | --- |
| 目录 | asset 下 import／artifact／serialization 仍对应真实阶段；scene 保持数据与描述符；UI 仍在 editor。此项不增加生产文件或伪 SceneLoader，不做无语义目录搬迁 |
| 职责 | 描述符负责发现引用，AssetManager 负责准备资源，Editor 决定触发时机；SceneSerializer 保持纯数据持久化 |
| 依赖 | AssetManager 没有引入 Scene／ImGui 依赖；ComponentRegistry 只依赖资产类型和 Handle，不反向调用加载器；组合只发生在应用层 |
| 冗余 | 删除 editor.cpp 的引用加载 switch；共享 Registry 是唯一 Runtime 缓存，已有具体 load_* API 保留其返回对象的职责 |
| 生命周期 | 载荷、收集结果与发布结果都是值；SceneDocument/Session 的 replacer 在 AssetManager 销毁前销毁，渲染回调先解除；仍由 FrameSlot 保留在途 GPU owner |
| 事务 | 结构／属性修改走 Scene 历史；资源准备不制造 Undo，坏引用不被默默修正；旧 Runtime 在刷新失败后仍保留 |
| 后台边界 | Worker 只产 CPU 候选，owner 发布后返回事实；不把 Scene／UI 指针传入 Worker |
| 未立即改动 | editor.cpp 仍较长，但剩余部分是 bootstrap／窗口生命周期／跨模块编排；不通过包字段减少行数。AssetManager 后续随任务背压与发布预算再拆工作队列职责 |

TaskScheduler 待处理队列和 GPU 发布预算仍是路线图阶段 3 的明确待办；本项没有拿一次性加载冒充 streaming。
Gizmo 增加旋转／缩放时再依据真实算法边界整理交互代码，不先引入每模式一个转发类。

## 测试结果

- Debug／Release 完整构建，各 **386 tests** 通过；clang-format 与 diff 检查通过，图形回归无 VUID。
- 收集测试验证重复引用去重、不同期望类型保留、空引用忽略以及结果不借用已销毁实体。
- ensure_loaded 测试覆盖无效身份、错误类型、缺 Artifact、GPU 创建异常、失败后重试和缓存命中。
- SceneDocument 集成测试保存带共享 Mesh／Material 引用的场景，以全新 Registry／AssetManager 打开，确认资源可解析，
  Mesh 只创建一次，重复打开不重建；故意损坏原 glTF 仍可使用先前发布的 Artifact。
- 缺失引用测试验证初次失败不改 Scene，后台 Import 只发布 Artifact，调用方再准备当前 Scene 资源；
  缺失材质仍保留，失败重导入不发成功项，也不删除旧 Mesh。
- 异常注入暴露测试工厂的“下一次 callback”依赖 moved-from std::function 为空的假设。
  改用 `std::exchange(callback, {})` 明确一次性消费后，异常与重试测试通过。
- 集成测试用 fake GPU 工厂确认加载/缓存契约，并未实际重启桌面进程或截图；已有 Vulkan 测试验证渲染资源生命周期。
  本项 CI 以推送后实际运行结果为准，不提前宣布 Linux 通过。

## 限制与后续方向

- 当前对一批发布重查整个当前 Scene，而不是增量维护需求图；无事件时不重查。大场景的引用索引和上传预算后续独立验收。
- 同步准备大批首次使用的 Texture／Material／GPU Mesh 可能有停顿；没有隐藏这条同步路径。
- 对不支持的 Shader／Scene 资产类型报告失败，未来新增加载契约时再扩展。
- 坏引用可以打开修复，不代表场景所有对象均可绘制；报错在 Log，Inspector 保留原 Handle。
- 下一项是 Gizmo 本地轴／吸附，再推进旋转／缩放。下次定期回顾为 015 或阶段 4 核心完成边界。
