# Comet 使用手册

## 骨骼角色与动画

角色场景使用引擎内置的 Quaternius 人形模型和 `idle`、`walk`、`run`、`crouch`、`crouch_walk`、`jump`、`fall` 七个动作。
资源位于 `engine/resources/characters/default.glb`，由 Git LFS 保存，启动不需要联网；来源与裁剪说明见同目录 `source.json`。
Shift／LeftShoulder 跑步，Ctrl／RightShoulder 蹲伏；右键拖动／右摇杆转动相机，滚轮或 PageUp／PageDown／D-pad 上下缩放。
相机遇到墙壁会缩短距离，离开障碍后恢复。

在层级中选中“角色模型”：

- “网格渲染 → 网格”可在引擎默认角色与项目导入的 glTF／GLB 之间选择。
- “网格渲染 → 材质”可选择引擎默认 PBR／无光照材质；默认角色使用中性的 PBR。修改配色、贴图或参数时，在项目中创建自己的材质并替换该引用。
- “动画”中选择片段、播放速度、循环与过渡时间；片段下拉框来自当前模型，空选择表示绑定姿态。
- 指定“驱动角色”后，根据角色的实际移动状态选择对应动作，各动作可独立替换；清空该引用后按“播放片段”播放。
- Edit 修改可保存、撤销；Play 中的时间和姿态只属于运行副本，暂停停止推进，单步推进一次。

首版 Mesh 资产支持一个 mesh／skin、每顶点最多四个骨骼权重，以及 TRS 的 Linear／Step／CubicSpline 动画。
同一 Mesh 的多个实体独立播放，主绘制和阴影共用 GPU 蒙皮结果。模型内多个 primitive 使用所指定的同一个材质。
替换角色时，应同时选择新模型自身的动作；跨骨架重定向、独立动画库、root motion、IK 和 morph target 尚未接通。
动作名称找不到时，模型显示绑定姿态，Console 报出实体和缺失动作；重新选择有效片段后恢复播放。
若新片段有效、只有旧过渡片段在替换模型后丢失，则直接播放新片段。其他对象继续绘制，保存的动作映射保持原值。

首次构建和 Demo 入口见 [README](../README.md)。本文说明具体操作与接口；模块实现见[架构文档](architecture/overview.md)，后续能力见[路线图](engine-roadmap.md)。

## 项目与配置

默认角色、基础动作、默认 PBR／无光照材质、共用字体和图标由引擎提供，不需要复制到每个项目，也不生成项目 `.meta`。
材质默认参数沿用引擎的材质布局定义；demo 的黄色地面、贴图材质和条纹 Shader 用于展示项目效果，仍属于项目资源。
输入动作、场景灯光、相机初始位置、菜单主题、关卡数值和游戏规则由项目配置。

### 打开项目

```bash
./editor.sh ./demo
./editor.sh /Projects/MyGame/project.json
./release.sh /Projects/MyGame
```

App 和 Editor 接受项目目录或 `project.json`，相对路径以调用者的工作目录为基准。
“文件 → 新建项目”生成项目描述和含主相机的 `assets/scenes/main.scene`；“打开项目”和“最近项目”选择现有项目。
新建使用尚不存在的目录，切换前处理未保存场景；Play 时先停止运行。

“文件 → 切换场景”按相对路径列出同项目场景，当前项带勾选；“打开场景”及其快捷键仍可浏览文件。
切换复用保存／放弃／取消提示，不改变“项目 → 启动场景”设置。刚保存、尚未索引的当前场景也可设为启动场景。
Editor 恢复 `.comet/editor/session.json` 记录的上次场景，失败再尝试启动场景；App 始终使用项目启动场景。

项目至少包含 `project.json` 和 `assets/`，例如：

```json
{
  "version": 2,
  "id": "817a665a-34e6-422c-8d81-32b8e795087c",
  "name": "My Game",
  "startup_scene": "scenes/main.scene"
}
```

`startup_scene` 相对 assets；项目 UUID 用于玩家设置隔离，改名／移动目录不改变身份。
创建独立项目使用新项目入口。项目及资产格式只接受当前版本，场景、材质和 `.meta` 使用 JSON。
Editor 写入项目描述及资产配置；关闭 Editor 后可以手动修改，重新打开时校验。

### 配置归属

| 数据 | 位置／编辑入口 |
| --- | --- |
| 开发者诊断、设备参数与资源预算 | 根目录 `config.json`，按构建 Profile 选择分组 |
| 游戏默认显示、画质、音量与输入 | `project.json`，项目 → 设置 |
| 玩家选择 | 用户目录 `players/<项目 UUID>/default/` |
| 窗口尺寸、面板布局、快捷键、最近项目 | 用户状态目录 |
| 场景内容与组件配置 | 项目 `.scene`；Inspector 与场景保存 |
| 缓存、日志和上次场景 | 项目 `.comet/` |

Profile 缺省字段使用 C++ 默认值，不合并其他分组；修改后重启生效。宽高、VSync、画质和音量通过项目或游戏菜单配置。
`config.json` 中的 `diagnostics.enable_file_logging` 决定是否生成项目 `.comet/logs/` 日志；Profiler 还需构建支持及 `enable_profiler`。

用户状态目录在 macOS 为 `~/Library/Application Support/Comet/`，Windows 为 `%APPDATA%/Comet/`，
Linux 为 `$XDG_STATE_HOME/comet/`，未设置时使用 `~/.local/state/comet/`。
玩家目录在 macOS／Windows 位于上述 Comet 目录；Linux 使用 `$XDG_CONFIG_HOME/comet/` 或 `~/.config/comet/`。

Editor 的 `window.json` 保存普通窗口尺寸和最大化状态，`imgui.ini` 保存面板布局；删除相应文件可恢复默认。
App 的普通窗口尺寸属于玩家显示设置。项目配置、玩家设置和编辑器本地状态分别保存。

## 编辑器使用

### 视口与运行

Editor 使用一个视口：Edit 为编辑相机，Play 为运行副本的主相机；Stop 恢复 Edit 内容和历史。
左侧按钮提供运行、停止、暂停／继续和单步。“预览”选择分辨率与适应／1:1 显示；Play 游戏 UI 使用同一项目页面。

Edit 中右键或 Option/Alt+左键环绕，中键或 Option/Alt+Shift+左键平移，滚轮缩放；左键选择，F 聚焦选中网格。
工具菜单选择移动／旋转／缩放、世界／局部空间和吸附。缩放使用局部空间；Esc、失焦或隐藏视口取消拖动。
“视图 → 碰撞体”显示盒、球、角色胶囊和关节反馈；关节字段旁“对齐”按钮调整锚点／方向，Edit 支持撤销。

Edit 属性修改及结构操作支持 Undo／Redo，一次拖动记一条历史；Play 修改仅用于运行调试。
默认快捷键为 Ctrl+Z／Ctrl+Y，macOS 为 Cmd+Z／Cmd+Shift+Z；可在“编辑 → 快捷键设置”修改。

### 实体、资产与材质

Hierarchy 右键创建、重命名、复制或删除实体子树；拖动修改父级并保留本地 Transform。
Ctrl/Cmd+C、Ctrl/Cmd+V 使用进程内子树剪贴板；粘贴生成新 UUID。Ctrl+Backspace／Cmd+Backspace 删除选中子树。
Inspector 提供组件、参数和按类型过滤的资源引用，Edit 支持从 Project 拖入引用。

Project 右键可新建 Material、Script 或 Lua Module；搜索按文件名／目录名筛选，Refresh 重扫，Reimport 重建产物。
移动／改名资产时保留 `.meta` 身份，场景移动同步更新保存路径、启动场景和会话；删除经确认后送入系统回收站。
当前场景、启动场景或被其他资产引用的文件需要先解除对应使用；恢复资源时同时恢复源文件和 `.meta`。
资产删除独立于场景 Undo。

系统文件管理器可拖入 PNG/JPEG、HDR、glTF/GLB。glTF 连同相对依赖复制到项目，获得新身份。
暂不导入整个目录或 Lua 多文件依赖包。Mesh 拖入视口后，在 Inspector 指定材质；当前 glTF 导入为静态网格。

基础材质模板为 `pbr` 和 `unlit_color`。PBR 包含基础颜色／纹理、金属度和粗糙度，支持直接光、方向光阴影与全局 IBL。
材质参数拖动实时预览，松手保存；Esc 取消。材质资产使用独立 Undo 历史，模板切换保留兼容参数并提示不兼容项。

选中 Hierarchy 的“场景”，可编辑 Environment、曝光与 Bloom。背景和环境照明分别控制，隐藏天空盒仍可保留照明。
这些参数保存到 `.scene`；Play 中场景面板只读，运行时代码仍可修改运行副本。

### 示例场景

`scenes/character.scene` 提供角色移动、跳跃、台阶、坡度、移动平台及三目标收集；WASD／左摇杆移动，空格／South 跳跃，R 重开。
Gameplay 负责动作读取与跳跃请求，Physics 负责胶囊求解；Demo Lua 保存计分、完成、声音和重开规则。
角色以脚底为原点，使用无父级的竖直胶囊和正等比缩放；外观与跟随相机作为子实体。
移动以相机的水平朝向为参照，前方标记随外观转向，相机不会跟着外观旋转。

`scenes/default.scene` 保留刚体、关节、材质和脚本演示：左右方向键移动，Space／South 切换旋转，J／K 施加冲量／力，
Tab／North 开关调色，R 重开。开启调色时相关动作由输入组消费，公共动作仍可使用。

## 输入与玩家设置

### 项目默认与动作组

“项目 → 设置 → 输入”配置动作、绑定和上下文，保存到 `project.json`。
动作和绑定具有稳定 UUID，名称用于 Lua 查询；改名后同步更新代码引用。类型如下：

| 类型 | 输入与语义 |
| --- | --- |
| `button` | 键／鼠标按钮／手柄按钮，查询电平与按下／释放边沿 |
| `axis` | 数字按键及手柄轴，合成到 [-1, 1] |
| `delta` | CursorX/Y、ScrollX/Y，保留位移单位 |

倍率 `scale` 用于方向与强度；手柄轴的 `deadzone` 控制死区。标准手柄使用首个已连接设备。
没有上下文的动作属于公共组；具名组可通过 `comet.set_input_context(name, enabled)` 请求启停。
优先级高且开启 `consume` 的组屏蔽低优先级组中的相同控制，同级共享；公共动作不参与屏蔽。

Lua 使用 `comet.action_value(name)`，或按钮专用 `action_down/pressed/released(name)`。
固定步保留零步帧的短按，多次补步只交付一次边沿；普通更新使用独立快照。Stop／重开恢复项目默认组状态。
默认相机动作和绑定示例见 [demo/project.json](../demo/project.json)。

### 显示、画质和音量

项目设置为新玩家提供默认值；游戏“设置”菜单修改本机玩家选择，恢复默认先恢复草稿，再应用。
App 与 Editor Play 共用设置能力和项目页面；显示、画质、音量及改键各自提交。

| 设置 | 选项 |
| --- | --- |
| 显示 | 宽高、窗口／无边框／全屏、VSync、帧率上限、HDR 输出与校准 |
| 画质 | MSAA 1／2／4／8、各向异性 1～16、渲染比例 0.5～1 |
| 音量 | 主音量、音效、音乐；音源选择分类 |

App 更改尺寸、窗口或输出模式后试用 15 秒，确认才保存；超时或取消还原。
只改 VSync、帧率上限或 HDR 校准直接应用。帧率上限为 0～1000，0 表示不限，预设包含 30／60／120／144／240。
VSync、GPU 和调度可能使实际帧率低于上限，限制不改变物理固定步频率。

Editor Play 的宽高用于预览，窗口模式、VSync、帧率限制及 HDR 属于独立 App；画质和音量仍可调试。
画质选项受设备能力限制，渲染比例只缩放场景，游戏 UI 保持输出分辨率。
最终音频增益为音源音量 × 分类音量 × 主音量；长音乐流式播放仍在规划中。

### 玩家改键与保存

Demo 点“设置”、F1 或手柄 Start 打开菜单；Tab／方向键／手柄方向键导航，Enter／South 确认，Esc／East 返回。
录入只接收新的按下边沿，Esc、失焦或输入中断取消录入；打开菜单及关闭当帧阻断游戏输入。
恢复绑定／动作／全部删除对应覆盖并重新继承默认；取消不保存。

个人文件位于 `players/<项目 UUID>/default/`，包含 `input.json`、`display.json`、`quality.json` 和 `audio.json`。
改键按稳定身份保存稀疏覆盖，项目新增绑定仍可继承；文件缺失使用默认值，已有错误文件报告诊断并保留。
保存失败保留草稿及运行配置；“已保存但未应用”可再次应用。本机设置由当前宿主保存，运行中不支持外部同时改写。

Editor Play 的“视图 → 输入”打开 ImGui 玩家面板，可筛选动作、修改控制／倍率／死区并清理失效覆盖；
项目默认新增动作和绑定仍通过项目输入面板完成。宿主保留的 Esc、F1、F6 会在录入时提示。

## Lua 与 Gameplay

### 行为与参数

Project 右键 New Script 创建组件脚本，将其指定到实体的 Script 组件后在 App／Play 执行。
脚本返回 table，生命周期包括 `on_start`、`fixed_update(dt)`、`update(dt)` 和 `on_stop`；内部状态保存在各实体独立的 `self`。

```lua
local script = {properties = {speed = 1}}

function script:update(dt)
    comet.translate(self.parameters.speed * dt, 0, 0)
end

return script
```

`properties` 声明 bool、float、string、Vec3、Vec4 和实体引用，Inspector 只保存编辑过的参数覆盖。
`self.parameters` 为只读配置；三／四分量数组表示 Vec3／Vec4，颜色及实体引用显式声明：

```lua
script.properties = {
    tint = {type = "color", default = {0.2, 1, 0.25, 1}},
    target = {type = "entity"},
}
```

颜色按线性 RGBA 使用，不根据变量名猜测类型。实体字段支持下拉选择和 Hierarchy 拖入。
参数右键“使用脚本默认值”删除单项覆盖；“恢复默认参数”或“移除不兼容参数覆盖”支持场景撤销。

Project 右键“打开源码”使用外部编辑器；macOS 优先 VS Code。Lua 支持 UTF-8、可选文件头 BOM、LF／CRLF。
Editor 保存源码后重载关联行为：暂停时等待继续或单步；成功重建实例并重新执行 `on_start`，会话值保留，任意 `self` 状态不迁移。
语法／声明错误保留旧版本并报告；新实例的启动执行错误会停止 Runtime，Editor 恢复 Edit。独立 App 不自动监视源码。

### Lua 实体与会话

| 接口 | 作用 |
| --- | --- |
| `comet.self_entity()`／`comet.find_entity(uuid)` | 获取本实体或 UUID 引用；引用提供 `:is_valid()`、`:position()`、`:translate()`、`:rotate()` |
| `comet.position()`／`translate(x,y,z)`／`rotate(x,y,z)` | 查询／修改本实体变换；角度为度 |
| `comet.create_entity(name, options)`／`destroy_entity(entity)` | 阶段末创建实体或删除子树，只修改运行场景 |
| `comet.has_rigid_body(entity)`／`remove_rigid_body(entity)` | 查询／阶段末移除刚体，保留实体及其他组件 |
| `comet.apply_impulse(x,y,z)` | 本实体质心处的世界空间冲量 |
| `comet.apply_force(x,y,z)`／`apply_torque(x,y,z)` | 下一固定步的世界空间力／力矩 |
| `comet.apply_impulse_at(ix,iy,iz,px,py,pz)`／`apply_force_at(...)` | 在指定世界位置施加冲量／力 |
| `comet.linear_velocity()`／`angular_velocity()` | 查询最近模拟速度的三个分量 |
| `comet.move_character(x,z,jump)`／`character_grounded()` | 提交角色移动意图／查询落地状态 |
| `comet.control_character(enabled, options)` | 使用基础 Gameplay 控制器，返回是否提交地面跳跃请求 |
| `comet.session_set(key,value)`／`session_get(key)` | 本局共享 bool、有限数值、string、Vec3；nil 删除 |
| `comet.restart_scene()` | 恢复本局启动内容基线 |
| `comet.emit(name,value)` | 发送场景通知；脚本通过 `events` 声明接收方法 |
| `comet.play_one_shot()` | 使用本实体 Audio Source 播放短音效 |
| `comet.log(text)` | 带源码位置的 Info 日志，仅在回调中使用 |

施力要求动态刚体和碰撞体，持续力在 `fixed_update` 逐步提交，无需乘 dt；物理状态不等于插值后的呈现姿态。
角色与动态刚体分别拥有位置写入权，不同时让脚本变换和物理控制同一位置。

基础角色使用 Character Controller 组件配置速度、尺寸、跳跃、坡度、台阶和地面吸附。
`control_character` 默认启用并读取 `character.move_x`／`character.move_z`／`character.jump`，两参数可省略；
可用 `{move_x = "move.horizontal", move_z = "move.vertical", jump = "jump"}` 覆盖动作名，移动要求 Axis，跳跃要求 Button。
选项中的 `movement_reference` 接受相机等实体引用，使用其无缩放世界朝向计算水平移动，俯仰不减速；省略时按世界 XZ 移动。
`facing` 接受外观实体引用，使本地 -Z 朝向移动方向，停止时保留朝向；不允许目标带刚体或角色控制器。
这两个选项均可通过脚本 `entity` 属性配置，沿用实体引用的失效检查；项目决定参考相机与外观，相机不必属于角色层级。

```lua
function script:fixed_update()
    if comet.control_character() then
        comet.play_one_shot()
    end
end
```

配置引用的项目可在 `on_start` 创建选项表，每个固定步复用：

```lua
self.controls = {movement_reference = self.parameters.camera, facing = self.parameters.visual}
-- fixed_update 内：
local jumped = comet.control_character(true, self.controls)
```

输入组由项目启停。C++ 可调用 `Gameplay::CharacterController`；碰撞／坡度／平台求解仍归 Physics，计分、声音和完成规则归项目。
角色控制在固定步调用；相机控制在普通更新调用。游戏相机默认经 CameraControllerSystem 选择主相机，也可由 C++ 调用 `Gameplay::CameraController::update(scene, camera, input, delta_time, physics)`，显式控制指定实体。
相机配置仍在 CameraControllerComponent 中；输入读取和方向计算与角色共用，编辑器导航沿用自己的拖动、聚焦和缩放规则。
停用控制或失焦提交零移动意图，重力与平台跟随继续运行。

创建实体可提供初始本地变换和网格／材质来源，不等同于 Prefab：

```lua
local marker = comet.create_entity("Marker", {
    translation = {0, 1, 0},
    scale = {0.15, 0.15, 0.15},
    mesh_source = comet.self_entity(),
})
```

事件声明例如 `script.events = {["demo.score_changed"] = "on_score_changed"}`，
再定义 `function script:on_score_changed(score)`。接触回调为 `on_collision_enter/exit(other)`，触发区为 `on_trigger_enter/exit(other)`。
`on_stop` 只执行清理，可以关闭输入组，不能修改已停止的场景。

### Lua 模块与材质

`require("scripts.shared")` 从项目 assets 根解析到 `scripts/shared.module.lua`；模块返回 table，不生成 `.meta` 或挂入 Script 槽位。
同一行为实例重复 require 使用同一模块表，不同实体各自拥有状态；共享玩法状态使用会话值。

```lua
local shared = require("scripts.shared")
local script = {}
function script:on_start()
    self.score = shared.get()
end
return script
```

Project 支持创建、移动、改名和删除模块；操作显示新旧 require 名称，引用由作者更新。
模块路径使用 ASCII 标识符，支持点分层级；在脚本顶层声明依赖，回调可再次 require 已加载模块。
保存共享模块会重载其依赖行为，整组候选成功后共同切换。

材质参数只覆盖当前实体的运行版本，不修改共享资产或 Edit 场景：

```lua
comet.set_material_scalar("roughness", 0.25)
comet.set_material_vector("base_color", 0.2, 1, 0.25, 1)
```

名称、类型和范围须符合材质布局，Stop 清除覆盖。Lua 的预算、权限、实体失效与通知交付边界见[Lua 架构](architecture/overview.md#lua-脚本与参数)。

## 项目游戏 UI

项目通过 `project.json` 声明 UI，路径相对 assets：

```json
"ui": {"document": "ui/runtime.rml", "controller": "ui/runtime.ui.lua"}
```

页面／样式／HUD／主题由项目保存；Engine 提供 RmlUi、共用字体、基础图标及设置控制器。
控制器可以使用内置模块，避免重复实现设置流程：

```lua
return ui.settings {
    display = {sizes = {{1280, 720}, {1920, 1080}}},
    quality = true,
    audio = true,
    model = {fps_text = "0 FPS"},
    on_frame = function(self, api, frame)
        api.set("fps_text", tostring(math.floor(frame.fps + 0.5)) .. " FPS")
    end,
}
```

改键默认启用，其余面板默认关闭；`input=false` 制作仅含显示／音量的设置页。
`elements` 配置控件 ID，`shortcuts` 配置快捷键，`quality.presets` 定义档位，`render_bindings` 可调整绑定行布局。
模型通过 `data-model="ui"` 绑定，控件通过 `command(...)` 发送事件；`api.session_get(key)` 只读本局标量，用于 HUD。

基础图标选自“570+ 图标 v1.0.3”，15 张 PNG 保存到 `engine/resources/ui/icons/`，与字体一起沿用 Git LFS。
构建复制共用资源，Editor 打包到 ImGui 图集，项目页面通过内置路径引用：

```html
<img src="comet://ui/icons/settings.png" />
```

项目自有图片继续使用文档相对路径。内置图片不进入项目资产索引或项目源码依赖。
静态依赖收集包括页面、模板、样式、隐藏图片、图集和项目字体，动态生成的路径需随导出能力另行声明。

视口“视图 → 游戏 UI”控制界面显示；“视图 → 重载 UI”或 Demo F6 重载页面与控制器。
候选失败保留旧页面和设置草稿；运行中的 UI Lua 错误停用界面并显示诊断，修正后显式重载。
文档回调、受控 API 与失败边界见[项目 UI 控制器](architecture/overview.md#项目-ui-控制器)。

## Shader 与渲染内容

项目示例是 `demo/assets/shaders/stripes.vert`、`stripes.frag` 和 `stripes.shader`，材质 `stripes.mat` 按稳定 Handle 引用程序。
打开 `scenes/default.scene`，选择条纹立方体，修改材质频率或 Shader 源码可观察结果。
程序描述提供阶段、入口、默认材质属性和编辑范围；基础属性支持 float、vec4、sampler2D。

Project 右键 Shader 源码或程序描述选择“打开源码”。Editor 监视项目材质程序及其 include；
编译／接口失败时相关物体显示洋红色错误材质并记录诊断，修正后恢复。
内置调试线、阴影、天空盒和显示输出 Shader 修改需重新构建。

| 引擎 Shader 目录 | 内容 |
| --- | --- |
| `material/` | 网格顶点与材质片元入口 |
| `common/` | 公共顶点实现和 Frame 接口 |
| `lighting/`、`shadow/` | 前向光照和方向光阴影 |
| `environment/`、`debug/` | 天空盒和调试线 |
| `post/` | Bloom、曝光、色调映射与显示输出 |

新增引擎程序维护 `engine/shaders/CMakeLists.txt` 的显式阶段配对；项目 Shader 走资产准备链。
接口、反射和发布规则见[材质与 Shader 架构](architecture/overview.md#材质shader-与-pipeline)。

## 资产准备与可选下载

```bash
./build/tools/asset/comet_prepare_project ./demo
./tools/download_assets.sh
```

准备工具增量生成启动场景所需产物，并验证项目 UI 的静态文件引用；`release.sh` 自动调用。
资源缓存位于项目 `.comet/cache/imported/`，可删除重建；构建和启动不自动下载资源。

下载脚本获取 Small Hangar 01 的 1K／2K／4K／8K HDR，并校验 SHA-256；依赖 curl 和 sha256sum／shasum。
`scenes/default.scene` 使用 4K，角色场景无需下载。源图来自 [Poly Haven](https://polyhaven.com/a/small_hangar_01)，
作者 Sergej Majboroda，使用 [CC0](https://polyhaven.com/license)。大环境图不入库，相邻 `.meta` 保留；必需图片、字体和音频使用 Git LFS。

真实 HDR 回归需显式配置 `-DCOMET_TEST_DOWNLOADED_ASSETS=ON`，再运行 `ctest --preset dev-debug -L assets`；
普通测试使用本地小数据。

## 诊断与性能测量

“渲染统计”分别显示主线程、渲染线程、场景录制和已完成帧的 GPU 耗时；采集开关和暂停显示分别控制采样与展示。
“保存显存分配报告”写入项目 `.comet/editor/diagnostics/gpu-allocations.json`，日志写入 `.comet/logs/`。
各分段包含关系及线程计时范围见[渲染诊断](architecture/overview.md#渲染诊断)。

渲染基准创建固定场景、预热、采样并退出：

```bash
./tools/render_benchmark/run.sh
./tools/render_benchmark/run.sh --help
./tools/render_benchmark/run.sh /tmp/comet-moving.csv 2048 640 360 240 0 64 moving
```

默认报告位于 `build-release/reports/render-benchmark.csv`；支持静态／移动、脚本、光源、裁剪、项目 Shader 及活动／休眠刚体场景。
报告记录画质与负载、整帧和分段耗时、P50/P95/P99、draw／上传／分配量。
正式比较使用同设备 Release、相同负载，多次交替运行；避免并行构建和其他 GPU 任务。
CPU 墙钟包含等待，GPU 不包含呈现完成，分段百分位不能相加；旧计时口径应单独比较。

资产扫描基准在临时副本中测量：

```bash
./tools/asset_scan_benchmark/run.sh /Projects/MyGame 30
./tools/asset_scan_benchmark/run.sh --synthetic 1000 30
./tools/asset_scan_benchmark/run.sh --profile --synthetic 1000 30
```

标准输出为 CSV，复制时间不计入采样；`--profile` 使用 RelWithDebInfo 并记录内部采样，不能直接与 Release 比较。

## 测试与贡献

```bash
ctest --preset dev-debug
ctest --preset dev-debug -L '^(cpu|ui)$'
ctest --preset dev-debug -L gpu
```

CPU 测试包含 `tests/editor/core/`；内存中的 ImGui 测试位于 `tests/editor/ui/`；真实窗口／GPU 集成位于 `tests/editor/integration/` 等目录。
macOS 的 CTest 仅在测试进程内关闭窗口动画。新测试在 `tests/CMakeLists.txt` 明确归组，新增源码维护所属模块清单。
C++ 遵循 `.clang-format`；第三方修改限依赖更新。`module_boundaries` 和 `module_dependency_contract` 检查依赖及禁止方向。
