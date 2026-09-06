# 027：交换链失败后的暂停呈现与重试

## 背景与范围

本项对应阶段 5 WSI 无呈现恢复验收。旧实现知道 `oldSwapchain` 的退休规则，因此新建失败只能明确终止，
不能像 MultiTarget 那样恢复旧版本继续使用。本项让普通创建／图像枚举失败可以恢复，但不把 device/surface 丢失
和 runtime 格式变化的完整重建混入这一项。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| 有 oldSwapchain 时创建失败直接 fatal | 调用边界移走 active，失败返回结果并保留无呈现状态 |
| 创建成功但枚举 images 失败也 fatal | 候选 Generation 自动释放，下一次用空 oldSwapchain 重试 |
| recreate 返回 false 时恢复旧 dependent | 保持 dependent 已释放，成功后只重建一次 |
| begin_frame 默认有可 acquire 的交换链 | pending 时先检查重试时刻；尚未恢复就不开始 FrameSlot |
| 第二次 acquire OutOfDate 直接 fatal | 跳过当前帧，下帧重新尝试，不 reset 未提交的 fence |
| present 的增强重载可能直接抛异常 | 指针重载显式返回 Result，OutOfDate 可到达恢复逻辑 |

## 代码与状态变化

### Swapchain：寿命不是可用状态

`try_create_generation()` 先创建空候选 owner，再把 `m_active_generation` 移入局部 `retired`：

```cpp
std::shared_ptr<Generation> generation(new Generation(m_device, {}, {}, config));
auto retired = std::move(m_active_generation);
const vk::Result create_result =
    m_device.get().createSwapchainKHR(&create_info, nullptr, &swapchain);
```

这让旧 handle 活到调用完成，但 active 在调用边界已撤销。成功即交给候选 owner，随后枚举／包装 Image；
候选完全就绪才发布为 active。失败时局部 owner 按 RAII 清理，不会把旧对象放回 active。
空候选在调用前分配，也避免创建 Vulkan handle 后才分配 Generation 时的裸 handle 泄漏窗口。

`acquire_next_image()` 无 active 时直接返回 OutOfDate，不调用 Vulkan。
下一次创建看到空 active，`oldSwapchain` 为 null，而不是错误地再次提供退休 handle。
直接调用 Swapchain 的使用者仍负责 GPU 等待和 dependent 生命周期；高层由 SceneRenderer 完成这些步骤。

### SceneRenderer：一轮恢复只释放一次

`m_swapchain_rebuild_from` 是 optional 的最初配置：空表示正常，非空表示本轮恢复未完成。
它同时保留跨多次失败后的 compatibility 比较基线；另一个 time_point 只决定自动重试间隔。
没有再增加 RecoveryManager、事件系统或一个仅包装两个变量的新类。

```text
正常 → 保存旧配置 → 等所有 slot + present queue → 释放 dependent
  → 创建失败／零尺寸延期 → pending（不 acquire、不提交、不恢复旧 dependent）
      → 100 ms 后 begin_frame 再尝试
      → 仍失败：保留 pending，不重复 release
      → 成功：比较最初配置 → 重建 target / image state / overlay → 清除 pending
```

runtime 的 SwapchainTarget 在等待完成后释放；editor 的 MultiTarget 保留，仅其最终呈现 target 释放。
失败期间 `Renderer::prepare_frame()` 返回 false，原有调用链因此不运行 overlay prepare、Scene 提取或提交。
场景、资产及 Application 更新仍存在，但 UI 不继续绘制；不是后台继续呈现一张已退休图像。
显式 `recreate_swapchain()` 可立即重试，自动 begin_frame 路径间隔至少 100 ms。

present 后可能已经提交一帧，这条路径等待完成后尝试恢复，即使失败仍执行 `FrameScheduler::end_frame()`，
不会留下 active slot。acquire 失败则尚未 begin_frame，不能提前 reset fence。

### Queue：一个字符影响整个恢复分支

```cpp
// 之前：增强重载在 ErrorOutOfDateKHR 时抛异常。
m_queue.presentKHR(present_info);
// 现在：Vulkan-Hpp 指针重载返回 vk::Result。
m_queue.presentKHR(&present_info);
```

这仍是 C++ Vulkan-Hpp 接口，不退回裸 C API。测试首先暴露两个 present 用例抛出
`vk::Queue::presentKHR: ErrorOutOfDateKHR`，修复后才进入原有结果判断和新的恢复状态。

## 架构价值

- Swapchain 只负责代际句柄和创建结果；SceneRenderer 编排等待、依赖释放与后续重建，各自不反向拥有另一层。
- pending 直接表达当前不能呈现，不以“shared_ptr 非空，所以旧资源还能用”混淆寿命和 WSI 协议。
- 沿用 FrameScheduler 的提交／完成边界和 editor 的已有两个生命周期回调，没有增加全局事件或退休队列。
- 普通资源创建事务与 WSI 退休具有不同失败语义，代码不强行套同一套 rollback 模板。

## 测试结果

- Debug / Release 全量均通过：490 个原单元测试、独立 WSI 进程的 10 个测试，以及 PipelineCache 进程和 Shader 构建契约；4 个 CTest 项通过。
- WSI 10 个测试连续 20 轮（200 次）通过，无 VUID／Validation Error。
- runtime 直绘和 editor 式离屏 + 独立呈现 target 两种配置：真实 acquire、clear pass、submit、present。
  验证退休创建失败、images 枚举失败、空 oldSwapchain 重试、release/rebuild 次数、离屏 owner 不变、frame serial 与关闭。
- 创建失败替换会先真实创建候选让驱动退休旧交换链，再销毁候选并返回 OOM；不是只改一个 bool 却仍使用有效旧句柄。
- 连续 acquire OutOfDate 不提交也不重置未提交 fence；present 后创建失败正常结束已提交帧，再恢复呈现。
- 用独立测试目标重编译真实 `swapchain.cpp`／`queue.cpp`／`scene_renderer.cpp`，仅重命名四个 Vulkan 入口。
  测试通过 `vkGetDeviceProcAddr` 转发实际驱动；convert.cpp 同编译以使用内部未导出转换函数。
  生产类没有故障注入成员／构造参数／全局开关，单元测试主进程也不受替换影响；CTest graphics resource lock 防并行图形初始化。
- 首轮 8 个 WSI 用例中 present 两项失败，错误如上；修复后新增关闭测试，两套完整配置及重复回归全部通过。
  测试目标最初缺少 Image 完整定义及内部转换实现的链接依赖，均已补齐，生产 API 没有为此扩大导出。
- clang-format 与 git diff --check 通过。026 的 Linux CI `34059473777` 此时仍在运行，不声称远端完成。

## 限制和后续方向

- DeviceLost、SurfaceLost、初始交换链创建失败及 runtime 不兼容格式仍明确失败；没有重建 Device、Surface 或全套 Pipeline generation。
- 覆盖的是 Vulkan 创建／枚举结果失败；CPU bad_alloc、dependent 重建中途异常不在本次完整恢复范围。
- 100 ms 只限制 WSI 重试频率，不是统一的主循环帧率限制；主循环时间步／固定更新由阶段 6 处理。
- UI 生命周期通过等价的独立呈现 target 和真实 Vulkan 验证；本项未进行人工 ImGui 桌面操作验收，也未强制驱动改变 surface format。
- 当前保留 graphics slot + present queue idle 回退，不引入未经验证的精确 present completion。
- 下一项：多 pass 的资源状态声明、同步编排及真实 producer/consumer 验证；阶段 5 forward 场景和阶段 6 尚未完成。
