# 027 资产源变化监视入口

## 目标

让 Editor 不再依赖 Project 面板的手动 Refresh 才能发现 `assets/` 中的新增、修改和删除，同时保持已有资产事务边界：
监视层只判断“源目录是否可能变化”，`AssetDatabase` 仍负责完整校验和变化集，`AssetManager` 仍负责 revision、后台 CPU
导入、Owner Thread GPU 创建和 Registry 发布。

本步没有让 watcher 线程读取 Asset Database、访问 ImGui 或创建 GPU 资源，也没有为 Editor 复制一套资产导入逻辑。

## 改造前

```text
Project 面板点击 Refresh
  -> AssetManager::scan()
  -> AssetDatabase 候选快照/变化集
  -> 已加载资产刷新
  -> Inspector 缓存失效
```

外部工具修改 Texture、Mesh、Material、`.meta` 或 glTF 外部 `.bin` 后，如果用户不点击 Refresh，数据库 revision 不会推进，
后台导入链路也不会启动。Project 面板同时承担按钮展示和刷新后 UI 状态更新，Editor 没有可复用的自动刷新入口。

## 改造后

新增引擎侧 `AssetSourceMonitor`，以项目资产根目录为输入，维护规则稳定的文件快照：

```text
Editor frame
  -> AssetSourceMonitor::poll()
      -> 未到 500ms 间隔：NotPolled
      -> 文件路径、mtime 或 size 未变化：Unchanged
      -> 文件集合或状态变化：Changed
      -> 目录不可访问：Failed，保留上一份成功快照

Changed
  -> Editor::refresh_project_assets()
  -> AssetManager::scan()
  -> AssetDatabase 校验、提交变化集和推进 revision
  -> Mesh/Texture 后台 CPU 刷新或 Material 同步刷新
  -> Project 列表与 Inspector 缓存更新
```

`poll()` 每帧调用只是为了提供稳定接入点；时间门控命中前不访问文件系统。实际递归遍历最多每 500ms 一次，并且只产生
变化事件，不会每 500ms 无条件执行 Asset Database 扫描和资源刷新。`poll_now()` 保留给显式 Refresh 和确定性测试。

这个类型位于 `engine/src/asset/`，因为它表达的是项目资产源目录观察能力，可被未来的导入工具或项目服务复用，不依赖
ImGui、ProjectPanel 或 Editor 生命周期。它没有命名为 `FileWatcher`，避免把当前跨平台轮询实现误解为系统原生文件事件后端。

## 失败与恢复

快照构建遵循“完整成功后再提交”：根目录缺失、不是目录、遍历失败或任一文件状态读取失败时返回带路径和信息的 `Failed`，
不会用不完整结果覆盖上一份成功快照。因此临时权限错误或挂载抖动不会被解释成全目录删除。

Editor 对相同错误做边沿触发日志，轮询间隔内的 `NotPolled` 不会清除错误状态或造成重复日志。若监视器在第一次建立基线前
失败，目录之后首次可访问会返回 `Changed`，从而补做数据库扫描；已有基线恢复后则与旧快照比较，只在真实变化时触发。

原子文本写入使用的 `.comet-tmp-*` 中间文件与 Asset Database 一样被忽略，避免观察到短暂事务文件。

## 编辑器自身写入抑制

Material Inspector 保存 `.mat`、Texture Inspector 保存相邻 `.meta` 时，`AssetManager` 已经同步更新数据库 revision、运行时对象
和依赖。成功后 Editor 调用 `AssetSourceMonitor::acknowledge(relative_path)`，只更新这个已知文件在监视快照中的状态，避免下一次
轮询再做一次无意义扫描。

扫描为新资产自动生成 `.meta` 时，也只确认本次新增资产对应的 sidecar。这里没有在每次写入后重拍整个目录快照，因而不会把
同时发生的其他外部修改一起吞掉。新建 Scene 等尚未直接更新 Asset Database 的写入不做确认，仍由监视事件触发正常扫描。

## Project 面板边界

`ProjectPanel::update_scan_report()` 只负责接收已完成的扫描报告、重建展示列表和清理已经失效的资产选择。手动按钮仍通过回调
请求 Editor 扫描；自动监视则由 Editor 调用同一个更新入口。这样 ProjectPanel 不需要持有监视器，也不负责资源生命周期。

## 验证

- 初始成功采样只建立基线，不产生伪变化；
- 新增、修改和删除文件各产生一次 `Changed`，下一次采样恢复为 `Unchanged`；
- 原子写入临时文件不产生事件；
- Editor 已知写入经精确路径确认后不再产生重复事件；
- 根目录暂时不可访问时返回失败并保留基线，恢复到原状态不产生删除/新增风暴；
- 第一次采样失败后目录恢复会产生 `Changed`；
- 长轮询间隔内返回 `NotPolled`，`poll_now()` 仍可确定性观察变化；
- Debug 全量构建与完整 CTest 通过。

## 当前限制与下一步

当前快照使用相对路径、写入时间和文件大小，与 Asset Database 的现有源签名策略一致；它适合当前项目规模，但不是大型项目的
最终监听后端。后续如 profile 证明递归采样成本明显，可在不改变上层 `Changed -> scan()` 契约的情况下替换为 FSEvents、
inotify、ReadDirectoryChangesW 或跨平台库，并在后端继续做事件合并。

下一步先做阶段 3 的阶段性架构复盘：核对监视、扫描、任务调度、revision、导入产物、GPU 发布和旧资源保留之间是否还有
重复状态或职责倒置，再决定是补齐资产移动/重命名工具闭环，还是进入阶段 4 的 Editor Viewport 交互。
