# 025 玩家运行中改键与 Play 设置面板

## 背景与实际效果

024 只在启动边界读取玩家文件。本项让 Editor Play 在不重启场景的情况下修改个人绑定，
运行或暂停时都可打开 Viewport 工具栏的“输入”。App 已能读取相同文件，但独立 App 的设置界面留给 026。
不修改项目默认动作、场景或编辑历史，不将本项等同于整个输入扩展完成。

```text
原来：个人文件 → 下次启动 → 整份安装 InputActions

现在：Play 输入面板 → 稀疏候选 → 宿主保存个人文件 → 请求重绑定
                                                ↓
                          下一次 prepare 按身份替换绑定 → System / Lua
```

## 运行时只替换绑定

原有 `set_input_actions` 仍要求 Runtime 停止。新增入口在活动运行域且未执行 System 回调时可用：

```cpp
Result<void, Error> SceneRuntime::rebind_input_actions(InputActions actions) {
    if(m_executing || !is_active())
        return Result<void, Error>::failure({"Input rebinding requires an idle active scene"});
    if(auto requested = m_input.request_rebind(std::move(actions)); !requested)
        return Result<void, Error>::failure({requested.error()});
    return Result<void, Error>::success();
}
```

候选须有持久身份，动作集合、名称、类型、所属组及组定义不能改变。允许绑定修改、禁用、恢复和重排；
候选不直接覆盖当前工作缓冲，最后一份有效请求在下一次 `prepare` 应用。无效请求不清掉已排队的合法请求，
改回当前映射会取消待处理变更；Stop/reset 取消尚未应用的请求。

`RuntimeInput::apply_rebind` 按 UUID 找原动作及绑定。未改绑定沿用待固定步消费的采样、路由和未完成基线，
改动／新增绑定只建立自己的新基线。不能简单调用 configure/reset，否则其他动作在零固定步帧积累的点击也会丢失，
动态启停的 gameplay／调色组还会被错误重置。

```cpp
if(old != previous->bindings.end() && *old == value) {
    pending[index][binding] = m_pending_samples[previous_index][old_index];
    routes[index].set(binding, m_routes[previous_index].test(old_index));
    baselines[index].set(binding, m_binding_baselines[previous_index].test(old_index));
} else {
    baselines[index].set(binding);
}
```

新基线与原路由获权逻辑合并：按钮不重放按下／松开，位移不重放旧 delta；电平仍可反映当前状态。
基线必须等实际授权和设备可用才消费，不能在 `nullptr`、鼠标撤权或手柄断开时提前算完成。
普通／固定阶段的既有按钮电平保留，因此移除正在按住的绑定仍能释放旧动作。

## 面板与宿主分工

新增独立 `comet_ui` 静态目标，`PlayerInputPanel` 仅依赖引擎输入值和 ImGui，不依赖 Editor、Project、文件或 Engine 实例。
面板拥有稀疏草稿，以 `take_request` 交出一次候选，宿主用 `complete` 返回结果；未增加回调注册或全局事件。
翻译表由宿主借给当帧，中文沿用现有 YAML。

- 仅编辑已有动作／绑定的控制、倍率、死区及禁用状态；创建动作／绑定仍在项目默认配置面板。
- 恢复默认删除补丁，未编辑字段继续继承项目；未知／类型变化记录显示诊断，不自动清洗。
- 控制名称沿用 InputActions；非键盘控制从列表选择，键盘录入直接读取物理帧，保留左右键和 macOS Ctrl／Cmd 身份。
- 非法编辑不替换有效草稿；文件保存失败不发布候选，保留草稿可重试；应用成功才关闭。
- 面板及关闭帧阻断原 Gate。Esc 先取消录入，再关闭面板，不穿透为 Stop；失焦或采样 interruption 变化取消录入。

Editor 负责打开时加载文件会话、应用时 resolve → save → rebind，以及场景替换时关闭旧会话。
文件保存后若运行域意外拒绝请求，明确报告“已保存但未应用”，不伪称磁盘回滚；正常入口保持原动作 schema、活动运行域和主线程边界。
面板不自动暂停模拟，也不创建暂停／恢复音频分支。成功关闭后的一个 UI 帧仍负责关闭 modal，宿主始终调用 render。

## 021 至 025 架构回顾

目录与职责没有把玩家设置塞进 Project 或 Scene：身份是 common 值；定义、覆盖、文件和采样在 input 中分层；
UI 是独立依赖方，Editor 只编排宿主边界。021 的控制选择与 023 的稳定身份被实际复用，022 的源码操作不与玩家设置混合。
运行时不读文件，不依赖 UI，也不因改键重新启动 System。原按键授权、暂停和固定步协议仍只有一份。

仍有两项真实后续：App 复用 UI 时需把现有 ImGui 后端移到共享层，并保留场景像素及正确 GPU 同步；
项目默认与玩家面板的控制选择可以按实际重复提取共享控件，但不能把两种草稿和保存流程强行合并。
025 未改 GPU 后端，不提前制造 UI Manager、输入事件总线或通用设置框架。

## 测试与限制

- 90 项定向 CPU 通过，覆盖帧边界、保留未改历史、按钮释放、间接路由变化、延迟获权、暂停和生命周期。
- 首轮 24 项定向 UI 通过；审查补充最小化采样中断用例后，完整 197 项 UI 通过，其中面板 17 项、Viewport Play 8 项。
- 完整 1051 项 CPU 通过，1 项既有原生监听测试按平台条件跳过；Shader 构建契约、模块边界通过。
- Debug 全目标与 Release App 构建通过，无新增编译警告。日志前缀 `/tmp/comet-auto3-025-`。
- 测试驱动真实 ImGui 控件和 Runtime API，但没有声称人工体验验收；本项无 GPU 后端变化，未重复 GPU 全量测试。

不监视个人文件，不提供多人、组合键或手柄事件录制；App 设置入口、个人绑定关系反馈仍未完成。
