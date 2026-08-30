# 026 Texture 后台 CPU 刷新

## 目标

让已加载 Texture 在项目扫描发现源文件或 Importer 设置变化后，不再在 Owner Thread 同步读取和解码图片。Worker 只生成 CPU
TextureData；Owner Thread 消费完成候选、执行可恢复 GPU 创建、再次验证 revision，并在成功后替换 Registry。旧 Texture 在
等待、过期或失败期间持续可用。

## 与同步入口的边界

以下入口有意保持同步：

- `load_texture()`：启动或显式按需加载需要立即返回资源；
- `reimport_texture()`：Inspector 编辑设置后的命令需要立即给出成功/失败，并只在成功后更新 metadata 和运行时对象。

本步异步化的是 `AssetManager::scan()` 对“已经加载且发生修改”的 Texture 刷新。这样不改变现有调用方返回契约，同时移除
日常 Project Refresh 中最重的图片文件读取与 stb_image 解码工作。

## 通用调度状态

原 AsyncState 的 `ScheduledMeshTask` 和 `pending_meshes` 被泛化为：

```text
ScheduledAssetTask { handle, revision, asset type, future }
pending_assets[handle] = latest scheduled revision
```

Mesh 和 Texture 仍使用各自类型化 completion deque，因为两种 CPU candidate 的数据、缓存和发布步骤不同；任务占位、Future
回收、pending revision 去重和异常日志则共享 `schedule_refresh_task()`。这避免复制一套 Texture scheduler，也没有用宽泛
variant 抹平类型差异。

## Texture Candidate

Worker 捕获调度时已经校验的项目根、相对路径、Handle、revision 和 TextureImportSettings，然后执行：

```text
TextureImporter::import(source, settings)
  -> TextureData or error string
  -> push TextureImportCandidate under completion mutex
```

Worker 不访问 AssetDatabase、AssetRegistry、RenderResourceFactory、Device 或 Logger。Importer 异常在候选中转为错误文本，
`process_completions()` 在 Owner Thread 统一输出带路径和 Handle 的诊断。

## Owner Thread 发布

Texture completion 采用两次 revision 验票：

```text
candidate arrives
  -> database.is_current(handle, revision)
  -> reject import error
  -> RenderResourceFactory::try_create_texture(TextureData)
  -> database.is_current(handle, revision) again
  -> Registry.replace_asset
  -> reload loaded dependent Materials
```

第一次阻止过期 CPU 数据占用 GPU allocation/upload；第二次覆盖 GPU 创建期间发生新扫描的情况。任一步失败或过期都不调用
replace，因此旧 Texture 及引用它的 Material 继续有效。成功替换后才刷新已加载的直接 Material 依赖。

多个 revision 可以排队：pending map 只记录最新 revision，旧 candidate 完成时不会清除新任务的 pending 标记。每个 candidate
仍独立验票，所以 Worker 实际读取文件的时刻不会决定最终发布顺序。

## 验证

- 测试确认 scan 只调度，完成前 Registry 和 GPU 工厂调用次数保持不变；
- Worker idle 后由 `process_completions()` 创建并替换 Runtime Texture；
- GPU 创建返回 out-of-device-memory 时保留旧 Texture；
- 连续两次修改只为最新 revision 创建和发布 Runtime Texture；
- 损坏图片在 Worker 导入失败，旧 Texture 保留且 GPU 工厂不被调用；
- 既有 Mesh 异步刷新测试继续复用泛化后的任务状态；
- 完整 Debug 构建与定向 Texture 测试。

## 下一步

Texture 与 Mesh 已完成后台 CPU 刷新和 Owner Thread 发布。下一步为 editor/tooling 建立轻量项目文件监听：监听只触发已有
AssetDatabase scan/变化集，不直接导入或创建 GPU 资源；先定义事件合并、写入抑制和目录失效策略，再替换目前依赖手动
Project Refresh 的入口。
