# 032 Texture 导入输入快照

## 目标

让已加载 Texture 的后台解码和 Owner Thread 发布复用第 031 步的 `ImportInputSnapshot`，保证 Runtime Texture 对应一份稳定的
源图片内容。图片在解码或 GPU 创建期间再次变化时，旧 candidate 不会短暂替换 Registry，而是推进 revision 并自动重调度。

本步也验证 `ImportInputSnapshot` 确实是导入管线通用概念，而不是为了 Mesh cache 单独创造的类型。

## TextureImporter 稳定输入结果

保留原 `TextureImporter::import()` 作为直接解码入口；新增 `import_with_snapshot(source, asset_root, settings)` 返回：

```text
TextureImportResult
├── TextureData
├── ImportInputSnapshot（单一源图片）
└── inputs_changed_during_import
```

实现先捕获图片内容指纹，再调用 stb_image 解码，最后再次捕获指纹。前后相等才返回可发布 TextureData；输入变化或后观察失败
返回可重试状态。若 stb_image 失败且输入前后稳定，仍传播真实解码错误，AssetManager 按既有策略记录并保留旧 Texture。

Texture 当前没有外部 Importer 输入，因此 snapshot 只有主源文件；`.meta` 中的 color space 和 `flip_y` 由 AssetRecord revision
保护。未来加入独立 mip/压缩源或其他文件依赖时，可直接扩展同一 snapshot，而不修改发布门语义。

## 后台发布门

Texture candidate 现在携带 asset root、input snapshot 和导入期间变化标记：

```text
candidate arrives
  -> AssetRevision current
  -> source snapshot current
  -> recoverable GPU Texture creation
  -> AssetRevision current
  -> source snapshot current
  -> Registry replace
  -> reload loaded dependent Materials
```

任一 snapshot 检查失败都调用 Mesh/Texture 共用的 `reschedule_after_input_change()`。该入口通过
`AssetDatabase::invalidate_import_inputs(handle, {})` 更新当前源签名并分配新 revision，再确认 Registry 中仍有同类型旧 Texture 后
提交新 Worker task。旧 Texture 和依赖它的 Material 在最新候选成功前保持有效。

共用重调度方法只对 Mesh 和 Texture 分派各自 schedule 函数，不把类型化 CPU candidate 合并成基类；两类 importer 的数据、cache
和依赖处理仍不同，强行统一 completion 只会隐藏分支。

## 同步入口

启动期 `load_texture()` 和 Inspector 显式 reimport 继续同步，但也使用 `TextureImportResult`：GPU 创建前后验证源 snapshot。
普通首次加载额外记录数据库 revision，并在注册 Registry 前验票，防止 ResourceFactory 可重入期间 metadata/路径状态变化。

`reimport_texture()` 在进入 GPU 创建前复制 AssetRecord 值快照，后续日志和路径不再跨越可能重入的 ResourceFactory 调用持有数据库
内部指针；成功后仍由 `update_import_settings()` 提交新的设置 revision。

## 验证

- TextureImporter 为真实项目图片返回单文件、可再次验证的 input snapshot；
- 既有色彩空间、flip_y 和损坏图片测试保持；
- 确定性测试在 Texture GPU factory 调用期间把源图片恢复为另一版本：旧 candidate 创建但不发布，数据库 revision 推进并自动
  调度新 candidate，第二次 completion 才替换旧 Texture；
- 连续 revision 只发布最新 Texture、稳定解码失败保留旧 Texture、GPU 创建失败保留旧 Texture的测试继续通过；
- Debug 全量构建与完整 CTest。

## 下一步

Mesh/Texture 的后台 CPU candidate、revision、输入快照和可恢复 GPU 发布已经形成完整闭环。下一步回到阶段 3 的项目操作层，先
设计并实现资产移动/重命名的事务接口：源文件和相邻 `.meta` 必须一起移动、Handle 保持不变，文件系统失败要回滚，成功后复用
同一个 AssetManager scan/变化集更新 Project、Registry 和依赖。
