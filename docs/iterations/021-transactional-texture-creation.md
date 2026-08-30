# 021 事务式 Runtime Texture 创建

## 目标

把 Image allocation、原生 ImageView 创建和 image staging upload 组合成一个完整的 Runtime Texture 创建事务。调用方要么
拿到拥有完整 ImageView 与 ready completion 的 Texture，要么拿到 Vulkan error；失败路径不发布半初始化对象，也不遗留
未提交的 active upload。

## 原有创建边界

原 Texture 的公开构造函数同时承担输入校验、Image 创建、ImageView 创建、上传录制和 flush。C++ 对象已经进入构造过程后，
其中任何一步都只能强失败。另一个“尺寸 + 纯色”构造函数复制了像素生成与 GPU 创建流程，使事务逻辑存在第二个入口。

这种接口把“持有一个 Runtime Texture”和“尝试执行一串 GPU 操作”混为同一件事，不适合作为异步资产发布的边界。

## 新创建契约

Texture 现在提供共享实现的双轨静态工厂：

- `Texture::create()`：保留关键资源与现有 ResourceManager 的强失败语义，内部使用 `within_budget=false`；
- `Texture::try_create()`：为非关键流送返回 `GpuResourceResult<std::shared_ptr<Texture>>`，并向 Image allocation 和 staging
  page growth 传递调用方选择的 `within_budget`。

私有构造函数只接收尺寸、完整 ImageView owner 和 ready completion。Runtime Texture 不再重复保存 Format；格式可由
`ImageView::get_image()->get_info()` 获得，当前 Texture 公共接口也不消费它。

## 事务顺序

`try_create()` 固定执行：

```text
校验 TextureData 和像素大小 / 解析 image states
  -> try_create Image
  -> try_create ImageView
  -> try_enqueue image upload
  -> flush batch
  -> 构造并返回 Texture
```

Image allocation 失败时没有 Image/ImageView wrapper 发布；ImageView 创建失败时局部 Image owner 正常释放；staging 失败时
UploadManager abort 整个未提交 batch，局部 ImageView 再按 `ImageView -> Image -> allocation` 顺序释放。只有 flush 产生
completion 后才构造 Texture。

输入尺寸、格式和像素长度仍属于 importer/调用方契约，非法输入继续强失败。像素长度计算增加 `size_t` 乘法溢出检查，避免
极端尺寸绕过精确长度校验。Queue submit 失败仍是不可由资源替换策略单独恢复的强失败。

## 删除重复入口

未被生产代码使用的“宽高 + 纯色”Texture 构造函数被删除。纯色纹理本质上也是 CPU `TextureData`，未来需要白色、法线或
错误占位纹理时，应由通用 CPU 侧辅助函数生成 TextureData，再进入唯一的 Texture 工厂。这样生成策略不与 Vulkan 对象创建
耦合，也不会出现第二套强失败/可恢复逻辑。

## ResourceManager 边界

`ResourceManager::create_texture()` 改为调用 `Texture::create()`，与 Mesh 对齐。`RenderResourceFactory` 本步仍保持原强失败
签名，避免在 Texture 事务尚未验证前同时改变资产发布接口。下一步会统一扩展工厂结果，再由 AssetManager 处理保留旧资源
和错误日志。

## 验证

- 编译期测试确认 Texture 的 recoverable 工厂结果类型；
- 编译期测试确认旧的 TextureData 构造函数和纯色 GPU 构造函数均不可直接调用；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 Image/ImageView/staging 失败的逐层回滚需要 Vulkan fault injection 或资源压力环境，仍保留为后续专项测试；自动化测试
不依赖具体 GPU 显存容量。

## 下一步

让 `RenderResourceFactory` / `ResourceManager` 暴露 Mesh/Texture 的可恢复创建结果，并在 AssetManager 的同步加载和热刷新发布
边界消费错误：候选创建失败时记录诊断、继续保留 Registry 中的旧 Runtime Resource。
