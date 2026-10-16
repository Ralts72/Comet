# Comet 使用手册

## 骨骼角色与动画

角色场景使用引擎内置的 Quaternius 人形模型和 `idle`、`walk`、`run`、`crouch`、`crouch_walk`、`jump`、`fall` 七个动作。
资源位于 `engine/resources/characters/default.glb`，由 Git LFS 保存，启动不需要联网。
Shift／LeftShoulder 跑步，Ctrl／RightShoulder 蹲伏；右键拖动／右摇杆转动相机，滚轮或 PageUp／PageDown／D-pad 上下缩放。
相机遇到墙壁会缩短距离，离开障碍后恢复。

在层级中选中“角色模型”：

- “网格渲染 → 网格”可选择引擎默认角色、已有单网格资产或模型内的子网格；完整模型从 Project 拖入视口。
- “网格渲染 → 材质”可选择引擎默认 PBR／无光照材质；默认角色使用中性的 PBR。修改配色、贴图或参数时，在项目中创建自己的材质并替换该引用。
- “动画 → 动画控制器”选择 `.ani` 资产，参数区显示默认值并允许实例覆盖；清空控制器后直接选择片段、循环与过渡时间。
- “角色动画驱动 → 驱动角色”引用物理角色，将实际水平速度、着地、上升和蹲伏状态写入 `speed`、`grounded`、`rising`、`crouched` 四个参数。非角色对象无需该组件。
- Edit 中组件配置可保存、撤销；Play 的参数、时间和姿态属于运行副本，暂停停止推进，单步推进一次。

Project 右键“新建动画控制器”创建默认角色控制器的项目副本。双击 `.ani` 或右键“编辑动画控制器”打开可停靠的状态图。

- 左侧管理参数及试播值；点击参数名称，在 Inspector 编辑类型和默认值。试播值不写入资产。
- 点击状态，在 Inspector 选择单动画、一维或二维混合；选好预览角色后，动画片段来自该模型或所选动画库。
- 选择来源状态并点击“连接状态”，再点击目标建立切换；点击箭头编辑条件、混合时间和优先级。
- 拖动节点调整布局，中键平移，滚轮缩放；“整理布局”恢复自动排列。布局只属于本次编辑会话。
- Edit 中选择预览角色和状态，播放或拖动预览时间；混合空间显示实际片段权重，姿态直接显示在现有视口。
- Play 中显示所选角色的参数、当前状态及过渡进度，状态图高亮运行状态。控制器草稿不会直接改写实例参数。

修改可撤销／重做，点击“保存”写入资产，“放弃修改”恢复上次保存。
切换选择时可保存／放弃／继续编辑；关闭、切换项目或运行前会保存合法草稿，校验不通过则保留编辑。
过渡按列表顺序匹配全部条件；来源“任意状态”不重入当前状态，每帧最多切换一次。“播放完成一次后”对循环片段也生效，不受采样时间回绕影响。
同一控制器可由多个对象共享，播放状态彼此独立。参数类型、片段名和状态引用错误会明确诊断。

Demo 使用 `animations/character.ani`，引擎也保留一份默认角色控制器。地面状态的 1D 阈值为 0／4／6 m/s（待机／行走／跑步），蹲伏为 0／2 m/s；它们标定动作步幅，不改变物理移动速度。
例如实际速度 2 m/s 混合待机与行走，5 m/s 混合行走与跑步；撞墙减速、摇杆半推使用同一实际速度。
混合片段按归一化周期同步，跨阈值不重置步态；周期长度按片段时长和权重插值。状态的“参数平滑”与过渡时间独立配置；播放速度为 0 时相位停止而过渡仍可结束。

Lua 可驱动门、机关或自定义角色的参数：

```lua
comet.set_animation_parameter(comet.self_entity(), "open", true)
```

参数必须先在该实体引用的控制器中声明，值为匹配类型的布尔或数值；对象引用沿用脚本的活动场景校验。
`comet.get_animation_parameter(entity, name)` 读取实例覆盖或控制器默认值。

### 2D 混合、动画库与分层

状态的“播放方式 → 二维混合（2D）”选择 X/Y 数值参数，每个片段配置 X/Y 采样位置。
蓝点表示动作，黄圈表示试播参数；位置不能重复，X/Y 应使用相近量纲。所有动作共用周期相位，权重归一化；
只有一个点或共线点也可使用，不要求完整网格。边界外输入仍得到有效权重，但不代表真实方向动作；默认角色不含侧移／倒退片段。

Inspector 展开“动画库与分层”：

- 动画库下拉选择项目 `.anilib`，清空时使用模型自带动作。片段列表切换到库中别名。
- 添加层，选择一个已有状态作为动作源，设置固定权重和可选数值参数；后者钳制到 0..1，再乘固定权重。
- 添加遮罩节点可从预览模型选取，包含该节点的全部后代。空遮罩影响全身；列表靠后的层覆盖前面的结果，可上移调整顺序。
- 层独立播放，权重为零仍走时钟。层不触发状态事件；基础状态负责脚步等事件。普通暂停／单步与基础状态一致。
- 结束预览不会修改场景或控制器。层定义、X/Y 和库引用随 `.ani` 保存，沿用撤销／重做。

动画库使用小型 JSON 目录，存入项目 assets 下，例如：

```json
{"clips":[{"name":"walk","mesh":"13857136248767905793","clip":"walk"}]}
```

`name` 是控制器使用的别名，`mesh` 是已有骨骼网格资产或 glTF 子网格的 GUID 字符串，`clip` 是来源片段名。
同一库可组合多个兼容来源；导出自动收集依赖，动画库从 CPU 网格产物读取动作，不为这些源网格创建 GPU 资源。
库文件可通过 Project“打开源码”修改或从外部导入；引用来源必须在项目中或为引擎默认资源。
当前要求节点名称唯一、层级和局部绑定姿态兼容，节点顺序可以不同；这不是跨骨架重定向。
来源仍使用已支持的带骨架网格 glTF／GLB；纯动画文件、FBX、加法层和每层独立状态机尚未接入。
Demo 起点旁“动画分层示例”引用 `animations/layered.ani` 和 `shared.anilib`；`pace` 控制行走混合，`crouch` 控制蹲伏混合，`upper` 控制 `spine_01` 子树的跑步覆盖层。普通角色仍沿用原有控制器。

### 动画事件与骨骼挂点

选中状态后，Inspector 的“动画事件”显示一个周期的时间轴。双击添加标记，点击选择、拖动调整时机；
也可使用“添加事件”和“触发比例”。事件保存名称与文本／数值／开关参数，沿用控制器的保存、撤销与重做。

事件按推进前后的时间区间依次触发，跨帧或跨多圈也会交付；0% 在开始推进及每次循环时触发，100% 在周期结束时触发。
1D／2D 混合共用状态时间轴，标记只交付一次。切换后的逻辑状态产生事件，淡出的旧状态不再产生事件。
零速、普通暂停和拖动 Edit 预览时间不触发游戏事件；Play 单步触发跨过的标记。Stop／重开清除待交付事件与播放进度。

事件只送给播放动画的模型实体，不广播到其他角色；在该实体上挂脚本：

```lua
local script = {events = {footstep = "on_footstep"}}
function script:on_footstep(foot)
    if comet.get_animation_parameter(comet.self_entity(), "speed") > 0.2 then
        comet.play_one_shot() -- 使用该实体的音频源
    end
end
return script
```

将物体在层级中拖到蒙皮模型实体下，添加“骨骼挂点”组件，从下拉框选择骨骼；本地 Transform 即骨骼空间的位置、旋转与缩放偏移。
挂点沿用场景父子关系，支持保存、Prefab 和撤销。Edit／Stop 使用绑定姿态，动画预览和 Play 使用当前混合姿态；
主绘制、阴影和挂点共用采样结果，挂点也继承父角色的固定步呈现插值。
无骨骼父模型或已丢失的骨骼会在 Inspector 提示，此时使用普通父子变换，配置保留待修正。

Demo 的角色模型挂 `character_feedback.lua`，移动时按左右脚标记播放项目内的合成脚步声；`hand_r` 挂一个手持物。
声音、动作标记和手持物属于项目内容，Engine 不包含武器、伤害或脚步声音规则。
2D 混合、独立动画库、分层与 root motion 继续按路线图推进。

首版 Mesh 资产支持一个 mesh／skin、每顶点最多四个骨骼权重，以及 TRS 的 Linear／Step／CubicSpline 动画。
同一 Mesh 的多个实体独立播放，主绘制和阴影共用 GPU 蒙皮结果。模型内多个 primitive 使用所指定的同一个材质。
替换角色时，应同时选择新模型自身的动作；跨骨架重定向、独立动画库、root motion、IK 和 morph target 尚未接通。
动作名称找不到时，模型显示绑定姿态，Console 报出实体和缺失动作；重新选择有效片段后恢复播放。
若新片段有效、只有旧过渡片段在替换模型后丢失，则直接播放新片段。其他对象继续绘制，保存的动作映射保持原值。

在 Edit 模式选中带 Animator 的模型，展开 Inspector 的“动画”，顶部“动画预览”可选择片段、播放／暂停、调整循环与预览速度。
拖动时间会暂停并显示对应姿态，可以定位到片段最后一帧；“结束预览”恢复绑定姿态。
预览只改变所选模型的瞬时播放快照，不启动脚本／物理、不修改播放配置、不进入保存或 Undo。
切换选择、关闭／折叠 Inspector、动画数据重新导入、换场景和进入 Play 会结束预览；Play 使用正常的游戏动画控制。

首次构建和 Demo 入口见 [README](../README.md)。本文说明具体操作与接口；模块实现见[架构文档](architecture/overview.md)，后续能力见[路线图](engine-roadmap.md)。

### 根运动

打开 `scenes/root_motion.scene` 并运行，角色会由行走动作带动前进，在墙前被胶囊碰撞阻挡。它复用引擎内置角色；`animations/root_walk.glb` 只提供兼容骨架、演示动作与导入所需的最小网格，动画库加载时不上传这份源网格。示例在原地行走上添加了每秒 2 米的根轨迹，用来验证运动链路；实际制作可换成 DCC 导出的根运动动作。

在带 Animator 和蒙皮网格的模型实体上添加“根运动”。根骨骼可留空自动选择全部关节的共同祖先；移动目标留空表示自身，也可以选择模型的祖先角色实体。水平位移、垂直位移与根旋转分别开启。角色保持直立，建议使用正等比缩放；父级产生剪切时拒绝应用，动态／静态刚体不能作为运动目标，运动学刚体沿现有目标变换同步。

根运动实例在固定步推进，跨循环保留完整位移与转向，混合与过渡组合增量而非骨骼绝对位置。基础动画提供运动；覆盖层不额外驱动物体。选中的运动从蒙皮与骨骼挂点姿态中移除，避免模型再走一次。Edit 动画预览保持原地，运行后才推动目标。

角色目标通过 PhysicsAccess 提交本步速度，保留碰撞、台阶和平台跟随，不直接覆盖物理位置。水平运动保留重力／跳跃；开启垂直运动时，动画控制本步垂直速度并关闭自动台阶／吸地修正。阻挡的距离不会留到以后补走。启用根旋转时，通用角色控制器不再改写模型朝向；需要由输入转向时关闭根旋转。`CharacterAnimationComponent` 为根运动使用当前移动意图驱动速度参数，普通动画继续读取实际移动速度。

配置沿组件保存、克隆、Prefab 引用重映射和撤销处理，动作仍使用原有模型／动画库及导出链路。暂停不推进，单步只执行一个固定步；Stop 清除临时播放和物理请求。当前根运动动画姿态按固定步更新，目标变换复用已有呈现插值；姿态级插值、动作扭曲和脚部 IK 后续扩展。

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

Edit 中右键或 Option/Alt+左键环绕，中键或 Option/Alt+Shift+左键平移，滚轮缩放；左键选择，F 聚焦选中组及其子树中的网格。
编辑相机默认从 `(10, 8, 12)` 斜上方看向世界原点；项目的游戏相机位置由场景决定。
工具菜单选择移动／旋转／缩放、世界／局部空间和吸附。缩放使用局部空间；Esc、失焦或隐藏视口取消拖动。
多选手柄以最后选中的实体为支点，统一移动、旋转或按比例缩放；选中的父子不会被重复变换，一次手势只记一条撤销。
非均匀缩放与不同朝向的组合可能产生 TRS 无法表示的斜切，此时整组恢复拖动前状态并显示提示；可使用等比缩放或分别编辑。
“视图 → 碰撞体”显示盒、球、普通／角色胶囊、凸包／静态网格线框和关节反馈；关节字段旁“对齐”按钮调整锚点／方向，Edit 支持撤销。

Edit 属性修改及结构操作支持 Undo／Redo，一次拖动记一条历史；Play 修改仅用于运行调试。
默认快捷键为 Ctrl+Z／Ctrl+Y，macOS 为 Cmd+Z／Cmd+Shift+Z；可在“编辑 → 快捷键设置”修改。

### 实体、资产与材质

Hierarchy 用 Ctrl/Cmd 点击增减选择，Shift 点击连续选择可见行；最后选择的实体作为 Inspector 多值显示的主目标和整组变换手柄的支点。
右键选中项可复制／删除整组选中子树；右键未选中项只处理该项。父子同时选中不会重复处理。
Ctrl/Cmd+C、Ctrl/Cmd+V 使用进程内多子树剪贴板；粘贴生成新 UUID，组内关节／脚本引用一起重映射，组外引用保持。
Ctrl+Backspace／Cmd+Backspace 删除选中组。复制、粘贴、删除整组都只产生一条撤销记录。
右键“批量放置…”输入新增份数与每份 XYZ 世界间距，按选中组为单位生成阵列；每份的内部引用和 Prefab 来源独立，父级不变。
例如份数 3、间距 `(2, 0, 0)` 会在原组的世界 X 方向 2、4、6 处新增三组；取消不改场景，确认后一次撤销可移除整个阵列。
右键仍可创建／重命名单个实体；拖动修改被拖实体的父级并保留本地 Transform。
Project 中纹理、模型、网格与材质条目带缩略图，悬停图片可看放大预览；模型展开后的子贴图也支持透明棋盘背景。
模型按默认场景显示节点与材质，单个网格使用默认材质，材质在球体上预览。三维预览使用固定灯光与自动取景，不改变当前关卡。
预览按导入设置生成，保存或重新导入后更新；损坏资源显示原因，不保留旧图冒充新结果。缩略图仅为编辑器缓存，不打包进游戏。
Inspector 提供组件、参数和按类型过滤的资源引用，Edit 支持从 Project 拖入引用。
多选时只显示共同组件与共同适用的字段，差异标为“多值”；修改数值统一应用，向量仅同步修改的轴，其他轴保持各自值。
资源替换与同脚本参数编辑共用整组撤销；参数只同步当前修改项，“使用脚本默认值”清除该项在所有选中对象中的覆盖。
碰撞层／掩码修改统一替换整份掩码；关节“对齐”按每个对象各自坐标计算。添加／移除组件和动画预览仍在单选时操作。
Edit 拖动过程中 Esc 恢复整组；切换选择结束原有修改。Play 批量修改仅影响运行副本，不写 Edit 历史。

层级右键“保存为 Prefab…”把当前实体及后代保存到项目 `assets/` 下的 `.prefab` 文件；文件已存在时不会覆盖。
Prefab 不能引用子树外的实体。将它从 Project 拖入视口可创建独立实例，内部关节／脚本实体引用自动映射；一次放置可撤销。
实例根右键“从 Prefab 更新（保留覆盖）”读取当前源文件，保留实体 UUID、放置变换和本地属性／脚本参数修改；成功更新可撤销／重做。
源删除若影响本地修改、额外子节点或关卡引用，会报错并保持原场景。“解除 Prefab 关联”保留实体内容，且可以撤销。
关联及源内容基线随场景保存，Play 使用已展开的实体；另存为 Prefab 会展开已有实例，不创建嵌套依赖。
制作 Prefab 使用现有场景视口与 Inspector：拖入实例、编辑子树，再在实例根右键“写回 Prefab 源”。
写回保持源节点身份和 `.meta` 句柄，新增节点获得源身份；场景根的放置变换不写回。写回前先合入源的新内容，再保留本实例修改。
源文件与当前实例关联记为一条撤销；Undo／Redo 同时恢复两者。其他实例通过“从 Prefab 更新”接纳变化，不自动覆盖。
“恢复 Prefab 源内容”清除本地属性、结构覆盖，恢复被本地删除的节点，保留放置变换；也支持撤销。
若被清除的本地实体仍被关卡引用，先解除引用再恢复。Play 禁用这些制作操作。

Project 右键可新建 Material、Script 或 Lua Module；搜索按文件名／目录名筛选，Refresh 重扫，Reimport 重建产物。
移动／改名资产时保留 `.meta` 身份，场景移动同步更新保存路径、启动场景和会话；删除经确认后送入系统回收站。
当前场景、启动场景或被其他资产引用的文件需要先解除对应使用；恢复资源时同时恢复源文件和 `.meta`。
资产删除独立于场景 Undo。

系统文件管理器可拖入 PNG/JPEG、HDR、glTF/GLB。glTF 连同相对依赖复制到项目，获得新身份。
暂不导入整个目录或 Lua 多文件依赖包。新 glTF／GLB 按完整模型在后台导入，Project 展开模型可查看其网格、材质和纹理。
模型拖入视口后按默认 glTF 场景生成实体子树，保留节点局部变换、多 primitive 与材质关联；一次撤销删除整棵放置子树，保存重开仍保持引用。
已有 Mesh 资产继续使用单网格路径，放置后可在 Inspector 指定材质。模型子资源由源文件管理，不能独立删除、移动或编辑；模型右键“重新导入”更新产品，实例可替换自己的材质引用。
重新导入完成后，层级中的模型实例根右键“从模型更新（保留覆盖）”，接纳源节点的增删、父子关系和属性变化；实例 UUID、放置变换和本地覆盖保持，更新可以撤销。
“恢复模型源内容”清除本地覆盖，“解除模型关联”保留当前实体内容；模型不提供写回 glTF，可另存为独立 Prefab。关联随场景保存，旧版已放置的普通实体不会自动获得来源关系。
Demo 的 `models/milk_truck.glb` 是下载的四材质 glTF 样例，含内嵌贴图；`character.scene` 与 `character_second.scene` 都保存其模型实例，位置和朝向各自覆盖，几何／材质共用。层级中的“牛奶货车”可用于尝试源更新、恢复与解除；模型按静态布景使用，不播放原模型的车轮节点动画。GLB 走 Git LFS。
唯一名称的节点在重排和换父级后保持来源身份；节点改名视为删除后新增，无名／重名节点按源下标识别，primitive 按节点内序号识别，重排这些内容可能改变对应关系。删除与本地修改或外部引用冲突时拒绝更新。
骨骼子网格的动画仍通过动画组件配置。

选中纹理资产可在 Inspector 修改色彩空间、法线用途、翻转 Y 和“生成 Mipmap”，修改后立即重新导入并保存到 `.meta`。
导入结果缓存在项目 `.comet/cache/imported/texture` 下，源文件或导入设置改变后重建；缓存可删除。
普通二维纹理默认生成完整 mip 链以减轻远处闪烁；颜色／自发光纹理选择 sRGB，金属粗糙度与 AO 选择线性。
勾选“法线贴图”后固定使用线性 RGB 法线编码，mip 按方向平均并归一化；取消后恢复原色彩空间选择。像素风或需要原始采样的纹理可关闭 Mipmap。

基础材质模板为 `pbr` 和 `unlit_color`。PBR 支持基础色、法线、金属粗糙度、AO 与自发光贴图，支持直接光、方向光阴影与全局 IBL。
金属粗糙度贴图采用 glTF 通道约定（G 粗糙度、B 金属度），乘以对应材质参数；AO 使用 R 通道，仅作用于间接光。
法线使用切线空间 RGB／+Y 约定；glTF 自带的 `TANGENT` 会保留到缓存与 GPU 蒙皮，实例化及镜像缩放同样适用。有 UV 但未提供切线的 glTF 使用 MikkTSpace 生成，接缝会拆分顶点并保留蒙皮权重；缺少法线时生成逐面法线。无 UV 的网格保留几何法线，程序网格未指定切线时仍沿用 UV 导数重建。强度为 0 时恢复几何法线；自发光为贴图 × 颜色 × 强度，无需光源，亮部参与 Bloom。
Inspector 的“表面模式”提供不透明、透明裁切和透明混合；“双面绘制”控制背面是否可见，PBR 背面法线随之翻转。
不透明忽略基础色 Alpha；裁切使用基础色颜色 Alpha × 贴图 Alpha，低于阈值的像素同时从画面和方向光阴影中移除。
透明混合保留深度测试、关闭深度写入，在不透明物体之后按相机深度从远到近绘制，首版不投射方向光实心阴影。
透明排序以物体包围盒中心为依据；互相穿插的透明网格或同一网格内的透明三角形仍可能排序不准确，当前不做折射或 OIT。
项目 Shader 需要沿用 `common/surface.glsl` 的 `surface_alpha` 才支持引擎裁切模式；标准阴影裁切沿用 PBR 的 `base_color`／`base_color_texture` 或无光照 `color` 约定。
材质参数拖动实时预览，松手保存；Esc 取消。材质资产使用独立 Undo 历史，模板切换保留兼容参数并提示不兼容项。

选中 Hierarchy 的“场景”，可编辑 Environment、距离雾、曝光、Bloom 与环境遮蔽。背景和环境照明分别控制，隐藏天空盒仍可保留照明。
这些参数保存到 `.scene`；Play 中场景面板只读，运行时代码仍可修改运行副本。

### 示例场景

通过“文件 → 切换场景”打开 `scenes/pbr_surfaces.scene`，后排从左到右展示法线凹凸、金属粗糙度分区和自发光网格；前排展示双面树叶裁切／阴影，以及红蓝透明层叠。
这些贴图由数学图案生成，随仓库保存，不需要额外下载；可直接选择对应材质／纹理检查参数和重载。

`scenes/character.scene` 提供角色移动、跳跃、台阶、坡度、移动平台及三目标收集；WASD／左摇杆移动，空格／South 跳跃，R 重开。
完成后 HUD 显示“进入第二关”，进入 `scenes/character_second.scene` 的隔墙收集关卡；第二关可随时返回第一关，两关收集记录分别保留。
收集后自动保存，HUD 也提供“保存进度”和“继续存档”；恢复从保存关卡起点出发，已收集目标不会重生。R 重开恢复本次入关的进度基线。首次尚无存档时先保存；失败原因查看日志。
App 和 Play 的存档隔离；Play Stop 后恢复原编辑场景，跨关和收集不会写回场景资源。
Gameplay 负责动作读取与跳跃请求，Physics 负责胶囊求解；Demo Lua 保存计分、完成、声音和重开规则。
角色以脚底为原点，使用无父级的竖直胶囊和正等比缩放；外观与跟随相机作为子实体。
移动以相机的水平朝向为参照，前方标记随外观转向，相机不会跟着外观旋转。

`scenes/default.scene` 保留刚体、关节、材质和脚本演示：左右方向键移动，Space／South 切换旋转，J／K 施加冲量／力，
Tab／North 开关调色，R 重开。开启调色时相关动作由输入组消费，公共动作仍可使用。

### P1 操作验收

自动测试覆盖确定性的内容、运行逻辑和 GPU 结果；下列实际操作由使用者确认。编辑实验建议先“另存为场景”，避免改动 Demo 原件。

| 操作 | 应看到的结果 |
| --- | --- |
| 打开 `pbr_surfaces.scene`，Edit／Play 查看；修改材质和纹理导入设置，保存重开 | 法线、金属粗糙度、自发光、树叶裁切阴影和透明层叠正确，保存后仍一致；裁切同时影响主画面与阴影，混合材质不投实心阴影 |
| 将货车模型与 Prefab 拖入临时场景，修改实例位置／材质；更新或恢复来源，再撤销 | 源更新保留合法本地覆盖；恢复清除覆盖但保持放置；撤销能恢复原状态；保存重开保留关联 |
| 选中多个实体，移动／旋转／等比缩放、修改共同字段、批量放置，再撤销 | 父子不重复变换，向量未修改轴保持各自值，一次操作一条撤销；缩略图可预览且修改后更新 |
| 在 App 与 Editor Play 分别完成第一关、进入第二关、返回、保存；退出再启动并“继续存档” | 两关收集记录保持，继续回到保存关卡起点；R 恢复本次入关基线；App 与 Play 存档互不影响 |
| Play 中暂停／单步、切关，再 Stop 并再次 Play | 暂停与单步正确，Stop 恢复原编辑场景，运行时修改不会写回场景 |
| “项目 → 导出 App”包含第二关，搬到仓库外启动并试玩 | 能显示角色、货车和游戏 UI，切关／继续可用；程序目录不产生日志、缓存或存档 |

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
最终音频增益为音源音量 × 分类音量 × 主音量；空间声源另乘距离衰减。

### 声源与音乐

在 Inspector 添加“音频源”，选择 WAV、音量、音效／音乐分类及循环。开启“空间音效”后，
声源使用实体的世界位置；开始衰减距离、最大距离以世界米数计，默认采用线性距离衰减。
场景可在相机或角色上添加一个启用的“音频监听者”，它的世界位置与朝向决定远近和左右。
持续声源随实体更新；一次性音效保留触发位置，实体随后移动或销毁不会改写这次声音。

在 Project 选中 WAV，Inspector 勾选“流式播放”用于音乐或长音频；设置保存到资源 `.meta`，
导出时进入产品索引。短音效默认预解码，流式资源只保存格式和来源，播放实例按页读取与解码。
背景音乐使用普通音源并选择音乐分类，通常关闭空间音效、开启启动播放和循环。
App／Editor Play 共用同一条音频路径；暂停保持播放位置，单步静默推进，Stop／切关释放声音和文件。

Demo 双关卡包含流式背景音乐、空间脚步声和“空间音效示例”固定声源；移动或转动相机可试听方向与衰减。

### 粒子反馈

在实体 Inspector 添加“粒子发射器”，配置每秒发射数、启动爆发数量、寿命、初速度、
锥体半角、世界加速度，以及出生／结束颜色和尺寸。粒子从实体世界位置沿本地 +Y 发射，
方向跟随层级旋转，尺寸以世界米数计；已出生粒子在世界空间运动，不跟随父实体移动。
关闭发射后已有粒子继续消退；每秒发射数设为 0 可只通过脚本触发。

`comet.emit_particles(24)` 使用本实体配置爆发，第二个参数可指定其他实体。
`comet.clear_particles()` 清空本实体的存活粒子，可选传入其他实体；连续发射保持开启时会继续产生新粒子。
爆发保存触发时的位置，源实体随后移动或销毁不会移走特效。

“粒子纹理”可选择或拖入普通纹理，RGB 与粒子颜色相乘，Alpha 控制贡献；不指定时使用柔边圆形。
纹理沿正常资产导入、热重载与导出链路使用；指定的纹理缺失时记录错误并跳过该批粒子。
“混合方式”默认加法，适合火花与发光反馈；Alpha 适合烟雾和半透明色层，用当前颜色覆盖一部分背景。
Billboard 面向相机，深度测试且不写深度，参与现有 HDR／Bloom。纯加法且没有透明网格时按纹理合批；
有 Alpha 粒子或透明网格时，所有粒子按当前相机深度从远到近排列，与透明网格交替绘制，只合并排序后连续的同状态实例。
排序跨发射器，也包含加法粒子，因此烟雾能衰减后方火花；相机切换与暂停观察都会重新确定绘制顺序。
透明网格按包围盒中心、粒子按中心深度联合排序；同深度先画网格。倾斜／相交网格和跨过大面积透明面的粒子仍可能出现局部错误，尚无 OIT 或 soft particles 表面柔化。
单发射器最多 4096 个、单场景最多 65536 个粒子，满额请求直接丢弃，不延迟补发。
模拟走固定步、暂停／单步与 Stop／切关清理；场景和 Prefab 只保存配置，不保存存活粒子。

Edit 中选中单个发射器，在 Inspector 的“粒子预览”点击播放、暂停、爆发、单步或结束，无需进入 Play。
爆发使用配置数量；数量为 0 时预览发射最多 32 个。未播放时爆发停留在出生瞬间，可继续播放或单步观察。
预览只模拟该发射器，不运行游戏脚本，也不修改场景内容或撤销历史；选择其他实体、折叠组件或进入 Play 会清理预览。
Demo 双关卡使用 `textures/particles/spark.png` 展示收集火花，`default.scene` 保留无纹理圆形反馈。
第一关起点旁的“烟雾示例（Alpha）”与“烟雾排序示例：透明面板”可在 App／Play 转动相机观察，也可选中发射器在 Edit 预览。
OIT、碰撞和 GPU 模拟继续按路线图扩展。

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
`self.parameters` 为只读配置；三／四分量数组表示 Vec3／Vec4，颜色、实体引用与资产引用显式声明：

```lua
script.properties = {
    tint = {type = "color", default = {0.2, 1, 0.25, 1}},
    target = {type = "entity"},
    unit = {type = "prefab"},
    stats = {type = "data"},
}
```

颜色按线性 RGBA 使用，不根据变量名猜测类型。实体字段支持下拉选择和 Hierarchy 拖入。Prefab／Data 字段在 Inspector 下拉选择对应类型的项目资产，默认未分配；内部使用精确资产身份，Lua 无需填写 ID。
参数右键“使用脚本默认值”删除单项覆盖；“恢复默认参数”或“移除不兼容参数覆盖”支持场景撤销。

Project 右键“打开源码”使用外部编辑器；macOS 优先 VS Code。Lua 支持 UTF-8、可选文件头 BOM、LF／CRLF。
Editor 保存源码后重载关联行为：暂停时等待继续或单步；成功重建实例并重新执行 `on_start`，会话值保留，任意 `self` 状态不迁移。
语法／声明错误保留旧版本并报告；新实例的启动执行错误会停止 Runtime，Editor 恢复 Edit。独立 App 不自动监视源码。

### 游戏数值资产

平面数值使用 UTF-8 CSV，例如 `assets/data/characters.csv`：

```csv
id,name,hp,speed
hero,主角,100,4.5
slime,史莱姆,20,1.5
```

同目录的 `characters.csv.schema.json` 声明字段类型与范围，`id` 是必需且唯一的字符串，不重复声明：

```json
{
  "fields": {
    "name": {"type": "string"},
    "hp": {"type": "integer", "min": 1, "max": 10000},
    "speed": {"type": "number", "min": 0}
  }
}
```

复杂数据使用 `*.data.json`，同一文件保存 `fields` 与 `rows`：

```json
{
  "fields": {"effects": {"type": "json"}},
  "rows": [{"id": "slash", "effects": [{"kind": "damage", "amount": 10}]}]
}
```

字段支持 `string`、`bool`、`integer`、`number`、`json` 和 `asset`。`json` 接受嵌套对象／数组，里面的技能规则由项目解释；
`asset` 额外声明 `asset_type`（如 `data`、`prefab`、`audio`），值为十进制资产身份字符串，引用随依赖闭包加载和导出。
仅声明为 `asset` 的顶层字段参与资源依赖提取，嵌套 JSON 内的普通字符串不会自动成为资产引用。
所有声明字段必填，未知字段、重复 ID／键、错误类型、越界和缺失／错类型资产引用会报错；无效数据不会当作默认值继续导出。
CSV 支持 BOM、CRLF、引号内逗号／换行与双引号转义；布尔写 `true`／`false`，数字使用小数点。
数值整数限于 ±(2⁵³−1)，完整 64 位资产身份使用字符串。嵌套值不支持 `null`，可省略嵌套对象中的可选键。
单文件最多 16 MiB、10000 条记录、63 个声明字段、100000 个数据节点和 32 层嵌套。

把文件放入项目 `assets/` 并刷新资源，或拖入 CSV／JSON 文件导入；CSV 的字段说明会随源文件复制、移动和删除。
只有数据源生成 `.meta`，字段说明没有独立资产身份。选中数据源可在 Inspector 查看表格；资源右键“打开源码”修改内容，CSV 字段说明也能打开。
首版提供只读预览，CSV 可用表格工具编辑，JSON 用代码编辑器。修改后刷新资源并重新进入 Play，验证新值；不自动重置游戏中的当前属性。

```lua
local script = {properties = {stats = {type = "data"}}}

function script:on_start()
    -- 在 Inspector 中为 stats 选择数据资产。
    self.base = assert(comet.data_get(self.parameters.stats, "hero"), "缺少 hero 数值")
    self.hp = self.base.hp
    -- comet.data_ids(self.parameters.stats) 返回按 ID 排序的记录列表。
end

return script
```

查询只访问已经加载的资产，不在 Lua 回调中读文件；缺失记录返回 `nil`，未加载／错类型的资产报错。
返回值是独立 Lua 表，修改它不会影响共享基础数据或其他实体；建议在初始化时读取，当前生命值、装备和 Buff 由项目维护。
导出将 CSV 与 JSON 统一写为规范化 JSON 产品，保留身份与依赖，不携带 CSV 或字段说明文件，也不依赖开发机缓存。
Demo 的 `collection.csv` 控制收集粒子数，并引用 `feedback.data.json` 的信标位移；两个关卡共用这些基础数值。

### Lua 实体与会话

| 接口 | 作用 |
| --- | --- |
| `comet.self_entity()`／`comet.find_entity(uuid)` | 获取本实体或 UUID 引用；引用提供 `:is_valid()`、`:position()`、`:translate()`、`:rotate()` |
| `comet.position()`／`translate(x,y,z)`／`rotate(x,y,z)` | 查询／修改本实体变换；角度为度 |
| `comet.instantiate_prefab(asset, position)` | 从已分配的 Prefab 参数生成完整子树，返回预留的根实体 UUID；position 是可选 Vec3 |
| `comet.create_entity(name, options)`／`destroy_entity(entity)` | 阶段末创建实体或删除子树，只修改运行场景 |
| `comet.has_rigid_body(entity)`／`remove_rigid_body(entity)` | 查询／阶段末移除刚体，保留实体及其他组件 |
| `comet.apply_impulse(x,y,z)` | 本实体质心处的世界空间冲量 |
| `comet.apply_force(x,y,z)`／`apply_torque(x,y,z)` | 下一固定步的世界空间力／力矩 |
| `comet.apply_impulse_at(ix,iy,iz,px,py,pz)`／`apply_force_at(...)` | 在指定世界位置施加冲量／力 |
| `comet.linear_velocity()`／`angular_velocity()` | 查询最近模拟速度的三个分量 |
| `comet.raycast(origin, displacement, options)` | 查询最近射线命中；返回实体、世界位置／法线和线段比例，无命中为 nil |
| `comet.overlap_sphere(center, radius, options)` | 返回球形范围内去重后的实体引用数组，无命中为空数组 |
| `comet.shape_cast(shape, origin, displacement, options)` | 平移盒／球／胶囊，返回最近命中的实体、目标表面接触点、法线与移动比例 |
| `comet.move_character(x,z,jump)`／`character_grounded()` | 提交角色移动意图／查询落地状态 |
| `comet.control_character(enabled, options)` | 使用基础 Gameplay 控制器，返回是否提交地面跳跃请求 |
| `comet.session_set(key,value)`／`session_get(key)` | 本次游戏共享 bool、有限数值、string、Vec3；切关保留，nil 删除 |
| `comet.restart_scene()` | 恢复当前关卡的初始内容和入关时的会话值 |
| `comet.load_scene("scenes/next.scene")` | 请求加载同项目关卡，切换时携带当前会话值 |
| `comet.save_checkpoint("gate", {"score"})` | 请求保存当前场景、检查点名称及指定会话值 |
| `comet.load_checkpoint()`／`checkpoint()` | 请求从存档继续／读取本次恢复的检查点名称，无存档恢复时返回 nil |
| `comet.emit(name,value)` | 发送场景通知；脚本通过 `events` 声明接收方法 |
| `comet.emit_particles(count[,entity])`／`comet.clear_particles([entity])` | 按发射器配置爆发／清空，数量不超过 4096 |
| `comet.play_one_shot([entity])` | 按本实体或指定实体的音频源配置播放一次，空间音效记录触发位置 |
| `comet.log(text)` | 带源码位置的 Info 日志，仅在回调中使用 |

重开和切关在更新／固定更新／接触／事件回调中发起，不能在 `on_start` 或 `on_stop` 中请求。同帧首个请求优先，同一请求可以合并。
切关路径相对于项目 `assets/`，只接受 `.scene`，不允许绝对路径或 `..`。当前阶段完成后才切换，会话值在该帧结束时采集，并在下一关 `on_start` 前恢复；脚本的 `self` 和实体运行状态不会跨关携带。
将跨关进度放进会话值，关卡局部状态放进 `self` 或组件。App 先准备候选资产、等待就绪，再启动下一关；加载失败会记录错误并保留当前关卡。
Editor Play 可以切关，重开使用当前关卡初始内容，Stop 始终恢复进入 Play 前的编辑场景与历史。

检查点是单槽进度存档：保存时显式选择会话键，值在调用时复制；只接受已有且不重复的键，空列表表示仅保存检查点名称。
脚本调用只提交请求，宿主在帧边界执行磁盘读写并记录成功／失败；同帧脚本失败或 Editor Stop 会丢弃待处理请求。保存和继续同样不能在 `on_start`／`on_stop` 中请求。
恢复通过持久场景身份定位关卡，因此项目内保留 `.meta` 的场景改名不影响存档；场景缺失、坏存档或版本不支持会报错并保留当前关卡。
进度值和 `comet.checkpoint()` 在恢复场景的 `on_start` 前就绪，项目脚本负责据此选择出生点、隐藏已收集物等；运行时 ECS、物理状态和 Lua `self` 不序列化。
`restart_scene()` 重开的是入关状态；要回到最近保存的检查点，使用 `load_checkpoint()`。普通切关不会携带检查点名称。

App 存档位于玩家设置目录的 `saves/checkpoint.json`（按项目 UUID 隔离）；Editor Play 使用项目 `.comet/editor/checkpoint.json`，不会覆盖 App 进度。尚未保存的编辑场景不能存档。

```lua
local script = {}
script.events = {save_progress = "save_progress", continue_game = "continue_game"}

function script:on_start()
    if comet.checkpoint() == "west_gate" then
        comet.self_entity():set_position(-6, 2, 0) -- 项目定义的检查点出生位置
    end
end

function script:save_progress()
    comet.save_checkpoint("west_gate", {"score", "collected"})
end

function script:continue_game()
    comet.load_checkpoint()
end

return script
```

示例中的 `score`／`collected` 须先由玩法脚本写入会话；关卡自身的数值与规则仍由项目定义。

普通 Collider 支持盒、球、胶囊、凸包和静态三角网格。胶囊以实体原点为中心，沿本地 Y 轴，可随实体旋转；总高度须大于直径。球／胶囊要求正的等比缩放；角色控制器的胶囊仍以脚底为原点，两者不混用。

凸包／静态三角网格在 Inspector 的“形状”下拉中选择，并指定“碰撞网格”，可引用模型子网格，且不要求同实体有 Mesh Renderer。
凸包包住网格的所有有效顶点，可用于动态／运动学／静态刚体；凹槽不会保留，复杂模型宜另做低面碰撞网格。静态三角网格保留凹面，仅允许静态刚体，按三角形逆时针正面响应碰撞。
两者沿用网格原点，支持正非等比缩放；使用绑定姿态，不随蒙皮动画变形，仍不支持有父级的刚体。空几何、坏索引、无体积凸包和超出后端凸包上限的几何会报错。
修改碰撞网格、缩放或已索引的源版本后在下一固定步重建，动态凸包保留速度；源未成功重新导入或构建失败会中止当前运行，不继续沿用旧碰撞。
只用于碰撞的网格不上传 GPU；同版本、形状与缩放共用物理形状。视口线框使用构建后的物理形状：凸包显示包络，静态网格显示去重后的三角形边。几何不可用时显示红色十字并记录原因，不保留旧线框；不支持的刚体运动类型／父级配置用红色线框标记。

Collider 与 Character Controller 的“所属碰撞层”和“检测碰撞层”均支持 32 层勾选，默认属于第 1 层并检测全部层。
碰撞和触发器要求双向匹配：A 检测 B 所属层，且 B 也检测 A 所属层；关闭任一方检测即可禁止这对接触。角色的移动、蹲伏空间检查和刚体响应沿同一规则。
运行中修改层／掩码在下一次固定同步生效；Play 修改不会写回 Edit，Edit 修改支持撤销。存储使用无符号 32 位整数，第 32 层对应 `0x80000000`，不经过浮点转换。

射线／形状扫掠／重叠查询读取最近一次同步的物理世界，暂停时不会自动推进；使用世界坐标，向量为 `{x,y,z}`。
射线第二参数是完整位移而非单位方向；零长度或非有限参数会报错。射线从实体内部出发时命中比例为 0，法线取射线反方向。
`options` 可省略；Lua 默认忽略自身且不命中触发器，`{ignore = other_entity, include_triggers = true}` 可覆盖，`ignore = false` 表示也检测自身。
查询可设置 `layers` 掩码（默认 `0xffffffff`），只检查目标所属层，不使用目标的“检测碰撞层”；例如 `{layers = 0x80000000}` 只查询第 32 层，`layers = 0` 不命中任何实体。
返回实体引用仍需按正常实体生命周期使用；C++ 接口在 `PhysicsAccess`，过滤条件显式传入，默认空 UUID 表示不忽略实体。
形状扫掠以原点为形状中心，支持 `shape = "box"` 的 `half_extents`、`"sphere"` 的 `radius`、`"capsule"` 的 `radius`／`height`；可选 `rotation = {x,y,z}` 为欧拉角度。
扫掠过程中姿态固定，只沿位移平移；起点重叠时返回比例 0，接触点仍属于目标表面。C++ 复用 Collider 的几何字段与 Transform，并可用已配置 CPU 网格来源的凸包扫掠；三角网格不能作为扫掠形状。查询不会创建场景实体。

```lua
local x, y, z = comet.position()
local hit = comet.raycast({x, y + 1, z}, {0, -3, 0})
if hit then
    comet.log("地面距离：" .. (3 * hit.fraction))
end
local nearby = comet.overlap_sphere({x, y, z}, 4, {include_triggers = true})
local blocked = comet.shape_cast(
    {shape = "capsule", radius = 0.3, height = 1.8},
    {x, y + 0.9, z}, {0, 0, -2})
```

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

运行时复用内容使用 Prefab 参数：

```lua
script.properties = {unit = {type = "prefab"}}
function script:on_start()
    self.spawned = comet.instantiate_prefab(self.parameters.unit, {0, 1, 0})
end
function script:update()
    local unit = comet.find_entity(self.spawned)
    if unit then -- 阶段末完成生成后才能查到实体
        -- 使用生成的实体
    end
end
```

App、Editor Play 和导出共用依赖收集：脚本声明引用的 Prefab 及其网格、材质、脚本在入关前准备。
生成复用编辑器的子树实例化与内部引用重映射；运行实例不携带编辑器源基线，Stop 丢弃尚未提交的请求。
不能从任意文件路径临时加载 Prefab。未分配、资源缺失或超出单阶段生成预算会明确报错。
Demo 的 `prefabs/` 保存角色、拾取物和终点门；两关角色脚本生成终点门，收集完成后打开，重载角色脚本不会重复生成。

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
`api.emit(name)` 向当前活动场景排队命名事件，返回是否接受；游戏脚本通过 `events` 表处理，下一次 Runtime 更新才执行。
例如项目 UI 发送 `api.emit("character.next")`，Demo 的角色脚本检查通关条件后调用 `comet.load_scene`。无游戏时返回 false；候选 UI 准备期不能发送事件。

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
| `lighting/`、`depth/` | 前向光照、AO 与共用几何深度 |
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

导出启动场景及额外关卡的运行资源：

```bash
./build/tools/asset/comet_prepare_project ./demo --export-assets /tmp/comet-game-assets scenes/character_second.scene
```

目标目录必须不存在；失败会清理本次临时目录，不覆盖已有导出。输出包含 `project.json`、`assets.index.json` 和 `assets/`，
资产身份及场景路径保持稳定，模型／图片／Shader 转为运行产品，场景去掉编辑器来源基线，不携带 `.meta` 或开发缓存。
导出包含所选场景依赖、UI 静态引用和全部项目 Lua 模块；脚本切换的其他关卡须显式列出，动态拼接的 UI 图片路径尚无额外包含入口。
上述命令只交付资源目录；macOS 可进一步打包为独立 App：

```bash
python3 tools/export_app.py ./demo /tmp/CometGame.app --scene scenes/character_second.scene
```

也可从编辑器“项目 → 导出 App”选择输出路径及额外关卡。当前场景须先保存；界面实时显示构建、资源准备、打包和签名阶段，失败时展开可复制的日志。
两种入口都会自动配置并增量构建 `app-release`，复用 `build-release/`；构建失败立即停止，不打包旧程序。首次构建可能较慢。
默认仅包含启动场景，双关卡 Demo 还须选择 `character_second.scene`。
打包需要 Python 3.9+、CMake、macOS 命令行工具及构建时的 Vulkan SDK；导出产物包含 Vulkan Loader／MoltenVK、动态库、引擎字体／图标及运行配置，可搬移运行。
使用本机架构与临时签名，不包含跨架构构建、Developer ID 签名或公证流程。

包内 `Contents/Resources/game` 为只读运行资源；日志、缓存和存档写入用户目录 `~/Library/Application Support/Comet/players/<项目 UUID>/default/`，不写回 App。
`ctest --preset dev-debug -R packaged_app` 在 macOS 上验证搬移后的真实程序：禁止读取源码、构建目录与全局 SDK 驱动，也禁止写入 App；验证 UI 加载、运行帧和存档。
该测试覆盖构建失败、重复输出、缺失关卡和缺失内置 UI 资源的失败路径；窗口交互与关闭由用户验收。

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
./tools/render_benchmark/run.sh /tmp/comet-skeletal.csv 64 640 360 480 0 8 skeletal
```

默认报告位于 `build-release/reports/render-benchmark.csv`；支持静态／移动、脚本、骨骼动画、光源、裁剪、项目 Shader 及活动／休眠刚体场景。
报告记录画质与负载、整帧和分段耗时、P50/P95/P99、draw／上传量及 VMA 当前分配字节（不代表 CPU 分配次数或内存峰值）。
`skeletal` 使用默认角色的 `walk` 片段，生产动画系统按每帧 1/60 秒推进，继续使用 Runtime／RenderThread 重叠路径。
CPU 采样与蒙皮准备包含在渲染准备／录制时间中；当前 GPU graph 区间不包含图录制前的蒙皮 compute，不能把它当作全部 GPU 成本。
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

## NPC 导航

通过“文件 → 切换场景”打开 `scenes/navigation.scene`，点击运行。NPC 在隔墙两侧巡逻；玩家靠近 6 米内会被追逐，远离 11 米后 NPC 返回起点并继续巡逻。移动方块只影响局部避障，场景仍使用现有 WASD／相机控制；玩家脚本 `character_controls.lua` 只调用通用角色控制，不附带收集／存档规则。

- **导航区域**：一个场景一个区域。实体世界位置为中心，“半尺寸”直接使用世界米数，不随实体旋转／缩放变化。首个固定步从静态非触发碰撞体生成可行走面；调整区域或静态碰撞后重新运行。
- **导航角色**：与 Character Controller 放在同一根实体。角色半径／高度不能超过区域烘焙尺寸，角色允许的台阶／坡度不能小于区域设置。“朝向实体”选择无物理组件的外观父实体；动画仍由 Character Animation 根据实际速度驱动。
- **行为**：项目脚本 `npc_patrol.lua` 保存巡逻点、发现距离和状态机；引擎负责路径缓存、查询节流、跟随与避障。无需在 Lua 中维护拐点或直接改角色位置。

在脚本 `fixed_update` 中调用 `comet.navigate_to(x, y, z, run)`；`run` 可省略。返回 `moving`、`arrived`、`no_path`、`blocked`、`budget_exceeded` 或 `disabled`。不再调用就不提交移动意图；查询失败或预算耗尽时停下，后续固定步可重试。无路径与配置错误不同：无路径返回状态，缺少必要组件、无效尺寸等报告脚本错误。

导航使用同步、受限查询（每步最多 8 次完整寻路、2048 个搜索节点、256 个路径多边形），稳定目标复用路径并按配置间隔更新。暂停不推进，单步只推进一个固定步；停止和切关释放导航运行态。

首版是地面导航：只覆盖静态碰撞面，动态物体使用候选方向与胶囊扫掠避让。暂不支持离线导航文件、动态切割、跳跃连接、飞行寻路或密集人群协调；动态门完全封路时会等待，不保证能找到远处的另一条绕行路线。

## 静态网格 LOD

打开 `scenes/mesh_lod.scene`，可观察近远球体的几何细节切换。球体三档分别为 960／224／48 个三角形；在 Editor 中移动相机，或 Play 后使用游戏相机控制靠近／远离。

在带“网格渲染器”的实体上添加“静态网格 LOD”：

- 原网格为 LOD 0。把同坐标、同原点的简化网格填入 LOD 1／2；各级共用该实体的材质。复用现有 glTF／GLB 网格导入和模型子网格，无需新资产格式。
- 屏幕高度比例低于阈值时选更低层级。LOD 2 阈值应小于 LOD 1；若填反，运行时按 LOD 1 上限约束。比例按视图高度归一化，同宽高比的分辨率变化不会单独触发换级。
- 默认 10% 滞回：缩小时低于阈值的 90% 才降级，放大时超过 110% 才升级，减少边界抖动。
- “层级预览”可强制 LOD 0／1／2；该值随场景保存，也影响 Play／App，验收后切回“自动”。未指定的层级跳过，强制空层级回到最近的较精细层级。

相机视图、方向光的各级阴影、聚光灯阴影与点光源各面分别选级；阴影按其投影范围判断，不照搬主相机结果。选级后继续自动实例化；包围盒覆盖所有层级，拾取、物理与导航仍使用各自原有几何来源。
“渲染统计”启用采集后显示主绘制／阴影合计最近一帧提交的网格三角形数，可同时查看 CPU／GPU 耗时与显存。

LOD 引用参与场景／Prefab 克隆、撤销、资产热重载与 App 打包。指定的网格缺失或包含蒙皮时会报告错误并跳过该物体，避免显示旧资源造成误判。首版只支持手工准备的静态网格，不包含自动减面、蒙皮 LOD、渐变交叉淡入或按层级流式加载；所有指定层级会驻留，减少三角形数不代表减少显存。

## 方向光级联阴影

通过“文件 → 切换场景”打开 `scenes/shadow_cascades.scene`。场景包含从近到远的立柱与细杆，可在 Edit 中移动相机，也可 Play 后控制游戏相机观察阴影。

选中方向光，在 Inspector 的“灯光”组件启用“投射阴影”，调整“方向光阴影距离（米）”（默认 100）。距离越大覆盖越远，但每级的世界空间纹素也变大。当前只为按实体 ID 排序后的第一盏有效、开启阴影的方向光绘制阴影。

四级随当前相机自动选择，交界最后 10% 混合相邻级，覆盖末端淡出。Edit、Play 和 App 使用同一链路；每级独立选 LOD 和剔除，不因主相机看不到投射者而直接丢弃它。

验收时观察近处细杆、远处立柱，缓慢平移／旋转相机，并把阴影距离改为 30／100 米后保存重开；检查覆盖范围、交界连续性和闪动。配合“渲染统计”查看阴影耗时与三角形合计。阴影图集及其预算见下节。

## 聚光灯阴影与预算

打开 `scenes/spot_shadows.scene`，四盏彩色聚光灯照射立柱和横杆。选择灯光，在 Inspector 启用“投射阴影”，调整位置、角度、“范围”和“外角”；阴影与光锥共同更新。方向光阴影距离不影响聚光灯。

每个 View 最多保留 4 盏聚光灯阴影，每盏 512×512；先排除保守光锥范围与相机视锥不相交、无场景覆盖或强度为零的灯，再按实体 ID 顺序分配。超预算的灯仍参与照明，只是不投影。“渲染统计”启用采集后显示实际数量和超预算数量；打开示例中的“预算演示灯（默认关闭）”即可检查上限。

方向光、聚光灯和点光源共用深度绘制、Alpha Mask、双面、实例化与 LOD 链路；透明混合材质不投射实心阴影。局部光的近裁剪为灯光范围的 1%，最多 5 厘米；紧贴光源的物体可能被裁掉。

每个 View、每个帧槽固定使用 2048×4096 D32 图集（32 MiB）：上方四块 1024×1024 放方向光级联，下方四块 512×512 放聚光灯，剩余十二块放两盏点光源的六面阴影。灯光数量不会继续扩大图集；绘制成本仍随投射者数量变化。Edit／Play／App 使用同一实现，实际观感请检查灯光移动、裁切物体、光锥边缘，以及保存重开后的结果。

## 点光源六面阴影

打开 `scenes/point_shadows.scene`，两盏暖／冷色点光源照射立柱、横杆、墙面和顶板。在 Inspector 的灯光组件中切换“投射阴影”、移动灯光或调整“范围”；点光源向四周照射，旋转和锥角不影响它。启用“预算演示灯（默认关闭）”可检查超额行为。

每个 View 最多两盏点光源投射阴影，每盏六面、每面 512×512，预算独立于聚光灯。候选先按范围与相机／场景界限筛选，再按实体 ID 分配；超额灯继续照明但不投影，数量显示在“渲染统计”。每个面独立剔除与选择 LOD，六面合计绘制开销可能明显高于一盏聚光灯。

六面交界使用额外保护纹素进行滤波，避免 PCF 越界采到其他灯光；有限分辨率仍可能产生锯齿和接缝两侧的细微滤波差异。近裁剪范围内的遮挡物不投影，紧贴灯光的效果需要实际验收。Edit／Play／App 共用实现，请检查墙角与顶板的接缝、灯光移动、开关阴影以及保存重开后的效果。

## 屏幕空间环境遮蔽（AO）

打开 `scenes/ambient_occlusion.scene`，在 Hierarchy 选中“场景”，于 Inspector 的“画面效果”切换“环境遮蔽”，比较墙角、台阶与立方体接触处。半径以世界米数计，强度控制环境光衰减；场景保存、撤销、Play 与 App 使用同一设置，旧场景默认关闭。

AO 只衰减 PBR 环境光，需启用 Environment 的环境照明；直接光、自发光、天空盒和透明混合材质不受影响。裁切材质按 Alpha Mask 参与遮挡，纯透明混合材质不参与。调整半径时以物体尺寸为准，过大的半径容易在物体轮廓周围产生晕影。

首版增加一次全分辨率、单采样的相机深度绘制，PBR 每个片元固定采样 16 次，兼容主画面的 MSAA。深度缓冲每个帧槽约为宽×高×4 字节，关闭后跳过绘制与采样（保留已创建资源便于再次开启）。只能感知当前相机深度中的表面，画外或被前景完全遮住的几何不参与；没有时域积累、半分辨率降噪或 GI 反弹。实际观感请检查相机移动、画面边缘、薄物体与不同渲染分辨率。

## 基础距离雾

打开 `scenes/distance_fog.scene`，在 Hierarchy 选中“场景”，于 Inspector 的“环境”启用“距离雾”。设置线性雾颜色、起始距离和完全覆盖距离；近于起始距离保持原色，中间线性过渡，远于完全覆盖距离使用雾色。距离以相机前向的世界米数计，支持透视和正交相机；起始距离必须非负，结束距离大于起始距离且不超过 100000 米。

设置随场景保存、克隆、撤销和 Play／App 使用，旧场景默认关闭。PBR 与无光照网格在 HDR 着色后、透明混合与色调映射前应用雾色，因此自发光也会被雾遮蔽，Alpha Blend 保留自身透明度，Alpha Mask 的空洞仍显示背景。

当前是基础网格距离雾，没有新增渲染阶段或深度纹理。天空盒、背景色、粒子、编辑器辅助线及 UI 保持原样；可手动把背景色设为雾色，减少远处接缝。项目自定义 Shader 保持自己的着色逻辑；需要雾化时按引擎 `common/frame.glsl` 的帧布局读取参数，并参考 `common/fog.glsl` 实现，导入器不会自动修改源码或跨项目引入引擎文件。实际验收请比较近远柱体、相机移动及开关恢复；高度雾、体积散射和粒子雾化后续扩展。

## 局部反射探针

打开 `scenes/reflection_probes.scene`，左右金属物体分别使用暖色与蓝色环境反射。选择探针实体，可在 Inspector 修改环境贴图、影响范围半尺寸、边缘过渡、强度、贴图 Y 轴旋转及盒投影校正；实体变换决定捕获中心和世界轴对齐包围盒。边缘过渡以世界米数计，范围外回到全局镜面环境，重叠区域按权重归一化。探针仅控制 PBR 镜面环境项，漫反射环境光、直接光与天空背景仍独立设置。

每个 View 按实体 ID 保留最多四个有效、与相机视锥相交的探针；超预算会输出诊断。当前使用固定采样器绑定，避免依赖描述符动态索引。设置沿组件描述符保存、克隆、撤销和 Prefab 复用，引用环境走现有资产加载、重载与 App 依赖导出；禁用探针不再产生运行时加载需求。

示例两个小 HDR 由数学图案生成，随仓库经 Git LFS 保存，不需要下载大型环境图。它们用于验证区域混合与盒投影，也可以用下面的场景捕获替换。

在 Edit 模式选中探针，设置“烘焙面尺寸”和“烘焙距离”，点击 Inspector 中的“烘焙反射”。面尺寸支持 16、32、64、128、256、512；默认 128。捕获中心是实体世界位置，使用原网格、静态光源及全局环境，不包含 Animator／角色控制器／非静态刚体及其子级，也不包含粒子、局部反射、雾与后处理。脚本运行时才移动的普通网格不会自动识别为动态物体。

六个方向使用同一份世界和 Shader 快照，读取色调映射前的线性 HDR，再保存为 `assets/baked/reflection_*.hdr` 并沿环境导入器生成粗糙度预过滤。保存场景后，Editor Play、App 和导出产品共用这个环境引用；导出消费预计算产品，无需在玩家机器上捕获或卷积。

Inspector 显示烘焙是否过期。输入指纹覆盖静态几何及变换、材质和传递依赖、灯光、全局环境、内置 Shader 及捕获设置，保存重开后仍可比较。局部范围、边缘过渡和反射强度只影响采样，不要求重烘焙。输入变化时保留旧结果供比较，需显式再次烘焙；这不是动态反射。捕获期间修改输入、切场景或进入 Play 会拒绝或取消旧结果。重新烘焙创建新资产，旧结果保留，引用替换可以撤销；不再需要的旧 HDR 可在项目面板手动删除。

## 静态探针 GI

在 Edit 中给实体添加“静态 GI 区域”，设置半尺寸、探针间距、射线样本和反弹次数，点击 Inspector 的“烘焙 GI”。区域以实体世界位置为中心、按世界坐标轴对齐；半尺寸和间距以米计，不随实体旋转和缩放。间距会调整为均匀覆盖区域的网格，每轴至少 2 个、最多 64 个，总数不超过 4096；减小区域或增大间距可降低成本。

烘焙在后台读取已导入的 CPU 产物，计算静态三角形的遮挡、漫反射反弹和自发光；Inspector 显示探针进度并支持取消。静态筛选与反射烘焙共用，排除 Animator、角色控制器、非静态刚体及其子级。仅支持内置 PBR／无光照材质，读取基础色、金属度、自发光与 Alpha Mask；混合透明体、法线贴图细节与镜面反弹不参与。自定义 GPU Shader 无法在 CPU 求值，会给出诊断。脚本运行时才移动的普通网格不会自动识别为动态物体。

结果保存到 `assets/baked/gi_*.gi`，引用替换支持撤销。保存场景后 Play、App 和导出产物共用结果；玩家机器无需烘焙。静态几何、材质、光源、环境或区域设置变化会显示过期，需显式再次烘焙；修改 GI 强度和法线偏移仅影响运行采样。烘焙期间修改输入或切换场景不会接纳旧结果；取消不发布部分产物。重复烘焙保留旧资产以支持撤销，不需要的旧 `.gi` 可在项目面板删除。

PBR 在有效区域内用 GI 替换全局 IBL 的间接漫反射，直接光、局部／全局镜面反射保持独立。静态和移动表面均按实际世界位置采样；区域外或没有有效探针时回退全局 IBL。已烘焙的区域固定在世界中，移动组件不会搬动旧光照；动态物体不会改变已烘焙遮挡，也不会向场景反弹新光。每个 View 首版使用一个可见区域，其他可见区域计入超预算统计。

烘焙报告分别显示输入数据、求解器自有工作数据及运行纹理字节，不代表整个进程工作集或驱动实际分配。`scenes/gi_closed_room.scene` 和 `scenes/gi_open_room.scene` 共用红／蓝墙、发光顶灯和静态遮挡物，后者移除相机后方的前墙。两者自带三次反弹产物，无需先烘焙即可打开；Play 中的白方块复用移动平台脚本与运动学刚体，只采样光照，不参与静态烘焙。当前相机位于室内，关闭 GI 和环境照明后，可见表面会变黑，因为示例没有额外直接光源。

对比时保持相机、曝光、AO 与材质不变：关闭区域并关闭环境照明为“无 GI”；关闭区域、开启环境照明为“仅 IBL”；启用区域后，将漫反射反弹数改为 1／3 并显式烘焙，比较有色墙面染色与遮挡。IBL 不感知房间几何，均匀环境下白物体会缺少明暗层次；GI 增加低频间接光与遮挡。探针不能替代高频接触阴影，细节较多时需要更密的布置；动态物体不改变静态遮挡。

本机参考成本（Apple M4、`editor-dev` 优化构建）：每区域 216 个探针、214 个有效探针，每探针 1024 条主光线。以下为三次重复的后台读取与求解耗时，不含首次资产导入、源指纹检查和主线程发布；结果逐字节一致。

| 场景 | 静态三角形 | 一次反弹 | 三次反弹 | 运行纹理逻辑数据 |
| --- | --- | --- | --- | --- |
| 封闭房间 | 108 | 0.225～0.232 秒 | 0.442～0.447 秒 | 138.4 KiB |
| 开口房间 | 96 | 0.194～0.200 秒 | 0.323～0.336 秒 | 138.4 KiB |

GPU 使用同一封闭场景与固定相机，1280×960、MSAA 1、关闭 validation／Bloom／AO，预热 100 帧后采集 300 个独立 GPU 样本，并交替重复三轮。`scene` pass 的 P50：仅 IBL 为 0.124～0.144 ms，三次反弹 GI 为 0.830～0.834 ms；P95 分别为 0.325～0.333 ms 和 1.027～1.046 ms。它只描述该小型参考场景的着色成本，不是整帧或其他设备的性能承诺。相同探针网格的一次／三次反弹运行采样成本基本相同，增加反弹主要影响离线求解；纹理逻辑字节也不等于驱动实际分配的显存。
