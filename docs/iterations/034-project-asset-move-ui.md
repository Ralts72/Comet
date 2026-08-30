# 034 Project 面板资产移动交互

## 目标

把第 033 步的资产移动事务接入编辑器，但不让 ProjectPanel 直接操作文件系统，也不把路径变更误建模成每帧属性同步。

用户在 Project 面板选中资产后点击 `Move / Rename`，输入 `assets/` 相对路径并提交。操作只在按钮或 Enter 事件发生时执行；成功后
保持同一个 AssetHandle 和选中状态，失败时对话框保持打开并显示后端诊断。

## 职责边界

```text
ProjectPanel
  -> 收集 selected AssetHandle + destination
  -> 调用 MoveCallback
  -> 展示 AssetScanReport

Editor composition root
  -> 将 callback 绑定到 AssetManager::move_asset
  -> 同步 Inspector cache / Log / AssetSourceMonitor

AssetManager
  -> 校验并执行 source + .meta 事务
  -> scan 提交数据库与 Runtime 刷新
```

ProjectPanel 只保留对话框 buffer、当前操作 Handle 和错误文本，这些都是 UI 临时状态。它不持有 ProjectPaths，不拼绝对路径，也不调用
`std::filesystem::rename()`。MoveCallback 使用 `AssetScanReport`，与手动 Refresh、自动源监视共享已有结果模型，不再引入一套 UI
专用成功/错误协议。

## 事件式交互

- 没有选中有效资产时禁用 `Move / Rename`；
- 打开对话框时以当前项目相对路径预填，既可只改文件名，也可改父目录；
- 只有点击 `Move` 或 InputText 返回 Enter 事件时调用一次 callback；
- `snapshot_updated == true` 表示事务已经提交，即使 scan 同时包含与本次移动无关的诊断也关闭对话框；
- 提交失败时保留输入和弹窗，以第一条诊断提供就地反馈，完整问题仍显示在 Project 的 Scan Issues 并写入 Log；
- Cancel 只清理 UI 临时状态，不触发任何扫描或文件操作。

这里使用 `snapshot_updated` 而不是 `issues.empty()` 判断移动是否提交：Asset Database 允许在有效快照中报告局部坏资产，不能因为项目中
另一个文件有诊断，就把已经提交的移动误显示为失败。

## 编辑器状态同步

成功移动后：

1. AssetManager 内部 scan 以同一 Handle 更新数据库路径并刷新已加载 Runtime 资产；
2. ProjectPanel 从数据库重建展示列表，Selection 因 Handle 未变而保持；
3. Editor 失效一次 Inspector 资产缓存，使选中资产从新路径重新读取；
4. Editor 向 AssetSourceMonitor 精确 acknowledge 旧 source、旧 sidecar、新 source、新 sidecar；
5. 下一次 500ms poll 不会把编辑器自己的移动再次识别为外部变化；
6. 成功信息和所有诊断写入现有 Log 面板，不塞入 Inspector。

失败时 AssetManager 已保证数据库和文件回滚，Editor 只记录诊断，不主动清空 Selection 或修改 Inspector 数据。

## 验证

- Debug 全量构建，确认 ProjectPanel callback、Editor composition 和 ImGui modal 接线；
- 完整 CTest 继续覆盖第 033 步的正常移动、目标冲突、路径越界、重复 GUID 扫描失败回滚；
- 交互路径不增加每帧 callback，代码审查确认只有 Move 按钮和 Enter 提交点调用后端；
- 实际 Vulkan/ImGui 可视检查留给具备显示与 GPU 的本地运行环境：选中资产、改名、跨目录移动、失败后修正路径再次提交。

## 下一步

阶段 3 的资产身份、导入、缓存、后台刷新、失败安全和基本项目移动闭环已经形成。下一步进入阶段 4A，先定义 Viewport Render Request
与 editor-only camera 边界；先完成可测试的模式/Camera 来源选择，不把相机操控、HiDPI、拾取或多 Viewport 混入同一步。
