# 018：布局驱动的 Material Inspector

## 背景

017 已让 scalar/vector 真正参与 GPU 绘制，但 Inspector 仍只遍历文件里已有的纹理 map。
这意味着数值必须手改文件，省略的默认参数不可见，缺失纹理槽也无法在界面补齐。
本项完成内置材质的布局驱动编辑入口；不引入新的全局事件总线或 ImGui 专用引擎类。

## 前后对比

| 之前 | 现在 |
| --- | --- |
| 布局在 MaterialRenderer 构造函数中定义 | 不可变内置描述由 MaterialLayout::find_builtin 提供 |
| Inspector 按 `.mat` 已有 texture_properties 生成控件 | 按布局生成 Texture、Scalar、Vector/Color 控件 |
| 省略的参数没有控件 | 显示布局默认值，但不立即写回文件 |
| 没有某个纹理属性就无法选取 | 必需槽始终出现，可逐个补齐后自动发布 |
| UI 与 GPU 的参数规则容易分开维护 | 共用默认值、槽名及类型；UI 另使用人为定义的显示语义 |

## 真实代码变化

### 1. 一份内置描述，两类消费者

`MaterialLayout::find_builtin(name)` 返回共享的 `shared_ptr<const MaterialLayout>`。
描述移入现有 `material_runtime.cpp`，没有为查两个内置布局新增一个 manager/service 或独立目录。
MaterialRenderer 仍按 template 选择 Shader，但不再复制参数块、槽及默认值定义。

TextureProperty 增加 display_name；ScalarProperty 增加 min/max/step/display_name；VectorProperty 增加 Semantic 与 display_name。
这些是作者提供的语义提示，不含 ImGui 类型，也不能由 SPIR-V 自动推断。
例如 blend 的编辑范围为 0–1、步长 0.01，intensity 为 0–10、步长 0.05；tint/color 使用 Color 语义。
scalar 元数据的范围及步长在构造布局时检查有限性/顺序，默认值和 GPU packing 继续沿用已有校验。

### 2. 控件按布局读取，数据只在变化时写入

```cpp
float value = property.default_value;
if(found != m_material_data->scalar_properties.end())
    value = found->second;
const float before = value;
if(ImGui::DragFloat(...) && value != before) {
    remember_previous();
    m_material_data->scalar_properties[property.name] = value;
}
```

读取默认值不用 map 下标，避免仅渲染面板就把字段插入数据。
scalar 用 DragFloat，普通 vector 用 DragFloat4，Color 语义用 ColorEdit4；显示名为空时回退属性名。
Texture 控件按 layout 的槽名生成，继续使用类型化拖拽载荷和文档 generation 校验，不接收错误资产类型。
编辑已知槽时清理该名称的其他错误类型值，避免生成跨类型同名字段。

### 3. 保存、回滚和缺失资源修复

每次控件产生实际变化才记录 previous_data；没有变化不会调用 update_material。
有效候选沿用 `Inspector → EditorApp → AssetManager::update_material`，同步更新文件和 Runtime；
Editor 继续 acknowledge 这次源文件变更，避免自己的保存又触发一次监视重载。
只有对应资产对象/版本变化，MaterialRenderer 的其他材质缓存不失效；同值不触发保存。
回调失败或不存在时恢复本次交互前的数据；更新成功/失败日志仍在 Log 区，不新增 Inspector 更新日志。

缺失必需纹理时先保留本资产草稿并显示补齐提示，不发布不完整候选：

```text
空 texture map → 选择 Texture 0 → 草稿保留，不调用更新
              → 选择 Texture 1 → 候选完整，自动调用一次更新
```

这是修复入口，不是隐式自动保存：切换到其他资产、刷新或重新载入缓存会丢弃未完成草稿。
未知属性/错误类型会给出诊断，不自动删除用户数据；未知 layout 没有可编辑控件，不捏造默认模板。
模板切换及资产编辑撤销仍不在本项范围，Scene CommandHistory 不记录这些文件修改。

## 设计理由与架构价值

- UI 不再维护另一份参数表；默认值、槽及语义来自渲染侧同一份 CPU 描述。
- 引擎不依赖 ImGui，Inspector 不需要访问 Device、Pipeline 或 descriptor cache。
- 现有回调是一个编辑入口向一个 owner 提交请求，仍适合直接调用；没有真实一对多需求，不加 EventBus。
- 布局负责描述，MaterialData 负责持久化，AssetManager 负责发布，GPU 缓存负责版本寿命，职责不混合。
- 元数据查找是只读内置表，不是通用自定义 Shader 注册系统；后续反射只替代接口结构信息，不替代人为语义。

## 测试结果

- Debug/Release 构建及完整 CTest：各 439 tests 通过。
- 新增 7 个测试：内置描述共享/范围/颜色语义；默认值不保存；scalar 实际变化；失败恢复重试；
  缺槽自动发布；纯色无纹理参数；颜色控件编辑；草稿隔离等断言组合在这些测试中。
- 17 个 UI/MaterialRuntime 测试重复 30 轮，510 次通过。
- UI 使用真实 ImGui 输入与帧推进，按生成的控件 ID 查找位置，不直接篡改 Inspector 私有字段。
  独立两次拖动之间推进 UI 时间，避免把双击进入文本模式误判为重试失败。
- 原纹理拖拽失败恢复、过期 generation、错误类型拒绝、Scene 撤销、GPU 像素与跨帧寿命测试继续通过。
- 全量日志没有 VUID/Validation Error。自动化 UI 测试不等同于人工桌面体验检查。

## 限制与后续方向

仅编辑已知内置布局；尚无模板切换、自定义 layout 注册、参数重置按钮或资产 CommandHistory。
数值输入沿用 ImGui 编辑行为：拖动中每次真实变化都会保存，不是每次 frame 都保存；没有后台合并文件写入。
scalar 范围是交互提示，不代表修改已有文件时会自动裁剪所有数值。
布局校验目前主要服务 Inspector，未知 Shader/属性的完整资产 schema 契约需结合后续反射与注册机制扩展。
下一项：从实际 SPIR-V 生成 ShaderInterface，检查手工 layout 与 shader 的 set/binding/type/count/stage/push constant 是否一致。
