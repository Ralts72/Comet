# 020 可恢复 ImageView 工厂

## 目标

让 Texture 的可恢复创建链不仅覆盖 VMA Image allocation，也覆盖随后的原生 `VkImageView` 创建。ImageView 要么包装一个
有效 view handle 并持有父 Image，要么返回 Vulkan error；失败时不得发布空句柄 wrapper。

## 为什么 Image 成功还不够

Texture 的 GPU 对象链是：

```text
Texture -> ImageView -> Image -> VMA allocation
```

此前 `Image::try_create()` 已能返回 allocation 错误，但 ImageView 仍在公开构造函数中直接调用增强版 Vulkan-Hpp
`createImageView()`。即使 Image allocation 成功，view 创建仍可能因为 host/device memory 等错误失败。如果 Texture 直接在
这一步强失败，它对上层宣称的 recoverable contract 就是不完整的。

## 双轨静态工厂

ImageView 现在与 Buffer/Image 使用相同模式：

- `ImageView::create()` 是关键 render target 等现有调用点的强失败入口；
- `ImageView::try_create()` 调用返回 `vk::Result` 的 Vulkan-Hpp 原始重载，并返回
  `GpuResourceResult<std::shared_ptr<ImageView>>`；
- 私有构造函数只接收已经成功创建的 `vk::ImageView`。

强失败工厂委托可恢复工厂，因此两条路径共享 create info、subresource range 和所有权逻辑。所有原有 RenderTarget、Texture
调用点迁移到 `ImageView::create()`，不再通过公开构造函数隐式执行 Vulkan 工作。

## 所有权与失败清理

`try_create()` 在原生句柄成功后才移动父 Image shared owner 并构造 wrapper。失败时传入的 shared owner 随参数和调用方局部
变量正常释放；没有 ImageView 对象，也没有需要销毁的 view handle。成功对象仍保持原有
`ImageView -> Image -> allocation` 生命周期链。

空 Image 和空 aspect 是调用契约错误，继续强失败，而不是伪装成显存压力。析构函数额外保护空 handle，但公开工厂不会
产生空 handle 对象。

## 验证

- 编译期测试确认强失败/可恢复工厂的参数和返回类型；
- 编译期测试确认旧的公开 ImageView 构造方式不可调用；
- 所有 ImageView 生产调用点已迁移到静态工厂；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 `vkCreateImageView` 失败仍需要 Vulkan fault injection 或资源压力环境验证，不通过伪造无效 create info 来替代合法资源
失败语义。

## 下一步

将 Image、ImageView、recoverable image upload 组合为 Texture 事务：全部 GPU owner 和 enqueue 成功、flush 产生 completion
后才构造 Texture；同时删除 Texture 公开构造函数中的 GPU 副作用。
