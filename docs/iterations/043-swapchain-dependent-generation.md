# 043 Swapchain Dependent Generation 与兼容性差异

## 目标

让引用 present images 的 RenderTarget 显式持有对应 `SwapchainGeneration`，并把 extent、format、image count 的失效差异变成纯数据，
避免 dependent 继续通过可变 Swapchain manager 获取“当前”images。

## 所有权改造

`RenderTarget::create_swapchain_target()` 和 `SwapchainTarget` 现在接收 `shared_ptr<SwapchainGeneration>`。target 的 framebuffer、image view
和 borrowed image 都来自该固定 generation；begin render 也读取同一 generation 的 current image index。

因此 target 不会出现“framebuffer 属于 old generation，但 begin 时从 manager 读到 new current index/images”的跨代组合。即使未来旧 target
进入延迟退休，它也会自然延长 core handle/images 生命周期。这个 engine 类型同时服务 runtime target 与 editor 最终 present target，
不是为 ImGui 单独添加的包装。

## Compatibility Diff

`compare_swapchain_configs()` 是无 Vulkan 调用的纯函数，返回：

- `extent_changed`：重建 framebuffer/attachments；
- `format_changed`：RenderPass/Pipeline/ImGui backend 兼容对象失效；
- `image_count_changed`：FrameScheduler per-image state 与 ImGui backend image count 失效。

SceneRenderer 在 core commit 前保存 previous config，成功后计算一次 diff 并传给 editor rebuild callback。重建延期或 create 失败仍使用旧 core，
因此传空 diff，只恢复刚释放的 target。

ImGui 在单纯 extent 变化时只创建新 target；format 或 image count 变化才 shutdown/reinit Vulkan backend，format 变化还会重建 editor render
pass。Viewport 离屏 descriptor 不因 extent-only swapchain 变化失效。

runtime 的 format-dependent pipeline generation 尚未完成；如果 surface format 真正变化，当前明确 fatal 并说明缺失能力，不再静默拿旧
RenderPass/Pipeline 创建不兼容 framebuffer。后续应由 Renderer 组合根基于同一 diff 重建整组 pipeline generation。

## 测试与验证

- 纯单元测试验证相同 config 无变化，extent/format/image count 分别能被识别；
- callback 接口测试要求 rebuild 接收类型化 compatibility；
- Debug 全量构建与完整 CTest。

真实 format 变化和窗口 resize 的 validation layer 验证仍需要平台窗口环境。

## 下一步

为 old swapchain generation 建立 retirement：graphics 侧使用最后提交 completion，presentation 侧优先使用可用 completion/fence，当前平台
缺失时只等待 present queue。完成后移除正常 swapchain recreation 的 Device-wide idle，同时保留 shutdown/device-lost 的安全回退。
