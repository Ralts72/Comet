# 040 Viewport 物理分辨率上限

## 目标

为第 039 步的离屏 generation 创建补上前置约束，避免超大浮动窗口、异常 framebuffer scale 或未来自定义固定分辨率把目标尺寸
无限放大。约束必须同时满足 Vulkan 设备能力和 editor 的资源策略，并在进入 Renderer 前完成。

## 边界选择

本步没有新增资源管理类。`DeviceCapability` 增加只读的 `max_image_dimension_2d`，在物理设备选择时从
`VkPhysicalDeviceLimits::maxImageDimension2D` 捕获；Editor 创建 ViewPanel 时把它与 4096 的软上限取较小值。

两层上限含义不同：

- Device 上限是创建任何 2D image 都不能越过的硬约束；
- 4096 是 editor 单 Viewport 的软策略，防止虽然设备允许 8K/16K image，但一次 resize 为每个 frame slot 创建巨大的
  color/depth/MSAA 附件；
- format/sample-count 的更细粒度限制仍由第 039 步的可恢复 Image 创建兜底，不能只靠通用 dimension capability 假设必然成功。

## 布局规则

`ViewportLayoutInput::max_render_dimension` 是纯值输入，0 只在独立布局调用和测试中表示“不额外限制”。生产 Editor 总是传入有效的
设备/编辑器合并上限。

计算顺序为：

```text
panel logical size + DPI
  -> Free / 16:9 / Fixed requested resolution
  -> max dimension proportional constraint
  -> final physical render resolution
```

约束在 resolution policy 之后统一执行，因此 Free、16:9 和 Fixed 没有三套截断逻辑。长边超过上限时直接设为上限，短边按同一比例
向下取整且至少为 1；不会分别 clamp 宽高造成非等比拉伸。显示矩形仍使用当前实际纹理尺寸，所以 generation debounce/切换期间不会
提前按照尚未生成的尺寸重排画面。

## 为什么不放在 SceneRenderer

SceneRenderer 只消费最终物理像素请求并负责 GPU generation 事务。若在这里再次截断：

- ViewPanel 的 layout.render_resolution 会与 Renderer 实际尺寸不一致；
- Camera aspect、1x 显示和后续鼠标到纹理坐标映射会出现两套真值；
- UI 策略会反向渗入 engine render 层。

因此 Device 只暴露 capability，Editor 组合软策略，ViewportLayout 产生唯一最终尺寸。

## 测试与验证

- Free 的横向和纵向超限尺寸都按比例收敛到 4096；
- 16:9 在约束后仍为 4096×2304；
- Fixed 也经过同一最终约束；
- 原有 DPI、Fit/1x、无效输入和固定分辨率测试保持通过；
- `cmake --build --preset dev-debug --parallel`；
- `ctest --preset dev-debug --output-on-failure`。

## 下一步

在继续更大的 swapchain generation 改造前做边界审计，重点确认：Swapchain core handle/images、runtime SwapchainTarget、
FrameScheduler image state、RenderPass/Pipeline 兼容对象和 editor ImGui backend 各自由谁 prepare/commit/retire，以及缺少 present completion
能力的平台在哪里使用最窄的 present-queue idle 回退。
