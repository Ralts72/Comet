# 022 可恢复资产 Runtime Resource 发布

## 目标

把 Mesh/Texture 的可恢复 GPU 创建结果接入资产发布边界。显存预算或资源创建失败不再被压缩成空指针或直接终止进程；
AssetManager 能记录具体 Vulkan error，并确保 Registry 只接收完整候选，热刷新失败时继续保留旧 Runtime Resource。

## 工厂接口为什么必须改变

此前 `RenderResourceFactory::create_mesh/create_texture()` 只返回 shared pointer。测试替身可以用空指针模拟失败，但真实
ResourceManager 调用的是强失败工厂，GPU allocation、ImageView 或 staging 失败无法返回 AssetManager。因此“刷新失败保留旧
对象”只覆盖 importer/测试空指针，尚未覆盖真实 GPU 资源压力。

新的资产侧窄接口为：

```text
RenderResourceFactory::try_create_mesh(data)
RenderResourceFactory::try_create_texture(data)
  -> GpuResourceResult<shared_ptr<RuntimeResource>>
```

ResourceManager 的 override 固定使用 `within_budget=true`，把项目资产创建视为可延迟、可保留旧版本的流送工作。它仍保留
显式 `create_mesh/create_texture()` 强失败方法，供未来真正的启动关键资源使用；两种政策不会用一个含糊的 bool 暴露给
AssetManager。

## AssetManager 发布顺序

首次同步加载与后台 Mesh completion、同步 Texture refresh 现在都遵循：

```text
导入 CPU data
  -> try_create Runtime Resource
  -> 失败：记录 Vulkan error，返回/跳过
  -> 再次验证 revision（Mesh）
  -> register 或 replace Registry
```

`GpuResourceResult` 失败不包含 wrapper，因此 Registry 不可能收到半初始化对象。后台 Mesh 创建仍保留异常保护，用来隔离测试
callback、CPU allocation 等非 Vulkan 异常；普通 GPU 失败走显式 result，不依赖 exception。

## 保留旧资源

- 首次加载失败：返回 `nullptr`，不注册 Handle；
- Mesh 后台刷新失败：跳过 replace，原 Registry Mesh 保持不变；
- Texture 扫描刷新或 Inspector reimport 失败：`create_runtime_texture()` 返回空，后续 metadata 更新、replace 和依赖 Material
  reload 都不会发生；
- 成功候选仍遵守 Mesh revision 验票，防止旧结果覆盖更新状态。

错误统一通过 Logger 输出，Editor 会在 Log 面板展示，不把资源更新日志塞进 Inspector。

## 验证

- FakeRenderResourceFactory 改为返回与生产接口相同的 `GpuResourceResult`，以 `eErrorOutOfDeviceMemory` 模拟资源失败；
- 测试确认 Texture 首次加载只创建一次并按 Handle 缓存；
- 测试确认已加载 Texture 在新候选 GPU 创建失败后仍保留原 Registry 对象；
- 既有测试继续确认后台 Mesh Runtime 创建失败保留旧对象；
- 完整 Debug 构建；
- `ctest --preset dev-debug --output-on-failure`。

真实 GPU 错误的端到端触发仍需要 fault injection；本步通过类型化结果和确定性 fake 验证发布政策，不依赖机器显存状态。

## 当前限制与下一步

- 当前失败结果只有 `vk::Result`，足以区分和记录 Vulkan 失败，但尚未携带 allocation 名称、请求字节数或资产上下文；这些
  上下文分别由底层强失败日志和 AssetManager Handle 日志提供，暂不增加重复错误对象；
- Texture import/创建仍在 Owner Thread 同步执行，Mesh 才具备后台 CPU importer；
- 本步完成 GPU 可恢复创建纵向链，下一步先做一次阶段性架构复盘，检查双轨工厂、结果类型、batch 边界和日志是否存在重复
  或不合理依赖，再决定 fault injection 与 Texture 后台加载的先后顺序。
