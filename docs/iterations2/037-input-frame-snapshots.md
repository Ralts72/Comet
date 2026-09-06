# 037：键鼠／手柄帧快照与输入生命周期

## 背景与验收项

阶段 5 核心已验收，阶段 6 首先需要一个不依赖 ImGui 的运行时输入入口。
原先编辑器直接消费 ImGui 输入，app 的更新只有自动旋转；不存在可给普通 Update、Fixed Update 或回放消费者保存的输入值快照。
本项完成平台采集、稳定帧边界和 app 的实际消费，不提前实现动作绑定、脚本、物理或全局事件总线。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| 运行时没有统一键鼠／手柄入口 | Engine::get_input_frame 提供 Window 最新发布的输入快照 |
| UI 使用 ImGui，但游戏复用会反向依赖 editor | Input 是 core 的纯值状态，Window 负责 GLFW 翻译，ImGui 仍独立消费自己的输入 |
| 只轮询按住状态容易错过同一批事件里的快点 | 键鼠事件累积 pressed/released，快速按下再松开两者都保留 |
| 输入数据可在后续平台回调中变化 | pending 和已发布 Frame 分离，消费者也可复制完整 Frame |
| 缺少失焦／断连语义 | 释放所有控制、清除位移／滚轮、手柄轴归零；恢复焦点不制造鼠标跳变或旧手柄按下边沿 |
| app 只能看自动旋转示例 | 同一输入帧驱动示例相机，键盘／滚轮／标准手柄可操作 |

## 代码级链路

```text
GLFW key/mouse/cursor/scroll/focus callbacks
                  ↓
Window → Input.pending（有界状态和边沿）
  ↓ glfwPollEvents 结束，采样标准手柄
Input::publish_frame
                  ↓
Engine::get_input_frame → app Update / 后续 Runtime 消费者
                  ↘ Frame 值副本（不借用 Window）
```

### Input 是输入状态机，不是 EventBus

新增 core/input.h/.cpp，Key、MouseButton、GamepadButton、GamepadAxis、Frame 都收敛为 Input 的嵌套类型。
Input 不包含 GLFW 类型、Window、ImGui、Scene 或 GPU 指针；按钮、轴和帧都是固定容量值。
公开的事件／采样入口可用于平台适配和确定性注入，不依赖真实窗口才能测试；生产平台仍由 Window 唯一拥有。
没有 std::function 订阅列表，没有任意业务事件转发，也没有每帧通知资产／面板重建。

每个 ButtonState 包含 down、pressed、released。只有 down 真正变化才累计边沿，重复按下不变成重复 pressed。
同一发布区间中按下再松开，最终 down=false，但 pressed 和 released 都为 true；不因只看末尾电平丢失短点击。
这不是保存每次按键顺序／次数的事件日志；一帧内多次同键点击会折叠成边沿 bool，未来节奏游戏等高精度输入需另评估。

publish_frame 递增输入 serial、复制 pending 到 Frame，随后只清除 pending 的瞬态边沿、cursor_delta 和 scroll。
按住状态保留到后续 release；后续事件只写 pending，get_frame 的已发布值在下次 publish 前不变。
复制出来的 Frame 不会被下一次发布改写。Input serial 是采样序号，不是 GPU serial 或 Fixed Update tick。

光标位置为窗口逻辑坐标，位移为事件差值累计；第一条位置事件只建立基线。滚轮保留 GLFW 的双轴偏移单位，不伪装成 framebuffer 像素。
非有限位置／滚轮忽略，累计结果必须有限；未知／越界按键和槽位忽略，不让异常平台数据越界写数组。

### 焦点和手柄

失焦立即在 pending 释放已按住的键鼠／手柄按钮，清除 pressed、位移和滚轮，轴归零。
未聚焦期间仍观察设备是否连接，但不激活按钮／轴。恢复焦点后第一条光标事件重新建基线，避免跨窗口的大位移。
首次启动／恢复焦点的第一份手柄采样建立电平基线：持有的按钮可以恢复 down，但不伪造一次新的 pressed。

Window 每次轮询后调用 GLFW 标准 gamepad API 采样 16 个 joystick 槽位；无标准映射或已断开则记为未连接。
标准按钮按空间位置命名 South/East/West/North，避免把所有设备写成 Xbox 字母；轴和按钮次序遵循 GLFW 标准映射。
原生扳机 [-1,1] 转为 [0,1]，摇杆保持 [-1,1]、Y 向下；Input 再夹取范围并把非有限轴归零。
断连释放按钮并清零轴。槽位只代表当前运行的设备位置，不是持久 player ID 或设备 GUID。

手柄采用平台状态采样，不能保证捕获两次采样之间已经结束的超短点击；它和键鼠事件累积的保证不同。
死区／按键重映射属于上层动作定义，本次只在 app 示例给摇杆应用 0.15 死区，底层不丢弃小幅轴输入。

### Window、Engine 与 ImGui 的边界

Window 持有 Input，安装五种原生回调并将自己的地址放入 GLFW user pointer；窗口禁止复制，指针身份稳定。
Window::poll_events 先处理全局平台事件，再采样本窗口手柄并发布自身 Frame。
不在 poll 前清空 pending，因此另一扇窗口轮询时送到本窗口的事件不会被下一次轮询直接清掉。
wait_events 只让事件继续累积，正常轮询时才发布；此行为不扩大成多窗口 Renderer 编排支持。

Engine 现有 poll→Update 时序不变，只增加只读 get_input_frame；更新回调执行时已经能取得本轮发布快照。
新增 Window::request_close 封装正常关闭请求，app 不为输入操作直接调用 GLFW。

ImGui_ImplGlfw_InitForVulkan(..., true) 保存并串接已安装回调，自己的数据使用独立映射，不覆盖 Window user pointer。
测试实际初始化该后端，向安装后的按键回调送入一次事件，确认 ImGui 和 Input 都收到，再检查 shutdown 后恢复原回调。
外部代码仍可通过 get() 进行平台互操作，但不能改写 user pointer；替换输入回调必须维护原调用链。
当前 editor 的相机、快捷键、Gizmo 仍按 ImGui 原路径工作，没有把 Edit 输入或文本框操作转给游戏。
未来 Play Viewport 的输入路由由编辑器／Runtime 边界明确决定，Input 不猜测 WantCaptureKeyboard 或 EditorMode。

## app 的实际效果

app 保存 Main Camera 的 EntityId，通过同一帧快照读取 W/A/S/D 水平移动、Q/E 高度、左 Shift 加速、滚轮前后移动、Escape 退出。
第一个已连接的标准手柄使用左摇杆和左右扳机，键盘／手柄合成方向后限制单位长度，避免对角线更快。
示例移动采用世界轴，单次时间增量最多 0.1 秒以避免调试暂停后的大幅跳动；原有 cube 自动旋转保持。
它只是输入可用性的相机消费者，不声称已经有角色碰撞、输入 action asset 或游戏脚本系统。

## 架构价值

- 平台翻译留在 Window，纯状态机可独立测试；不把 GLFW 枚举变成引擎公开键值 ABI。
- 同一 Frame 可用于普通更新、后续固定步／回放；没有让 Runtime 从变化中的 ImGui 全局状态临时读取。
- 生命周期沿用实际 Window owner，销毁原生窗口后不再有回调入口；没有新建全局 Input singleton 或业务 EventBus。
- 输入更新是硬件采集边界，不是把材质／资产的事件更新退回每帧重建模式。
- 新类型聚合在一个有职责的模块，没有为 key/mouse/gamepad 各制造一个 manager 和 data 文件。

## 测试结果

Debug／Release 最终构建成功，完整 CTest 各 6/6 通过，耗时 17.18／13.60 秒。
单元套件 551 项（550 通过、1 项专门同步对照正常跳过），原有 25 同步 GPU、10 WSI、两个契约和 profile smoke 均通过。
输入／窗口 12 项定向测试先通过一次，再通过 `GTEST_FILTER='InputTest.*:WindowTest.*' GTEST_REPEAT=20 ctest --preset dev-debug -R '^unit_testing$' --output-on-failure --timeout 120`，
共 240 次测试执行、13.23 秒，没有回调恢复或 Cocoa 生命周期停滞。
本次 C++ 修改按 .clang-format 格式化，dry-run 与 git diff --check 通过；未修改／格式化 Shader 和第三方。
本机日志为 `/tmp/comet-037-focused.log`、`/tmp/comet-037-verified-debug.log`、`/tmp/comet-037-verified-release.log`、`/tmp/comet-037-repeat.log`。
键鼠集成采用已安装的原生回调注入及真实 ImGui 后端，并非物理键盘／鼠标自动操作；未做手工手感巡检。

- 七个纯输入测试覆盖稳定发布、按住／释放、快速点击双边沿、光标／滚轮累计、失焦／恢复、手柄非法轴／断连、异常输入、Frame 副本重复消费。
- 原生 Window 回调测试覆盖连续键值区间两端、特殊键、未知键、repeat、鼠标短点击与滚轮，发布前不可见、轮询后可见。
- 实际 ImGui GLFW 后端测试覆盖回调串接／恢复和 user pointer 所有权；既有 Window 测试继续覆盖多窗口和连续生命周期。
- Engine 三帧 GPU 集成回归同时检查更新回调前输入 serial 已前进；不是只测试一个未使用的纯类。
- 未连接真实手柄进行手感／平台映射实机验收；手柄状态机使用确定性采样输入测试，不冒充硬件验证。

## 限制与下一项

没有 text/IME、触摸、多键盘原始设备、鼠标锁定／raw motion、触觉反馈和可保存动作映射。
Frame 是同版本内存值，不是已定义版本的磁盘回放格式；键鼠边沿折叠、手柄采样频率限制继续成立。
Input 与 Window 使用遵循主线程约束；Frame 副本可独立持有，不代表同一个 Input 可并发读写。

下一项为 Fixed Update／普通 Update 的明确阶段和有界累计：零固定步的帧不能丢边沿，多个固定步不能重复触发同一 pressed。
之后接 RuntimeState 暂停／单步、Native Script、dirty Transform、角色／物理／声音及 Play/Edit 隔离。
本项不宣称完成阶段 6，也没有从路线图移除上述验收。
