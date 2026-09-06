# 016：场景／材质解析边界与布局驱动绑定

## 背景与验收边界

对应阶段 5「SceneResolver 不知道属性名、数量、binding」及手工布局/revision 缓存基础。
原来任意新增材质都要先修改场景解析器，且每帧逐物体读取相同两个字符串属性。
本项解除这层耦合；**不宣称多种 GPU pipeline、标量/向量或不可变 MaterialSet 已完成**。

## 前后对比与实际代码

之前：

```cpp
// SceneResolver::resolve_item
if(material->get_template_name() != "cube_texture") return std::nullopt;
std::array<std::shared_ptr<Texture>, 2> textures = {
    material->get_texture_property("u_Texture0"),
    material->get_texture_property("u_Texture1")};
// MaterialBinding 直接传两张纹理，SceneRenderer 再写死 binding 2/3。
```

现在：

```cpp
// 场景只交付资源身份与资源引用。
.material = {.material_handle = render_item.material_handle, .resource = material}

// 渲染侧选择布局；当前 cube 的映射在 setup_pipeline 中只声明一次。
m_material_layout = std::make_shared<MaterialLayout>("cube_texture", 1,
    std::vector<MaterialLayout::TextureProperty>{{"u_Texture0", 2}, {"u_Texture1", 3}});
const auto material = m_material_cache.prepare(item.material.material_handle,
    item.material.resource, m_material_layout);
```

`MaterialRuntimeCache::prepare` 遍历布局槽，生成携带 binding 的 Texture 快照。
SceneRenderer 的 DescriptorSetLayout、pool 数量和 descriptor writes 都读取该布局/快照，不再各写一份两纹理规则。
写 descriptor 前一次性分配全部 `DescriptorImageInfo`，避免 vector 扩容导致 `pImageInfo` 悬空。

新增 `render/material_runtime.h/.cpp` 作为统一入口，放在一起描述布局、准备结果、缓存，而不是为三个小类型拆三组文件。
`Material` 仍是资源属性对象；它不依赖 Vulkan、FrameScheduler、Editor 或缓存实现。

## revision、失败与回收逻辑

- Material revision 从 1 开始；新增/修改纹理属性递增，相同属性写回相同 Texture 不递增。
- 禁止直接赋值/拷贝 Material，避免绕过 setter 把整个属性表替换而不更新版本；需要复制资产时构造新对象。
- MaterialLayout 校验非空名称、非零 revision、槽名称/位置唯一，并按 binding 排序；构造后没有修改/赋值入口。
- 缓存比较 Material **对象身份 + revision** 和不可变 Layout **对象身份**。
  新 layout 即使误用相同 revision 也会重新准备；新 Material 即使 revision 与旧对象相同也不会命中旧缓存。
- 缺失纹理或 layout 不匹配记录一次错误并缓存失败；源/布局变化后再尝试，不逐帧刷屏。
- `PreparedMaterial` 经 `shared_ptr<const ...>` 交付，保留当时的 Texture 引用。
  材质后续改动不改变已经交付的快照。
- `collect_unused()` 每帧清掉本周期未使用的缓存项，再重置使用标记。
  这只是 CPU 缓存回收，不代表 GPU 完成；descriptor 原有 frame serial 回收和 FrameSlot Texture retention 继续生效。

## 设计理由与架构价值

属性/布局匹配属于渲染准备，不属于 Scene → Handle 解析。不同材质不再要求 SceneResolver 配合改代码。
这不是单纯把 array 改成 vector：同一份布局实际驱动 descriptor 声明、容量计算和写入，缓存消除未变化属性的重复解析。
先保留已经验证的按 slot descriptor 寿命，再独立迁移 Frame/Material/Object 所有权，便于定位同步回归。

## 测试结果

- Debug 全量构建与 `ctest --preset dev-debug --output-on-failure --timeout 120`：427 tests 通过。
- Release 全量构建与 `ctest --test-dir /tmp/comet-audit-release.lfw8ZZ --output-on-failure --timeout 120`：427 tests 通过。
- 7 个新增测试：布局排序/重复校验；缓存命中/版本变化；源和布局对象替换；未使用回收；失败恢复；
  真实 Texture 的三槽映射和旧快照；任意 template 的 SceneResolver；跨 slot 连续 8 帧纹理修改/Material 替换。
  多个断言主题在同一测试中覆盖，因此上述主题数不等于测试数。
- 新图形 fixture 开启 Vulkan validation，捕获日志断言无 VUID/Validation Error；完整测试日志也未发现这些错误。
- 既有场景当前帧编辑、拾取、DebugRenderer、资产后台发布测试全部通过。
- 上一步 015 Linux CI 34052585677 成功；本步 Linux CI 需推送后观察。没有把自动化测试当成人工画面验收。

## 限制与后续方向

当前只支持一套生产 cube GPU pipeline；空布局和三纹理布局验证的是准备层，不是三种 Shader 绘制效果。
MaterialRuntimeCache 只准备 CPU 绑定，GPU descriptor 仍由 SceneRenderer 按 slot 管理；Frame UBO 仍与材质纹理在同一 set。
任何属性变化会重新准备该材质全部槽，尚无字段级脏标记。布局目前手写，只描述纹理槽，不承担 SPIR-V reflection。
下一项接入 FrameSet/MaterialSet 分离、纹理/标量/向量参数和第二种实际 GPU 布局，再推进 RenderQueue/PipelineKey。
