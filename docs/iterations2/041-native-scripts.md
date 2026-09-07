# 041：Native Script 生命周期与共享描述符字段

> 本项最初因额度停止而保留草稿，随后按用户“先完成 041 并推送”的授权恢复。
> 恢复仅完成本项，不开启 042；初次验证失败与修复证据保留在下文。

## 背景与前后对比

已有 System 是按场景执行的模块，还缺少按实体绑定、可通过编辑器配置的 C++ 行为。
本项接通脚本数据、运行实例和编辑／保存链路，并提供 app／editor 都能运行的旋转示例。

| 之前 | 现在 |
| --- | --- |
| app 的 System 保存两个方块 ID 并直接旋转 | Spin 参数是实体组件，每个实体有独立 NativeScript 实例 |
| Editor Play 默认场景仍静止 | Editor Cube 默认带 Spin Script，可 Play 调参、暂停／单步 |
| 未定义脚本字段入口 | 直接使用已有 ComponentDescriptor／PropertyDescriptor，不建另一套注册表 |
| 删除／重加组件可能被当成同一实例 | ScriptComponent 的瞬态代标识区分生命周期，不依赖内存地址 |
| 编辑／序列化可能复制运行对象 | Scene 只持有参数，运行实例由 NativeScriptSystem 独占 |

## 代码级链路

```text
CometDemo::SpinComponent 参数 + SpinScript 行为
              ↓ make_script_descriptor
ComponentRegistry
  ├─ PropertyDescriptor → Inspector / Serializer / Undo
  └─ create_script + script_instance_key → NativeScriptSystem
                                           ↓
                 on_start / fixed_update / update / on_stop
```

### 作者数据与运行对象

新增 runtime/native_script.h/.cpp，将 ScriptComponent、NativeScript、NativeScriptSystem 及注册辅助函数放在同一对文件。
不拆出 ScriptManager、FactoryRegistry、ScriptData 等多个单用途文件。

ScriptComponent 是作者组件的生命周期基础，只包含内部 uint64 代标识，没有虚接口、Scene 指针或脚本对象。
复制／复制赋值产生新代，移动保留代：删除重建、结构撤销恢复要重启实例；EnTT 存储搬移不能误判重建。
通过 PropertyDescriptor 修改 float／bool 等字段不会复制组件，因此不会重启脚本。
代标识不是持久 UUID，不进入场景文件，也不参与跨运行排序；标识分配由 engine 的原子计数器负责。

NativeScript 保存运行中的 C++ 状态，提供实体级 on_start、fixed_update、update 和 noexcept on_stop。
更新接收 Scene、Entity 和 System::Context，每次重新获取组件；不把组件地址作为长期绑定。
on_stop 不接收实体，因为实体或参数组件可能已经被移除；实现须支持部分启动后的清理。

### 复用现有描述符

ComponentDescriptor 只新增一对关联信息：create_script 与 script_instance_key；注册时要求成对存在。
make_script_descriptor<Component, Script> 复用 make_component_descriptor 的全部字段、添加／删除与快照协议，
再用类型化函数产生实例／读取代标识；不使用裸 offset、typeid 字符串或动态字段副本。

Inspector、Serializer、Undo 不需要增加脚本专用解析逻辑。场景文件仍保存稳定 component.id 和已声明参数，
加载／克隆时默认构造组件并写入字段，创建新的瞬态代；仅构造数据不会启动 NativeScript。
脚本类的内部状态不会进入 std::any 组件快照或 YAML。

### 发现、顺序与失效

NativeScriptSystem 构造时复制已注册脚本描述符，不借用可重分配的 Registry vector。
应用完成类型注册后再创建 System；运行中改变注册表不会隐式重写已有脚本类型。
绑定键是注册顺序、EntityUuid 和组件代；更新顺序按注册类型、UUID 排序，不依赖 EnTT 存储搬移顺序。
实例保留带 EnTT 代校验的 Entity 句柄，但不保留组件地址。

每阶段开始先停止失效实例，再从 Scene 当前组件建立缺少的实例；新实例 on_start 成功后才参与更新。
每次回调前重新检查实体与组件代，因此前一个脚本删除自身、删除后一个实体或替换组件都不会让后续回调访问旧数据。
本阶段内新增的组件在后续阶段发现，不一边遍历脚本回调一边递归启动无限新对象。
失效对象在回调返回后清理，避免脚本在自己的方法执行中被销毁。

全体 stop 按绑定顺序逆序清理，部分启动失败也会清理已创建实例；空工厂明确报错。
错误沿 SceneRuntime 停止并传播，不尝试继续已经部分修改的场景，也不自动回滚游戏数据。
查询当前仍按“实体 × 已注册脚本类型”扫描，绑定查找使用有序 map；未预建 ECS 调度图或事件总线。

### 示例项目模块

新增 demo/scripts.h/.cpp 和 comet_demo 静态目标，app／editor／测试共同链接；engine 不依赖该目标。
SpinComponent 的 speed／enabled 在 demo 定义，SpinScript 在 fixed_update 读取最新参数并旋转 Transform。
app 只保留 DemoCameraSystem，不再由宿主保存方块 ID；两个方块各带正／反转速度。
Editor 使用同一注册表，默认 Editor Cube 带脚本；Edit 不运行，Play 在克隆场景运行，Stop 恢复原件。
其他自定义脚本按同样模式由项目注册；旧场景不会被隐式加上 SpinComponent。

## 架构价值

- 组件是作者数据，脚本实例是运行对象，System 是阶段编排者，三者没有混成一个可序列化大对象。
- 字段注册、编辑、克隆和撤销使用同一描述符，避免脚本属性与 Inspector 各自定义一套 schema。
- 瞬态代解决真实的删除／重建生命周期问题；不是仅为包起字段而加基类。
- demo 成为共享项目内容模块，示例旋转行为没有进入 engine，也不让 editor 依赖 app 可执行程序。
- 固定更新、输入门控、暂停和错误清理继续复用既有 Runtime，不复制第二套执行循环。

## 测试结果

新增 11 项 NativeScript 测试，覆盖稳定顺序、正反生命周期、字段事务／撤销／序列化、组件同帧删除重加、
阶段动态增删、自删／删除后续实体、启动／更新异常、复制／搬移身份、结构快照恢复、无效注册／空工厂、Play 克隆与暂停单步。
首次完整重编译发现既有 Config 测试忽略 nodiscard 返回值的警告，本项改为显式丢弃，未修改其判断逻辑。

首次自动迭代停止时的实际结果：

- Debug／Release 构建通过；11 项 NativeScript 重点测试通过（/tmp/comet-041-focused.log）。
- 完整 Debug **失败**：unit_testing 和 render_graph_sync_validation 各 120 秒超时，其余 4 个 CTest 通过，合计 247.80 秒。
  两次采样都停在既有 RenderDiagnosticsGpuTest.EngineReportsCpuWallPhasesAndActualSceneGraphTimings 的
  MoltenVK／CAMetalLayer.nextDrawable 路径，未确认根因，不将其归因于脚本或直接忽略。
- Release 全量验证启动后因额度停止被主动终止，**没有完整结果**。
- 日志：/tmp/comet-041-debug.log、comet-041-release.log、comet-041-unit.sample.txt、comet-041-sync.sample.txt；
  构建日志 comet-041-verified-build.log、comet-041-release-build.log。恢复后先复核验证，不直接提交草稿。

### 恢复验证与链接边界修复

再次单独运行渲染诊断测试仍无法结束。临时记录 update 计数发现超过 1,500 帧，说明不是固定卡在 Metal：
第 3 帧的原生关闭请求未生效。紧接 `glfwSetWindowShouldClose` 读取错误得到 **65537 / GLFW_NOT_INITIALIZED**；
同一回调改由 engine 内的 `Window::request_close()` 调用后正常结束。进程同时报告宿主与 engine 重复定义 GLFW Objective-C 类。
这些证据指向同一进程包含两份静态 GLFW，各自维护初始化状态，而不是 NativeScript 更新阻塞 GPU。

修复保留原有原生接口测试，不以替换测试调用绕过问题：

- 根 CMake 通过 GLFW 官方 `GLFW_LIBRARY_TYPE=SHARED` 选项统一动态库；不修改第三方源码，也不把其他依赖全部改成共享库。
- 删除 unit_testing 强制使用安装 RPATH 的配置，构建树路径由 CMake 根据链接目标生成。
  切换动态库后的首次验证曾因旧 RPATH 找不到 libglfw 中止，此处一并解决，不依赖本机复制库或环境变量补丁。
- 新增 `WindowTest.HostAndEngineShareNativePlatformState`：宿主原生关闭／取消与 engine 查询双向一致，且没有初始化错误。
- 临时计数、错误输出及关闭兜底均已移除；原渲染诊断用例保持不变。

最终 Debug／Release 构建及各 6 个 CTest 通过。主单元集共 593 项，592 通过，1 个故意缺失屏障的对照仅在专门同步验证中运行；
专门同步验证 25 项、WSI 恢复 10 项，以及 pipeline cache、Shader 构建契约和 render profile smoke 均通过。
格式检查和 `git diff --check` 通过。完整结果见两套构建目录的 `Testing/Temporary/LastTest.log`。
本机验证平台为 macOS／MoltenVK，Linux 结果以推送后的 CI 为准；未将自动化测试冒充人工 UI 验收。

共享 GLFW 是进程状态唯一性的部署约束，分发程序时需要携带对应动态库；当前没有新增安装打包系统。

## 验证用法

打开 editor，选中 Editor Cube 查看 Spin Script。Edit 修改 Degrees per second 可保存／撤销；进入 Play 后开始旋转。
Play 调整速度或 Enabled 应立即影响后续固定步；`||` 暂停，`|>` 只前进一步；Stop 后恢复 Edit 的参数与变换。
保存带脚本的场景并重新打开应保留作者参数，不保留上一轮运行实例。旧场景可通过 Add Component 添加 Spin Script。

## 限制与后续方向

这是编译进项目的原生 C++ 脚本，不是脚本语言解释器；不支持 C++ 二进制热重载、外部程序集或自动扫描源码。
项目须在 app／editor 启动前注册相同组件类型；未声明字段的运行状态只属于 NativeScript。
组件构造／析构及工厂应遵循普通 C++ RAII；跨实体依赖需显式设计，不能依赖创建顺序以外的偶然存储顺序。
目前主线程串行、阶段扫描；大量脚本的查询增量化按测量再处理，未声称完成并行 ECS System 调度。
下一项 dirty Transform；物理、音频和角色碰撞声音 demo 仍未完成，阶段 6 不能在此标为结束。
