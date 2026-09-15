# 异常处理审查与迁移边界

审查日期：2026-09-15。范围为 `engine/src`、`editor/src`、`editor/editor.cpp` 和 `app` 的自有代码。

## 目标与当前状态

目标是移除自有生产代码的 `try/catch/throw`，不是把它们搬到入口或 helper。用户已确认暂不更换 yaml-cpp，因此仅其直接解析捕获暂缓。第三方库及测试故意注入的异常不属于本轮清理对象。

**自有异常语法迁移已完成，用户指定暂缓的 YAML 捕获除外；整体验收尚未完成。** 没有开启 `-fno-exceptions`，也不宣称标准库、分配或 Vulkan-Hpp 调用不会抛异常。入口不再兜底，未预期的依赖异常仍可能导致进程终止，而非有序关闭。

## 已完成的路径

| 位置 | 当前处理 |
| --- | --- |
| runtime/runtime.*、runtime/entry.cpp | 初始化、更新、关闭钩子直接返回 `Result<void, Error>`；Error 不依赖图形类型，入口无捕获，失败退出码为 1 |
| core/engine.* | run/tick 传播更新与呈现结果；RAII 复位运行标记 |
| config/config_loader.* | 文件读取、字段类型、枚举、范围、配置合并返回 Result；仅 `YAML::Load` 捕获 |
| common/json.* | simdjson 错误码直接转 Result；对象、数组、必需字段及标量类型校验均返回 Result；移除 Json::Error |
| asset/serialization、core/project.cpp、scene/scene_serializer.cpp | 完整 JSON 调用链传播失败；保留来源和字段路径；写入失败不发布半成品 |
| scene/scene.cpp、editor/src/scene/scene_commands.cpp | ScopeExit 负责实体、索引和命令恢复回滚；成功后 release，无捕获后重抛 |
| graphics/context.cpp、swapchain.cpp | 使用带输出参数的 Vulkan 查询接口，检查原生返回码；枚举处理 eIncomplete |
| graphics/device.cpp | 关闭等待直接检查 vkDeviceWaitIdle 返回码 |
| asset/artifact/mesh_artifact.cpp | 字符串长度、非有限顶点和发布校验直接返回失败 |
| editor/editor.cpp | 生命周期与 Shader 发布返回结果；模式克隆失败由会话报告，不再外层捕获 |
| editor/src/ui/shortcuts.cpp | 文件与绑定校验返回 Result；仅 `YAML::Load` 捕获 |
| asset/asset_manager.cpp、editor/src/assets/editor_assets.cpp | 首次加载、重载、编辑和材质依赖返回带码 Result；候选准备、引用赋值、Inspector 和 demo 消费者已接通，不把 DeviceLost 改成缺失引用 |
| core/window.cpp | 文件拖放 C 回调不再 catch；noexcept 防止非预期异常越过 C 边界 |
| render/line_draw_list.cpp | add_line/add_box/append 对顶点数量超限返回 false；Renderer 拒绝整批追加并记录告警，不把数量溢出改成隐式截断 |
| render/material.cpp | 数值 setter 对非有限值返回 false，保持值与版本；AssetManager 转成带属性名的加载错误，不发布残缺材质 |
| render/material_renderer.cpp、debug/debug_renderer.cpp | 材质准备/调试缓冲增长 DeviceLost 经 scene pass 返回 Renderer；停止 overlay 与提交，进入关闭状态 |
| render/scene/scene_renderer.cpp | resize 的 DeviceLost 经 set_render_view、Viewport::update、on_frame_ready 返回；普通失败保留目标和重试策略，设备错误进入关闭状态 |
| editor/src/viewport/viewport.cpp | 采样器由 Editor 在 setup_panels 中检查获取结果后传入；初始化错误原样返回 on_init，不在 Viewport 构造中抛异常 |
| render/render_context.cpp、renderer.cpp、core/engine.cpp | create 在局部准备依赖，交换链/场景目标错误原样向上返回，私有构造只接受完整依赖；Application 创建失败不进入应用钩子 |
| asset/database.cpp | 版本耗尽通过扫描诊断/Result 拒绝；单项更新先检查再改文件与索引，无变化操作不消耗版本 |
| asset/source_operations.cpp 的 move | 文件/元数据移动及扫描失败统一回滚；ScopeExit 保留提前退出清理，显式失败报告回滚错误，成功才替换数据库；无扫描捕获 |
| asset/source_operations.cpp 的 import_files | 文件系统调用检查 error_code 后返回失败；暂存与已发布文件共用清理出口，正常失败报告清理诊断，ScopeExit 保留提前退出清理；不再外层捕获 |
| asset/import/asset_task_queue.cpp、editor/src/render/shader_reload.cpp 的完成消费 | 业务失败由候选 Result/编译诊断交付；保留 future.get 同步与异常检查，删除外层捕获，不读取异常中断的候选；消费槽位/待处理状态清理保留 |
| core/task_scheduler.cpp、asset/import/asset_task_queue.cpp 的容量检查 | 正容量为内部构造前置条件；零容量使用 LOG_FATAL，未接入用户配置；背压/空任务/停止提交仍返回空，不作致命错误 |

关闭只尝试一次，关闭错误不覆盖主错误。关闭失败时保留 Engine/Diagnostics，由派生类先销毁依赖资源，再销毁基类 owner。该保证适用于显式 Result 路径，不包括未迁移的抛异常路径。

场景属性快照复用 `PropertyDescriptor::copy_value()`，另校验有限数；恢复不能使用受 editable/read_only 限制的编辑接口。候选 Scene 全部恢复成功后才交给调用者。

## 剩余清单

| 位置 | 必须完成的迁移 |
| --- | --- |
| config/config_loader.cpp、editor/src/ui/shortcuts.cpp | 用户指定暂缓的 YAML 解析捕获 |

复查范围为本页开头列出的自有生产源码。其余关键字命中为字符串内容，不是异常语句。线程启动失败仍由 ScopeExit 复用 shutdown 回收部分线程；第三方异常的全链路有序退出不在已证明的保证内。

## 验收规则

当前先收敛已迁移流程，不继续扩大语法清理范围。JSON 必需标量字段使用 read_field，场景内部共享 Context，避免多层纯转发；仍显式检查失败，不引入传播宏或隐藏错误状态。

- 可预期失败可观察，不吞错、不返回伪成功，不仅移动异常语法。
- LOG_FATAL 只处理代码不变量，不替代用户输入、资源加载或需要清理的设备故障。
- 检查真实调用者：原始错误、旧状态、回滚顺序和关闭次数都要保留。
- 清理代码不能因减少关键字而删除；不可恢复的分配失败不伪装成业务成功。
- 完成迁移后重新扫描生产源码；剩余 YAML 捕获单列，不将“测试通过”等同于“异常已清零”。
