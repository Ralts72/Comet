# 003：收集后退出物理，保留实体播放反馈

## 背景

demo 的目标原来在接触回调中直接请求销毁。它能计分和发声，但目标自身不能继续运行脚本，
例如先升起／旋转再消失。把 `Collider::is_trigger` 关掉会变成实体碰撞，并非退出模拟；
只移除 Collider 又会留下不完整的 RigidBody 配置。

本项添加类型化刚体查询和移除，把既有组件、阶段末结构提交和 PhysicsSystem 同步链连起来。
具体表现仍由项目 Lua 控制，不添加收集 System、通用组件代理或新命令管理器。

## 1. 原来与现在的脚本

之前在 `on_trigger_enter` 中计分后立即请求销毁：

```lua
comet.destroy_entity(comet.self_entity())
```

现在先移除物理参与，保留原目标供后续更新：

```lua
function script:on_start()
    self.collected = not comet.has_rigid_body(comet.self_entity())
    self.collect_time = 0
end

function script:update(dt)
    if not self.collected then
        return
    end
    self.collect_time = self.collect_time + dt
    comet.translate(0, 1.5 * dt, 0)
    comet.rotate(0, 360 * dt, 0)
    if self.collect_time >= 0.5 then
        comet.destroy_entity(comet.self_entity())
    end
end
```

接触确认后设置 `self.collected` 并调用 `comet.remove_rigid_body(comet.self_entity())`。
计分、短音效、小奖牌创建和停止 gameplay 输入都沿用现有逻辑；同批回调仍由 collected 防止重复计分。

`has_rigid_body` 有真实用途：重载会重建 Lua self，而已经提交的组件变化仍在 Scene 中。
因此新版 on_start 可以从组件事实恢复“正在收集”的阶段，不需要复制一份永久 collected 组件或会话键。
短动画会从零重新计时，这是受控重建，不承诺任意 Lua 状态热迁移。

## 2. Lua 只提交意图，不直接改物理世界

```text
Lua remove_rigid_body(reference)
  → 既有引用解析：场景代次 + UUID + EntityId
  → Scene::request_remove_rigid_body
  → 同一个 EntityRequest 队列
  → 当前启动／Fixed Update／Update 阶段末提交
  → 下一物理固定步同步移除 Jolt body
```

新增 Lua 入口不持有 EnTT、Jolt 或 Editor 对象；参数错误继续使用现有保护调用。
查询只读取当前组件，因此请求后、提交前仍返回 true；不是看到请求就伪造已移除状态。
移除不终止当前物理步骤，也不撤销已经产生的接触通知。

## 3. Scene 沿用有界结构请求

`EntityRequest::Type` 从 Create／Destroy 扩展为 Create／Destroy／RemoveRigidBody，提交处显式 switch：

```cpp
case EntityRequest::Type::RemoveRigidBody: {
    const Entity entity = find_entity(request.uuid);
    if(entity && entity.get_id() == request.id)
        entity.remove_component<RigidBodyComponent>();
    break;
}
```

入队要求活动 Runtime 和本 Scene 的有效实体。
已经没有刚体或相同目标已经排队时幂等成功，不重复占用队列容量；真正新增的请求与创建／销毁共用 1024 项上限。
提交前目标已销毁、即使同 UUID 创建了新实体，EntityId 不同也不会误删新实例的刚体。
与销毁请求交错按原队列顺序处理，提交前组件已被移除也不会重复删除其他东西。

Collider、Transform、MeshRenderer、Script、AudioSource 和层级都保留。
已有 ComponentDescriptor／序列化格式不变，没有把一项类型化能力扩成开放任意组件结构操作。
未来其他组件操作仍须分别确认 System 生命周期，不能仅凭“有描述符”就全部向脚本开放。

## 4. 为什么不修改 PhysicsSystem

PhysicsSystem 已在固定步同步时检查组件组合，RigidBody 不再存在便移除其 body。
物理层继续负责原有接触失效和局部唤醒，Scene 和脚本不复制 Jolt 逻辑。
仍存活的双方可以收到一次 Exit；目标实体本身不因物理移除而变成失效引用。

暂停不会执行动画和结构提交，单步照常推进；Stop／阶段失败丢弃尚未提交的请求。
已经提交的修改属于运行 Scene，不是 Stop 时反向添加组件。
Editor 恢复 Edit、重新 Play 或项目重开由既有场景基线克隆恢复刚体，不把运行修改写回文档。

## 5. 验证

测试覆盖阶段末可见性、重复／容量、失效和跨场景引用、同 UUID 重建、失败／Stop 清理、
移除后的物理同步和接触退出。真实 demo 回归检查计分一次、目标保留但无刚体、动画暂停／单步、
动画中重载后仍能完成、奖牌保留、Edit 隔离和 Restart 恢复。

实际验证：

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='SceneRuntimeTest.*:ScriptInvocationTest.*:PhysicsSystemTest.*:ScriptSystemTest.*'
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure
cmake --build --preset app-release --parallel 6
```

Debug 全量及 Release app 构建通过，无编译警告；120 项定向测试、930 项 CPU 测试、
156 项无窗口 UI 测试通过，Shader 构建契约与模块边界检查通过。
`AssetSourceMonitorTest.UnchangedFallbackDoesNotInvalidateDatabaseCandidate` 因当前平台使用原生通知而条件跳过。

首次定向回归为 119／120：测试错误地在 Stop 解除 Scene 绑定、代次改变后继续复用旧实体引用。
修正测试为先确认旧引用失效，再通过 Start 取得新引用，分别检查只读查询和活动 Runtime 写入限制；
没有放宽生产代码的引用有效性规则。初次失败记录在 `/tmp/comet-auto3-003-targeted.log`，
最终日志使用 `/tmp/comet-auto3-003-final-{build,targeted,regression,release}.log`。

本项未启动真实图形窗口，不把无窗口回归等同于实际音画和交互验收。

## 6. 手动体验

1. 启动 demo 的 app 或 Editor Play，用左右方向键把移动方块移到条纹目标。
2. 得分后目标应继续升起、旋转约半秒再消失，小奖牌和旋转方块的得分反馈保留。
3. Editor 在这半秒内暂停，动画应冻结；单步应略微前进，继续后完成。
4. 按 R 重开，目标和刚体恢复，分数与小奖牌清空；Stop 后 Edit 中目标仍有原组件。

本项不提供重新添加刚体、任意组件增删、Prefab、完整角色控制器或动画系统。
后续优先推进输入组的优先级与消费，继续使用当前 demo 的真实交互验证。
