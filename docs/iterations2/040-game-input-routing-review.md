# 040：游戏输入路由与阶段性架构回顾

## 背景与前后对比

037–039 已建立平台输入、System 时序和 Play 控制，但 Runtime 仍直接接收整个窗口的输入。
在添加游戏脚本之前，需要阻断 Inspector、工具栏、弹窗和窗口恢复过程中的编辑输入。

| 之前 | 现在 |
| --- | --- |
| Runtime 直接读取平台帧 | 平台帧 → Input::Gate → 普通／固定更新 |
| UI 是否应接收输入没有边界 | ViewPanel 判断有效 Play 画面，Editor 本帧显式放行 |
| 离开／重新进入可能继续按住旧键 | 离开发布释放，重新进入需数字按钮先松开再按 |
| 同一平台 serial 无法表达本地许可变化 | Gate 有自己的发布 serial，独立于源 serial |
| WSI 无 UI 帧可能沿用旧许可 | 宿主更新先默认关闭，只由本帧有效 UI 重新开启 |
| 最小化前输入余量可能恢复后补放 | 中断清除按下／位移，保留固定步仍需看到的释放 |
| Input 与 Runtime 各写瞬态清理循环 | 共用 Frame::clear_transients／release_controls |

## 代码级链路与设计理由

```text
Window / Input 原始帧（保持不变）
           ↓
SceneRuntime 的 Input::Gate ← Editor 本帧许可 ← ViewPanel
           ↓                  （app 默认允许）
普通 Update 输入 + 固定步累积输入
```

### Input::Gate 是消费者状态，不是全局路由器

Gate 收敛在现有 core/input.h/.cpp 内，读取纯值 Frame 和 enabled，不依赖 GLFW、ImGui、Scene 或 editor。
不同消费者可各自拥有 Gate；没有订阅表、共享“已消费”标记或全局 EventBus。
它不决定焦点／动作绑定策略，只执行调用者许可及输入丢失／重获的状态规则。

read 生成自己的稳定帧：相同源 serial 且许可没变化返回同一发布；许可变化即使源 serial 不变，也产生新的消费者 serial。
否则 Fixed／普通 Update 会把本地失焦的释放误认为已经处理过。源 serial 倒退仍拒绝，start 重新建立输入流。

关闭时 down／pressed 清空，并为上一次游戏帧中 down 的按钮生成一次 released，位移／滚轮／轴归零。
重新开启的首帧不接受点击、位移、滚轮或轴；所有仍按住的数字按钮写入固定容量 bitset，直到物理松开才解除阻断。
此后新按下与快速点击正常通过。轴只在取得输入的首帧归零，下一帧恢复最新值，不猜测死区或要求摇杆精确回到 0。
键鼠和 16 个标准手柄槽走同一受控快照；connected、光标位置等元数据保留，focused 表示该消费者当前获准接收输入。

### Runtime 的采样与固定步仍分开

SceneRuntime::advance 先通过 Gate，再沿用已有固定输入合并／普通输入处理。
set_input_enabled 可在 inactive 时设置，start 保留许可策略；app 默认允许，Editor 初始化默认关闭。
Gate 保存来源状态，Runtime 保存尚未消费的固定步状态，两者不能合成一个边沿 bool：
普通更新可能已经看到 release，但固定步因为本帧零步尚未看到。

discard_input 服务采样中断：释放固定输入中的 held，取消旧 pressed／位移，保留已累计 release；Gate 标记中断，
下次先发布释放再重新取得输入，不重置游戏时间或 Running／Paused。
Engine 在 framebuffer 为零的分支调用它，继续原有等待事件／计时重置流程。
直接重置整个输入 Frame 会丢失未消费 release，因此本次没有采用这种做法。

### Viewport 是策略边界，不向 core 注入 ImGui

ViewPanel 记录 Play 图像的可见几何命中与窗口焦点；accepts_game_input 在最终消费时继续检查文本、活动控件、拖拽和弹窗。
因此即使 Inspector／对话框在 ViewPanel 之后绘制，也能阻断本帧输入。
无纹理、inactive Runtime、隐藏／折叠、黑边／裁切区外均不允许；保留现有 viewport 坐标映射，不重复计算另一套矩形。
有效游戏区域拥有两个滚轮轴，避免游戏缩放同时滚动 ImGui 窗口。

Editor 每次宿主更新先 set_input_enabled(false)，在本帧 UI 完成后按最终条件放行。
如果 prepare_frame 因 WSI 重试没运行 UI，许可保持 false，不沿用上一帧的 hover 状态。
Viewport 交互 ID 从 m_gizmo_id 改为 m_interaction_id：它已同时服务 Gizmo、编辑相机和游戏滚轮，不再假称仅供 Gizmo。

## 架构回顾（036–040 及相邻边界）

| 审查维度 | 结论与本次处理 |
| --- | --- |
| 目录 | core/input 负责输入值与消费者门控；runtime/scene_runtime 负责游戏执行；editor/panels/view 负责 ImGui 策略。Gate／Frame 行为合入已有文件，不新增细碎 manager 文件 |
| 职责 | Application 宿主事务与 System 游戏更新已拆开；DemoMotionSystem 留在 app。Runtime 不接收编辑器相机、资产刷新或 GPU 生命周期职责 |
| 依赖 | Window→Input；Runtime→Input＋Scene；editor→Runtime。不存在 core/runtime 反向依赖 editor/ImGui；未为方便 UI 建全局输入单例 |
| 冗余 | 原生 Input 与 Runtime 重复的瞬态清理合并到 Frame；释放控制同样由 Frame 复用，区别于只清边沿。UI 不复制 Runtime 状态 |
| 生命周期 | Engine 先停止 Runtime 再释放 Scene／资产／设备；Session 停止 Play System 后恢复 Edit 原件。Gate 和固定输入均拥有值快照，不借用平台回调内存 |
| 帧边界 | UI 许可在 System 更新前应用；无 UI 帧默认关闭；采样中断不遗留按下或丢失 release；旧 Shader／GPU 版本仍由渲染层既有在途保护管理 |

当前 System 全部主线程串行，不能把 Scene／System 引用捕获到后台任务再任其退出；未来系统任务须定义读写集合与停机等待，
不能依赖 Engine 最后的全局 wait_idle 来补救已被提前销毁的 System。此项保留为并行扩展约束，未创建闲置的线程调度层。
runtime/runtime.h 仍是 Application 启动入口，scene_runtime.h 是游戏循环入口；没有为统一名称同时大范围移动稳定文件。
035 的 GLFW 进程生命周期、036 的 CPU/GPU 测量边界继续保留，本轮未改 Renderer／RenderGraph 的队列或退休策略。

## 测试结果

本项新增 11 项：5 Input Gate、4 Runtime、2 ImGui，主测试共 581 项。
覆盖独立消费者、重复 serial／许可切换、按住重获、手柄末槽末按钮、轴／位移、平台失焦、采样中断、
固定步迟到 release、初始化禁止、文本／活动控件／隐藏／失焦区域，以及 Viewport 绘制后才打开的弹窗。
已有暂停／单步、Gizmo、相机、WSI 和 GPU 帧顺序测试继续回归。

最终 Debug／Release 构建无本次警告，完整 CTest 各 6/6 通过，耗时 15.42／13.65 秒。
主测试 580 通过，1 个缺屏障对照按设计留给专门同步验证；其他 GPU／WSI／构建契约／profile 项均通过。
Input、Runtime 和 Viewport UI 全部相关测试另连续运行 10 轮通过。
日志：/tmp/comet-040-verified-build.log、comet-040-verified-release-build.log、
comet-040-verified-focused.log、comet-040-debug.log、comet-040-release.log、comet-040-repeat.log。
038 Linux CI run 34067499081 已成功；本次推送后的 CI 另以实际结果为准。

## 限制与后续方向

采用保守的“聚焦且鼠标位于可见游戏画面”策略，鼠标离开时连键盘／手柄一起关闭；不宣称实现了 FPS 相对鼠标锁定。
重新进入不需要平台光标锁定，激活点击本身被拦住。单步读取的是受路由控制的输入，点击工具栏时通常为中性输入。
尚未分玩家、按设备分配输入、制作 Action Map 或持久回放格式；这些不靠 Gate 猜测。
Native Script、dirty Transform、物理／音频／角色 demo 仍未完成；下一项是脚本生命周期与共享描述符字段。
