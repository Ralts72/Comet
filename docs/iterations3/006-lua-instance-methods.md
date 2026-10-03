# 006：让脚本实例支持自然的辅助方法调用

## 背景与证据

组件模板已经采用 `local script = {}`、`function script:update(dt)` 和 `return script`。
但作者把复杂逻辑拆成 `function script:helper()` 后，`self:helper()` 会失败。
004 编写 demo 调色逻辑时实际出现过：

```text
scripts/spin.lua:54: attempt to call a nil value (method 'set_palette_active')
```

当时先以 `local function helper(self)` 收尾并验证输入功能。原诊断仍在
`/tmp/comet-auto3-004-demo-diagnostic.log`。本项补齐实例模型，而不是继续要求每个项目绕开方法调用。

## 原因与最小修复

原来的初始化分别保存两张表：

```cpp
vm.definition = luaL_ref(state, LUA_REGISTRYINDEX);
lua_newtable(state);
vm.self = luaL_ref(state, LUA_REGISTRYINDEX);
```

`definition` 是脚本返回的定义表，包含回调、辅助方法和声明；`self` 是另一张空表。
调度时从前者取 `update`，传后者作为第一个参数。Lua 的冒号语法实际等价于：

```lua
self.helper(self, argument)
```

问题是 `self.helper` 查不到，不是函数无法定义，也不是组件缺少另一个 C++ 类型。

现在保留两张表，只给 `self` 安装一个宿主管理的私有元表：

```cpp
vm.definition = luaL_ref(state, LUA_REGISTRYINDEX);
lua_newtable(state);
lua_newtable(state);
lua_rawgeti(state, LUA_REGISTRYINDEX, vm.definition);
lua_setfield(state, -2, "__index");
lua_setmetatable(state, -2);
vm.self = luaL_ref(state, LUA_REGISTRYINDEX);
```

查找关系成为：

```text
self 自有字段
  ├─ 有：直接使用实例值
  └─ 无：查找本 VM 的 definition
```

新增动作仍在已有 Lua 保护调用内，失败沿原 Result 路径返回。没有新公共 API、类、文件、
函数复制表、方法缓存或第二套生命周期。

不能直接把 `definition` 当成元表：那会把项目自己定义的 `__gc`、`__newindex` 等普通字段
意外启用为实例元方法。本项新建的私有元表只有 `__index`；脚本仍不能调用 `setmetatable/getmetatable`。

## 项目代码前后对比

之前 demo 被迫显式传 self：

```lua
local function apply_palette(self)
    local color = palette[self.palette_index]
    comet.set_material_vector("base_color", color[1], color[2], color[3], color[4])
end

local function set_palette_active(self, active)
    self.palette_active = active
    comet.set_input_context("palette", active)
    if active then apply_palette(self) end
end
```

现在使用与生命周期相同的定义方式：

```lua
function script:apply_palette()
    local color = palette[self.palette_index]
    comet.set_material_vector("base_color", color[1], color[2], color[3], color[4])
end

function script:set_palette_active(active)
    self.palette_active = active
    comet.set_input_context("palette", active)
    if active then self:apply_palette() end
end
```

`update` 调用 `self:set_palette_active(...)`；后者再调用 `self:apply_palette()`。
按键、优先级、消费、颜色、计分和重开规则都没有变化，仍由原 demo 行为回归验收。
自由函数和 `local function helper(self)` 仍然合法，不要求所有函数都改成方法。

## 保持的生命周期与边界

- 每实体 VM 独立执行定义和模块，因此回退的普通 table、闭包和模块状态也不跨实体共享。
- `self` 自有字段遮蔽定义字段，赋值写入实例；删除自有字段后恢复定义查找。
- 这是普通表查找，不只返回函数。`pairs(self)` 仍只枚举实例自有字段。
- 只有显式 `properties` 被解析成编辑字段；辅助方法、普通常量和运行变量不会自动进入 Inspector。
- `self.parameters` 仍为引擎安装的只读配置。修改 Lua `self.properties` 不会改写已发布的 C++ 默认值。
- 引擎仍从 `definition` 取生命周期和已声明事件入口。给 `self.update` 赋值不会重绑定引擎下一帧的入口。
- helper 与调用者共用一次保护调用、指令与内存预算，不因多一层方法重置预算。
- 成功重载仍重建整个 VM／self／定义／模块；失败候选仍保留旧组，暂停等单步或继续才切换。
- helper 本身运行报错仍是运行时错误，可能已经产生世界副作用；不承诺回滚世界或恢复旧脚本版本。

## 验证

本项首次构建及定向测试通过，没有失败后反复重跑。

```sh
cmake --build --preset dev-debug --parallel 6
build/tests/unit_testing --gtest_filter='Script*:*ScriptAsset*:EditorAssetsTest.*Script*:EditorAssetsTest.*LuaModule*'
ctest --preset dev-debug -L '^(cpu|ui)$' --output-on-failure -V
cmake --build --preset app-release --parallel 6
```

- Debug 全目标、Release app 构建通过，无编译 warning/error。
- 定向 103 项通过，包含实际 demo 调色、得分与重开消费者。
- CPU 955 项、无窗口 UI 159 项通过；原生监听平台不执行的轮询回退用例跳过 1 项。
- `shader_build_contract`、`module_boundaries` 通过。
- 5 个实例层新增用例验证嵌套方法／返回值、实例和模块可变表隔离、只读参数、私有元方法边界，
  以及异常、缺失方法、循环／尾递归和内存预算失败后的受保护清理。
- 扩展原成功重载用例，并增加整组预备失败用例：两旧实例继续使用旧 helper 与累计状态，
  修复阻塞新实例的参数后整组切新版；暂停、单步、Stop／再次启动和兼容覆盖保持原协议。
- 没有修改渲染或 Window；本项不重复 005 的 GPU 冒烟，不把旧结果当作新增手工验证。
  真实键鼠、颜色与声音仍待人工验收。

日志：`/tmp/comet-auto3-006-first-{build,targeted}.log`、
`/tmp/comet-auto3-006-final-{regression,release}.log`。

## 手动验证

1. 在 demo 中 Play 或运行 app，Tab 进入调色，左右方向键切换、J 重置、空格确认。
   功能应与 004 相同，Log 不应再出现 `set_palette_active` 的 nil method 错误。
2. 从 New Script 创建组件，按 README 的 `script:move(dt)` 例子定义辅助方法并在 update 调用。
   挂载到实体后运行，应按参数移动，不需 CMake 或宿主重新编译。
3. Play 中修改辅助方法的实现并保存，继续运行时切换到新版；暂停时等单步或继续。
4. 换版后实例计数重新开始；Stop 返回 Edit，不保存这些运行变量。

## 限制与后续

不引入用户自定义构造函数、类继承框架、脚本间直接访问实例或任意状态迁移。
方法查找不等于自动反射，不开放内部 ECS 对象或元表 API。
如果后续需要调试器，应基于当前明确的定义／实例关系扩展，而不是另建并行脚本对象模型。
