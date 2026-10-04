# 039 手柄录入识别跨跳帧重连

## 真实路径

Engine 在窗口非零尺寸但渲染准备 Deferred 时，仍会采样并发布物理输入，却不调用 on_frame_ready 中的 UI。
它不同于最小化等待路径，不一定增加全局 interruption。原手柄录入只比较锁定槽号及当前 connected：

```text
开始录入槽 0
  → 断开并发布（无 UI）
  → 槽 0 重连并发布（无 UI）
  → 后续新按 East
  → UI 恢复：槽号仍是 0，误写入旧录入
```

重连首个采样原本已压制 pressed，但它不能代表消费者没有错过整个连接中断。
焦点也一直可以为 true，因此不能靠 034 的失焦版本判断。

## 代码变化

Input::GamepadState 增加该槽的 connection_revision，只在实际 connected 状态变化时递增。
重复断开、普通轮询以及仅焦点变化不增加它；已发布快照仍是值，不会被后续采样改写。

```cpp
if(gamepad.connected != sample.has_value())
    ++gamepad.connection_revision;
gamepad.connected = sample.has_value();
```

录入开始记录锁定槽及其连接版本；处理新帧时，除原焦点／全局中断／槽选择检查外，
比较该槽的连接版本。版本改变只取消手柄录入，草稿保留；键盘录入不受另一输入设备重连影响。

这不是持久设备身份，不参与项目 JSON 或玩家文件，不在 Input::Gate 增加全局阻断，
也不建立设备管理器、回调或多玩家分配规则。

## 验证

- 修复前 UI 红测确认同槽重连会把原 West 覆盖误改为 East；修复后保留完整草稿。
  同一用例另确认键盘录入未被取消，仍可录入 K，且面板保持游戏输入阻断。
- 新 CPU 用例验证首次连接／断开／重连递增、重复采样及发布不变、各槽独立、旧快照不变和全局 interruption 不变。
- 41 项定向玩家面板测试、完整 1060 CPU／229 UI、构建契约及模块边界通过；
  1 项既有 CPU 平台条件跳过。Debug 全目标和 Release App 构建通过，无新增警告。
- 日志：`/tmp/comet-auto3-039-{red-ui,build,ui,regression,release}.log`。
  独立复核未发现需要扩大到 Gate、RuntimeInput 或文件协议的修改。

## 限制

只识别 Input 实际采样到的断连；两次平台采样之间发生但未被观察到的设备替换无法推断。
首个已连接槽仍是当前采样选择，不是稳定玩家身份。设备到玩家映射继续留在路线图按需扩展。
