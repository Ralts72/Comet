# 007 Render Resource GPU Wait

## 目标

把 Runtime Mesh/Texture 保存的 ready completion 接入实际 frame submission，形成“上传提交后立即发布、首次渲染由 GPU
等待”的完整链路。等待使用资源真实消费 stage，不让 AssetManager 或 AssetRegistry 承担 Vulkan 同步职责。

## 改动前

第 006 步已经取消资源创建时的 CPU wait，但安全性依赖 UploadManager 与 SceneRenderer 当前都使用同一 graphics queue：

```text
graphics queue: upload V -> frame draw
```

这在当前 backend 中正确，却把未来专用 transfer queue 的关键依赖只留在说明文字里。一旦上传和 draw 分属不同 queue，
资源虽然携带 completion，frame submission 却不会等待它。

## 改动后

解析与提交链路现在显式携带就绪前置条件：

```text
Mesh ready V1 ---- VertexInput ----\
Texture ready V2 - FragmentShader --- RenderSubmission.resource_waits
                                      -> merge by timeline semaphore
                                      -> frame submit waits max(V1, V2)
```

`RenderResourceWait` 只包含 `GpuCompletionPoint` 和 `PipelineStage`。SceneResolver 在成功解析一个 RenderItem 后：

- Mesh completion 标记为 `VertexInput`；
- Material 中实际绑定的 Texture completion 标记为 `FragmentShader`；
- 缺失资源导致 item 被跳过时，不产生无意义 wait。

没有有效主 Camera 时场景不会 draw，Renderer 也不会把资源 wait 传给 `end_frame()`。

## 提交去重

同一个 Mesh、Texture 或 upload batch 可能被多个实体引用。SceneRenderer 在提交前执行两项处理：

1. 已经完成的 completion 直接忽略；
2. 同一个 timeline semaphore 只保留一个 wait，value 取最大值，stage mask 合并。

timeline 的单调语义保证等待较大 value 同时覆盖同一 semaphore 上较小 value。stage 合并可能比逐资源等待稍保守，但避免
在一个 `VkSubmitInfo2` 中重复列出相同 semaphore，也不会丢失 VertexInput 或 FragmentShader 的可见性要求。

swapchain image-available binary wait 与资源 timeline wait 最终进入同一个 waits 数组；Queue 仍统一验证 semaphore 类型、
value 和非空 stage mask。

## 架构边界

- Mesh/Texture：声明自身 ready completion，不决定在哪个 pipeline stage 消费；
- SceneResolver：知道资源在当前渲染路径中的用途，因此生成类型化 wait；
- RenderSubmission：成为 draw 数据与 GPU 前置条件的完整只读交接对象；
- SceneRenderer：把前置条件编译为 Queue submit waits；
- AssetManager/AssetRegistry：继续只处理资源发布和类型化查询，不依赖 Queue/Semaphore。

这套结构不是只为当前 Mesh 写的特例。未来 compute skinning、storage image、indirect buffer 或 transfer queue 可以增加相应
stage，而无需修改资产身份和导入模型。

## 验证

- 编译期接口测试确认 SceneRenderer 的 `end_frame()` 接收类型化 resource waits；
- 单元测试确认 RenderSubmission 保存 completion 与 stage；
- SceneResolver 空场景和缺失资源测试确认不会产生孤立 wait；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 GPU 下 timeline self-wait、Validation Layer 与热加载帧时间观察属于人工图形验证项，不阻塞本次提交。

## 当前限制与后续

- 当前 upload/render 仍在同一 graphics queue，显式 timeline wait 是保守且可省略的；后续可让 Queue 识别自己产生的
  completion 并利用 queue order 消除该 wait；
- completion 完成前，连续多个 frame 可能重复提交同一个已满足/待满足 wait，正确但可进一步做 per-queue 消费缓存；
- 每个资源仍单独 flush，跨资产 batch 策略尚未实现；
- ready 解决的是“新资源何时可用”，没有解决热重载旧资源何时可销毁。

下一步建立 GpuRetirementQueue，把替换下来的 Runtime GPU owner 绑定到最后一次可能使用它的 frame completion；这会修复
现有热重载中 CPU shared_ptr 释放早于在途 draw 完成的生命周期缺口。
