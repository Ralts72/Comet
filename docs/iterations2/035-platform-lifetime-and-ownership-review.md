# 035：平台生命周期、面板单一状态与阶段性架构审查

## 背景与验收项

本项是每五步一次的架构审查，重点覆盖最近的 forward／诊断链路及其依赖的窗口、UI 和关机路径。
032 和 034 的长序列 GPU 回归先后在 Cocoa 平台初始化停滞；034 的一次完整 Debug／Release 回归虽通过，
20 次重复测试仍在 120 秒超时。不能把重跑成功当作生命周期设计正确的证据。

同时新增 Render Stats 暴露了既有 View 菜单的两份可见状态：面板关闭按钮只改面板，菜单仍认为它开着，
再次点击菜单先把菜单自己的 bool 关掉，往往要点第二次才能打开面板。

验收目标：窗口销毁不结束整个 GLFW 平台，重复创建窗口保持平台有效；可见状态单一来源；完整回归和重复 GPU 验证。
阶段 5 的代表场景性能测量另列下一独立验收项；本次不凭架构审查直接宣布阶段 5 或 6 完成。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| Engine 调 glfwInit，Window 析构却调 glfwTerminate | Window 实现内的进程级平台 owner 统一初始化／终止 |
| 销毁一个窗口可能连带销毁其他窗口 | Window 只调用 glfwDestroyWindow 销毁自己 |
| 每次 Engine 重建重新进入 Cocoa 初始化 | 首次窗口初始化一次，正常进程退出统一终止 |
| 测试自行管理 GLFW，甚至重复终止 | 测试使用生产 Window，不建立第二套平台生命周期 |
| Window 可隐式复制原生指针 | 禁止复制，避免双重销毁 |
| 菜单维护 bool map 和 callback map | 菜单观察已登记的 EditorPanel，可见 bool 只在面板 |
| 六个仅用于转发可见性的 lambda | 六个明确的 add_panel 注册，没有事件总线 |
| 菜单与面板销毁顺序未表达观察关系 | 先解除 UI 回调／销毁菜单，再释放面板 |

## 代码级逻辑与设计理由

### GLFW 与 Window 不是同一生命周期

`window.cpp` 内部的 GlfwRuntime 是真正的平台资源 owner，不暴露到 engine 公共头文件，也不是为了收纳几个字段的包装。
Window 构造函数的函数局部 static 保证首次使用才初始化；正常进程结束时析构调用 glfwTerminate。
GLFW 失败初始化本身会清理失败状态，构造抛出 runtime_error；局部 static 初始化失败后不会被标记为已成功。
平台析构不调用 Logger，避免进程静态析构顺序依赖。

所有 Window 都继续在主线程创建、销毁、处理事件；这个实现不将 GLFW 变成线程安全接口。
调用方不能另行调用 glfwTerminate，否则会破坏平台 owner 的有效性假设。当前仓库的生产代码和测试已统一到这一条入口。
进程退出前，Application／Engine 仍正常释放 Renderer、surface 和 Window；平台保留不意味着 GPU 或窗口延迟到退出才释放。
这也不是可热卸载引擎 DLL、外部宿主共享 GLFW 的最终协议；将来出现这类真实需求，应增加明确的宿主平台契约。

每次创建窗口先调用 glfwDefaultWindowHints，再应用 Comet 的 Vulkan／可见／resizable 设置。
原因是 GLFW 不再反复 terminate，先前由重新初始化偶然清除的全局 creation hints 必须在每次创建时显式复位。
没有增加 visible 配置或改变现有启动显示策略；多窗口 owner 正确不等于已支持多窗口渲染编排。

原生指针是 Window 的唯一所有权资源，禁止复制构造／赋值；get() 仍只是现有平台互操作入口，不能由调用方另行销毁。

### Cocoa 证据与结论边界

034 原始采样栈为旧 `OffscreenSceneExportsSampledLayoutAcrossMsaaAndResize` 测试重建 Engine 后，
`Engine::Engine → glfwInit → _glfwInitCocoa → NSApplication::run`；尚未进入 Vulkan 创建或 RenderDiagnostics 查询。
本项删除重复初始化这条路径，同时修正可独立证明的多窗口错误所有权。
通过连续窗口／GPU 回归可以证明本实现未再次走该重复初始化路径，不能宣称覆盖所有 macOS 首次启动问题或修复了 AppKit 的所有行为。
第三方 GLFW 源码没有修改；依据是仓库中 GLFW 公开接口对 init／terminate、主线程与全局资源的说明。

### View 菜单观察实际面板

`EditorPanel::is_visible()` 暴露现有 bool；MenuBar::add_panel 接收 EditorPanel&，map 保存非拥有的 reference_wrapper。
map 继续按名称排序，不额外改变菜单顺序；同一对象重复登记是幂等操作，不同对象同名则抛出 invalid_argument，避免静默替换观察对象。
渲染菜单时直接读取 panel.is_visible()，点击时调用 panel.toggle_visible()；X 按钮、代码 set_visible 和菜单始终操作同一个状态。
034 为第二份菜单状态增加的 initially_visible 参数随重复状态一起移除，Render Stats 的初始隐藏只由自身构造函数决定。

Editor 明确在面板释放前销毁菜单，并先销毁承载 UI 回调的 ImGuiContext。
没有给面板共享所有权，没有新增通知广播；这是一对一的显示操作，不值得引入 EventBus。
真正的编辑命令仍通过 CommandHistory／事务入口；渲染准备和绘制回调仍需要既有同步时序，未混到 UI 通知里。

## 定期架构审查

| 维度 | 审查结果／处理 |
| --- | --- |
| 目录 | renderer 仍是渲染入口；scene 放提取／解析／提交，resource 放 GPU 资源。Shadow／PostProcess／Diagnostics 有独立执行与寿命职责，保留 render 下成对文件，不再加空的 data／manager 目录 |
| 职责 | Engine 编排主循环；SceneRenderer 编排图；Shadow／PostProcess 执行 pass；RenderDiagnostics 观察；Allocator 只生成报告，Editor 决定文件位置。平台／面板两处重复 owner 已修正 |
| 依赖 | 生产渲染不依赖 ImGui／editor；面板只观察 Engine 的诊断快照；GPU QueryPool 复用 FrameScheduler。当前未发现需要新建全局服务定位器或事件总线的理由 |
| 冗余 | 删除菜单 bool map、callback map、回调类型及六个转发 lambda；删除 Engine 和测试的重复 GLFW 初始化／终止；旧三方创建窗口测试替换为生产 Window 的契约测试 |
| 生命周期 | Device 比所有 GPU owner 活得久；Renderer 等待必要 GPU 完成再销毁 SceneRenderer／ResourceManager／Context；FrameSlot 保留实际资源，QueryPool 没有反向持有 scheduler，不成环；平台最后退出 |
| 故障 | 已修正窗口全局终止；WSI 创建失败恢复仍保持有限间隔重试。surface/device 丢失和运行时不兼容格式完整重建仍未实现，继续明确列为后续能力 |

SceneRenderer 的成员虽多，但分别对应实际 pass owner、frame scheduler、图计划、目标和重建状态；
本次没有为减少可见成员数量再套一个同文件大 struct，也没有把 Shadow、后处理和诊断合成万能 Renderer。
若未来图变为可注册的动态 pass，再基于真实图接口演进；当前固定 forward 链路不需要提前复制一套插件系统。

关机目前 Engine 和 Renderer 都包含等待调用；它们分别保护上层资产及独立 Renderer 使用的资源，不能仅按文本重复删除。
FrameScheduler 的完成 serial 以 fence 确认为依据；Vulkan-Hpp 增强重载会对 device lost 等错误抛出异常，
不能误判成“只打印日志后照常完成”。有限 timeout 的 Device API 返回表达仍有改进空间，当前 scheduler 使用无限等待，不在本项扩大修改。

路线图中的材质布局数和热加载分组描述已校准到实际四种布局／两组三 Shader；长期透明、IBL、跨队列、完整设备恢复项没有删掉。
README 只补平台及面板 owner 的实际架构，不充当本迭代索引。

## 测试结果

Debug／Release 完整构建成功，完整 CTest 均 5/5 通过，分别 13.82／12.06 秒。
单元套件 542 项（541 通过，专门同步对照在正常套件按设计跳过），独立同步套件 25 GPU 项、10 WSI 项及两个独立契约入口均通过。
此前超时的命令在新实现上执行 20 轮：25 GPU 项 ×20 = 500 次，通过，耗时 28.31 秒，没有重新出现 Cocoa 重复初始化停滞。
这是实现改变后的验证，不覆盖或删除 034 的失败记录。没有人工窗口视觉验收，也没有模拟平台首次初始化失败。
相关本机日志：`/tmp/comet-035-debug.log`、`/tmp/comet-035-release.log`、`/tmp/comet-035-repeat.log`、
`/tmp/comet-035-focused.log`（首次 UI 驱动失败）、`/tmp/comet-035-focused-verified.log`（修正后四项通过）。
已对本次 C++ 修改运行 `.clang-format` 并通过 dry-run／git diff --check；Shader 和第三方没有格式化或修改。

- 三个 Window 测试覆盖直接构造、多窗口隔离、连续 24 次创建销毁和 creation hints 复位；编译断言检查不可复制。
- UI 回归覆盖默认隐藏／程序关闭后一次菜单打开、真实 X 关闭后一次菜单打开、幂等注册及同名冲突。
- UI 测试首次使用了错误的菜单 item ID，未包含 ImGui 的 `##MenuBar` ID scope，未打开菜单；依据其实现修正测试驱动后四项针对性测试通过。
- 没有修改 GLFW／ImGui 第三方实现，没有把原始 Cocoa 超时删掉或改写成通过。

## 限制与后续方向

平台 owner 保持到正常进程退出，不提供显式重新配置平台／卸载重载入口；异常进程结束无法保证普通 RAII 清理。
同进程所有使用 GLFW 的代码都应遵守该 owner 协议。现有 get() 平台互操作仍需要调用方遵守借用规则。
MenuBar 的引用要求面板在菜单使用期间有效；不增加动态插件卸载／运行时面板注册取消能力。

下一项为可复现的 Release forward 场景测量：PBR、灯光、阴影、Bloom、多个对象，区分 CPU 图录制／整帧与 GPU，
再明确 RenderThread、并行录制及 Dynamic Rendering 的采用条件。该测量不是拿 validation 测试耗时推断用户项目瓶颈。
阶段 6 输入、System、脚本、暂停／单步、物理／声音和 Play/Edit 隔离的约定保持原样；未完成项不会因额度接近阈值而被划掉。
