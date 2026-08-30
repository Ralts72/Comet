# 036 阶段 3 → 阶段 4 架构复盘

## 目标

在资产系统闭环、Viewport 模式化请求刚建立的阶段切换点，检查最近新增代码是否出现：

- 同一状态被多个对象保存；
- UI、资产、渲染或线程职责越权；
- 只有测试使用的兼容入口；
- 为单个功能提前建立服务类或目录；
- 文档已经落后于真实生产链路。

本次复盘不以缩短文件或统一命名为目标，只处理有明确所有权问题或真实冗余的部分。

## 复盘后的资产所有权

| 状态/行为 | 唯一所有者 | 非所有者如何使用 |
| --- | --- | --- |
| AssetRecord、路径索引、依赖图、revision | AssetDatabase | AssetManager/Panel 只查询 |
| Runtime Handle 缓存 | AssetRegistry | AssetManager 发布，SceneResolver 解析 |
| Worker task、candidate、pending revision | AssetManager | TaskScheduler 只执行闭包 |
| 文件树观察基线 | AssetSourceMonitor | Editor 只 poll/acknowledge |
| Mesh 派生缓存 | MeshImportCache / `.comet/cache` | Importer/Manager 可重建 |
| 最新扫描诊断的 UI 展示 | ProjectPanel | Editor 通过返回值转交 |
| source + sidecar 移动事务 | AssetManager | ProjectPanel 只提交 Handle/目标 |

这些边界没有要求每一行职责都成为一个类。`AssetManager` 当前约 1500 行，但其中 load、后台 candidate、Owner Thread 发布和移动都围绕
“数据库记录到 Runtime Registry 的一致提交”这一协调职责。现在仅有一个项目文件操作，抽出 `AssetOperations` 只会增加转发和共享内部
状态。等创建、复制、删除或批量移动至少有第二组共享事务规则时，再提取独立操作组件。

Importer、cache、serialization 继续按外部格式解析、派生数据、Comet 自有格式分目录；不把 MeshData/TextureData 再迁到宽泛 `_data`
目录。`ImportInputSnapshot` 是 Mesh/Texture 共用的输入一致性契约，保留在 `asset/import/` 合理。

## 删除重复 AssetScanReport

复盘前：

```text
AssetDatabase current snapshot
  + Editor::m_asset_scan_report
  + ProjectPanel::m_scan_report
```

Editor 的副本只用于初始化 ProjectPanel，之后每次手动刷新、自动监视和移动又同时更新 Editor 与 Panel。它不是业务真相，也没有其他
消费者，容易在新增入口时漏同步。

复盘后初次 scan report 作为局部值移动给 `setup_panels()`，运行期每份报告直接返回 ProjectPanel。AssetDatabase 仍是资产真相，
ProjectPanel 只拥有最后一次诊断的展示快照；Inspector 通过事件失效，不读取 report 副本。

## 收回输入策略的渲染泄漏

第 035 步最初把 `ViewportInputPolicy` 放进 Engine 的 `ViewportRenderRequest`，但 Renderer 和 SceneResolver 都不消费它，运行时也没有
Input System。这会让渲染契约提前知道 `EditorCamera`/`RuntimeScene` 输入策略，却不能落实焦点或转发行为。

复盘后 ViewportRenderRequest 只保留 Renderer 真正消费的字段：

```text
visible + requested render size + camera source + optional explicit camera
```

输入策略将在出现真实消费者时放到 Editor 交互路由：editor camera controller 消费 Edit 的 image rect/hover/focus；Runtime Input
System 消费 Play 的焦点授权。SceneRenderer 永远只接收 RenderSubmission。这样不是删除未来能力，而是避免“先有枚举、后找消费者”。

## SceneResolver 单入口

新增完整 request 后，旧的 `resolve(RenderScene, size)` 只剩单元测试调用。保留它会让生产代码与测试使用两套相机选择入口，并允许未来
调用方绕过显式 Camera source。复盘删除旧重载，测试通过一个小型 `runtime_view(size)` 构造 ScenePrimary 请求；Renderer 与测试现在
验证同一入口。

`ViewportRenderRequest` 继续与 RenderScene 的 Camera/Item 值类型放在同一头文件。它们规模小且共同组成 SceneResolver 输入，当前没有
理由再创建 `viewport_request_data.h`。若多视图请求之后包含独立显示策略、标识和生命周期，再按完整子模块整理。

## 文档校正

`docs/architecture/asset-pipeline.md` 原先仍写着 Texture 后台导入和文件监听属于后续工作，已经落后于第 026、027、031、032 步。
本次同步为当前链路：

- Mesh/Texture 后台刷新；
- ImportInputSnapshot 前后捕获与 Owner Thread 复核；
- AssetSourceMonitor 自动扫描触发；
- ProjectPanel 移动回调与 AssetManager 文件事务；
- scan report 的唯一展示所有者。

README 和第 035 步文档也移除未消费输入策略的描述，路线图把输入焦点保留为真实 controller/Input System 接入时的待办。

## 验证

- CodeGraph 检查 AssetManager/Database/Monitor/Editor 调用路径与 Viewport 请求传播路径；
- 精确搜索确认 `m_asset_scan_report`、`ViewportInputPolicy`、`input_active` 和旧 SceneResolver overload 已无引用；
- ViewportRenderRequest 与 SceneResolver 定向测试；
- 完整 Debug 构建与 CTest。

## 下一步

阶段 3 的状态所有权不再重复，阶段 4 的请求也只包含真实渲染输入。下一步进入阶段 4B，先建立可纯测试的 Viewport 布局计算，分离
panel content size、物理像素 render resolution 和保持宽高比的 image display rect，再由 ViewPanel/Renderer 分别消费自己的结果。
