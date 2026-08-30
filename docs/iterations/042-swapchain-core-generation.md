# 042 Swapchain Core Generation 候选事务

## 目标

把 Swapchain 的 active core 从多个可被逐项覆盖的字段，收敛为一个完整 generation owner；新 core 先在局部候选中创建，只有 handle、
swapchain images 和 metadata 全部就绪才替换 active generation。

本步继续使用第 041 步的 Device idle 与同步 release/rebuild 边界，不同时引入 present completion 退休。

## 改造前

`Swapchain::recreate()` 直接依次写入：

```text
m_swapchain
m_images
m_config
m_current_index
```

`createSwapchainKHR()` 使用抛异常的便利重载，active handle 的赋值就是创建调用本身。后续 `getSwapchainImagesKHR()`、borrowed image 包装
或 CPU 容器分配如果失败，active 状态可能已经混合新旧字段，新 handle/old handle 的责任也不再清晰。

此外 config 和 current image index 与 handle 同属一代，却保存在 manager 外层；未来 dependent compatibility diff 和延迟退休无法拿到一个
稳定、可共享的 core 快照。

## 新的所有权

`SwapchainGeneration` 定义在既有 `swapchain.h/.cpp` 中，没有为它新增物理文件。它是 engine graphics 的 WSI core owner，不为 ImGui
服务，后续 runtime present target、editor present target 和 retirement 都会复用：

```text
Swapchain
└── shared_ptr<SwapchainGeneration> active
    ├── Device&
    ├── vk::SwapchainKHR
    ├── shared_ptr<BorrowedImage>[]
    ├── SwapchainConfig
    └── current image index
```

generation 不可复制/移动；共享的是 owning pointer，不复制 Vulkan handle。析构时先清空 borrowed image wrappers，再销毁 swapchain handle。
`Swapchain` 的既有 get/get_images/get_width/get_height/get_current_index API 继续委托 active generation，因此上层不需要跨层读取新字段。

## Prepare/Create/Commit

`Swapchain::recreate()` 先完成 surface capability 查询与纯 config 选择，再调用私有 `try_create_generation()`：

1. 在局部变量中构造 `VkSwapchainCreateInfoKHR`，`oldSwapchain` 指向当前 generation handle；
2. 使用返回 `vk::Result` 的非抛异常 Vulkan overload 创建 candidate handle；
3. handle 一成功就放入未发布 `SwapchainGeneration` owner，保证后续 CPU 异常不会泄漏 handle；
4. 查询 images 时处理 `VK_INCOMPLETE` 重试，并为每个原生 image 创建 BorrowedImage wrapper；
5. 所有字段完成后，才一次性替换 `m_active_generation`。

`vkCreateSwapchainKHR` 返回失败时，Vulkan 没有 retire old swapchain，函数返回失败且 active generation 不变；第 041 步会用旧 core 恢复刚释放
的 dependent。

## WSI 不可回滚边界

新 handle 一旦创建成功，规范语义上 old swapchain 已 retired，不能把“CPU 尚未 commit shared_ptr”误解成仍可 acquire old。因此本步对
handle 成功后的 image 查询失败采用明确 fatal：先销毁 candidate handle，再终止，诊断说明 old 已 retired。它不伪装成可以恢复旧 active。

后续更完整的实现可以把这种状态转为 no-present/retry recovery，但也只能围绕新 handle 恢复 dependent，不能回滚到 old acquire。当前先保证：

- create 前失败不污染 active；
- create 后失败不继续使用语义上已 retired 的 old；
- 任意 CPU wrapper 异常都有 candidate owner 承担新 handle 清理；
- successful commit 只发布完整 generation。

## 测试与验证

- RAII 测试确认 `SwapchainGeneration` 不可复制/移动；
- 接口测试确认 `Swapchain` 发布的是 `shared_ptr<SwapchainGeneration>` owner，而非裸 generation 指针；
- Debug 全量构建；
- 完整 CTest。

Vulkan WSI create 失败注入和真实窗口 resize 仍需要平台 surface/driver 集成环境；纯 config 选择测试继续覆盖 Deferred、Unsupported、extent、
format 和 present mode 规则。

## 下一步

让 `SwapchainTarget` 和 editor ImGui target 保存构造它们所基于的 generation shared owner，而不是只借用可变 `Swapchain&`。同时增加纯
compatibility diff：extent 只重建 attachments，image count 更新 per-image state/ImGui backend，format 变化使 RenderPass/Pipeline 与
ImGui backend 一并失效。完成 parent/dependent generation 后，再把 old core 接到 graphics + presentation 完成语义。
