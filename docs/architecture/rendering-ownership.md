# 渲染资源所有权

本文记录 Comet 当前渲染基础设施的所有权和生命周期约束。它描述的是资源由谁销毁，不代表 Scene、Asset 和 RenderItem 的最终 API。

## 所有权结构

```text
Engine
└── Renderer
    ├── RenderContext
    │   ├── Context
    │   ├── Device
    │   │   ├── VulkanAllocator
    │   │   └── default CommandPool
    │   └── Swapchain
    ├── ResourceManager
    │   ├── ShaderManager
    │   ├── SamplerManager
    │   ├── MaterialManager
    │   └── Mesh/Texture runtime cache
    └── SceneRenderer
        ├── RenderPass/PipelineManager/Pipeline
        ├── FrameManager
        ├── RenderTarget
        └── descriptor resources
```

`Renderer` 是当前渲染子系统的组合根。`RenderContext` 独占 Vulkan Context、Device 和 Swapchain；`Device` 独占 `VulkanAllocator`。`ResourceManager` 与 `SceneRenderer` 只保存指向 Device 或 RenderContext 的非拥有指针。

## 生命周期约束

关闭时必须遵循以下顺序：

1. 等待 Device idle。
2. 释放 Renderer 直接持有的 Buffer、Texture、Mesh 等资源。
3. 释放 SceneRenderer，确保 pipeline、descriptor、render target、command buffer 等对象先于 Device 销毁。
4. 释放 ResourceManager 及其 runtime resource cache。
5. 释放 RenderContext：Swapchain → Device → Context。
6. Device 内部先释放 CommandPool、PipelineCache 和 VulkanAllocator，再销毁 Vulkan Device。

任何 `Buffer`、`OwnedImage`、`Texture`、`Mesh` 或其他 VMA 资源都不得比创建它的 Device 活得更久。`BorrowedImage` 只包装外部 image，不负责释放该 image。

## 职责边界

- `RenderContext`：Vulkan 上下文、逻辑设备、交换链和 idle 等待。
- `ResourceManager`：运行时/GPU资源创建与缓存；不负责扫描项目目录或分配资产 GUID。
- `SceneRenderer`：帧同步、render target、pipeline、descriptor 和 draw command 录制。
- `MaterialManager`：Material/MaterialInstance 的内存注册；不存在的基础材质不能产生有效实例。
- `Scene`：只保存实体、可序列化组件和 `AssetHandle`，不保存 Device、GPU对象或文件路径。
- Asset Registry/Asset Manager：后续负责把 `AssetHandle` 解析为元数据和导入产物，再交给 ResourceManager 创建运行时资源。

## 帧同步

FrameManager 按 frame-in-flight 等待和复用同步对象，并记录每个 swapchain image 最近一次使用的 frame fence。只有在对应 fence 完成后才能重录该 image 的 command buffer。每个 frame-in-flight 必须独立持有会被 CPU 更新的 descriptor set 和 uniform buffer；纹理、sampler 等只读资源可以共享。正常呈现路径不得依赖每帧 `queue.waitIdle()`；Device idle 仅用于关闭和 swapchain 重建等全局同步点。

## 错误处理

- 违反引擎内部构造前置条件时使用 `LOG_FATAL` 记录诊断并立即终止，禁止部分初始化对象继续传播。
- Vulkan/VMA 创建失败必须立即终止当前创建流程，不能返回带空 handle 的可用对象。
- 可恢复的运行时状态，例如无效 AssetHandle 或缺失资源，应返回空结果并由提交层跳过，同时输出诊断。
- `eErrorOutOfDateKHR` 和 `eSuboptimalKHR` 触发 swapchain 重建；其他 present/acquire 错误不得被静默忽略。
