# 014：项目输入绑定的上下文关系反馈

## 背景

004 已支持输入组优先级与消费，但项目输入面板仍沿用早期的提示：只要两个动作的 source/control
字符串相同，就显示 `!` 与“其他动作也使用此绑定”。同级共享、公共动作和高优先级屏蔽都长得一样，
而 `F1`／`F01` 等解析成同一按键的名字又可能漏报。这是 authoring 反馈缺口，不是运行路由失效。

本项对应路线图的“上下文级冲突反馈”；合法重叠仍允许保存，不新增游戏内改键或玩家配置。

## 规则留在 InputActions

增加小型只读关系枚举及比较函数，仍放在 InputActions 内：

```cpp
enum class BindingRelation { Unrelated, Shared, Consumes, ConsumedBy };

static BindingRelation compare_bindings(const Binding& binding,
    const Context* context, const Binding& other, const Context* other_context);
```

比较规范化后的 `Binding::control`，不是面板字符串。倍率、反向和死区改变数值，不改变物理控制身份。
`nullptr` 表示公共动作；同组、同优先级或高优先级未启用消费时都共享。

```cpp
bool consumes_context(const Context* consumer, const Context* target) {
    return consumer && target && consumer->consume
        && consumer->priority > target->priority;
}
```

这个谓词同时服务比较接口与实际 `resolve_routes`。原来的 control → 最高消费优先级映射改为
control → 最高消费组的临时指针；仍只收集已启用的消费组，并且不把指针保存到本次解析之外。
最终路由、同级共享、公共动作以及逐绑定消费规则没有改变，不引入第二套运行算法。

比较接口刻意描述“双方组都启用时”的两两关系，不读取 enabled 或 RuntimeInput。
不能把两两共享当作一定获得输入：第三个更高消费组仍可能屏蔽它们。

## 面板链路与前后对比

之前：

```text
每个绑定行 → 遍历其他原始字符串 → 相同则 ! → 泛化提示
```

现在：

```text
编辑动作／输入组草稿
  → 绑定关系区域展开
  → build() 复用完整解析与 InputActions::create 校验
  → compare_bindings() 比较选中动作和其他动作
  → 显示对方动作、组、共享／消费方向、默认禁用标记
```

- 在选中动作下增加默认展开的“绑定关系”，删除原来的 `!`。
- 以规范化 source/control 显示，每个控制与对方动作只显示一次；同一动作的重复绑定不重复刷提示。
- 默认禁用的 palette 仍显示它启用后的消费方向，同时标注“默认关闭：palette”。
- 明确这不是当前 Play 运行状态；其他组仍可能参与最终路由。
- 草稿无效时显示原因，不拿上一版有效配置产生过时反馈，也不猜测部分合法字段的最终含义。
- 保存仍走原请求与原子写盘，不改项目格式、RuntimeInput、场景历史或现有活动配置。
- 中文词表与英文提示同时更新；英文组说明补上“已启用且勾选消费”的前提。

这里只在面板展开的关系区构建临时有效配置，不保存新的缓存／Manager，也不增加运行状态监听回调。

## 验证

- Debug 全目标与 `app-release` 构建通过，无新增编译警告。
- InputActions／RuntimeInput／真实 demo 清理链路共 28 项定向 CPU 测试通过；项目输入及设置共 9 项 UI 通过。
- 新 CPU 测试覆盖五类控制、消费双向、同级／同组、公共动作、默认关闭的假设关系、倍率／死区与 F1/F01 别名。
  原实际路由测试继续证明禁用组、声明顺序及部分绑定被屏蔽时的行为未变。
- UI 捕获真实绘制文本，验证修改默认状态／优先级／消费即时更新但不请求保存；公共／同级共享仍能保存；
  键盘 Right 与鼠标 Right 不混淆，无效 control／上下文草稿不显示旧结论，修复后恢复。
- 最终 `ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure -V` 通过：989 CPU、167 UI，
  1 项既有原生监听平台的轮询回退测试按条件跳过；构建契约与模块边界通过。
- 首次 UI 定向运行有 1 项旧 footer 文本断言失败：修改测试文字时编译中的旧目标文件尚未刷新。
  最终重新编译 UI 测试并完整回归通过；不隐藏该失败，也不把它当成运行路由问题。
- 两份只读交叉审查无逻辑／生命周期阻塞，收紧英文消费描述为同一 control，避免误解成整个动作被禁用。
- 日志：`/tmp/comet-auto3-014-{build-final,targeted,ui,ui-final,regression,release}.log`。
  未启动新 GPU／性能测量，反馈消费真实配置类型但不触及渲染或运行输入状态。

## 架构价值与限制

引擎拥有可复用的消费规则，Editor 拥有草稿与显示文案；没有把 ImGui、项目写盘或“冲突警告等级”放入 Engine。
接口是实际编辑器消费者需要的只读比较，不开放 Runtime 的私有路由表。

这不是实时输入调试器或组合键／设备分配系统，也不决定一键多用是否符合项目玩法。
后续游戏内改键、玩家覆盖和更丰富交互语义仍按路线图独立推进。
本项的无窗口 UI 测试可证明实际绘制文本与操作行为，不等于字体布局、键鼠手感的人工验收。
