# 引擎架构与运行时边界

本文描述当前模块、所有权和关键时序。功能规划与验收见[路线图](../engine-roadmap.md)，构建和操作见 [README](../../README.md)。

## 构建模块

项目使用 `build/`、`build-editor/`、`build-release/` 三个构建目录，对外提供一个 `engine` 动态库和 `Comet::Engine` 目标。
内部对象库约束依赖与增量编译，全部汇入 engine；App、Editor 和资产准备工具直接链接 engine。
源码归属由 `engine/cmake/module_sources.cmake` 声明，依赖由 `modules.cmake` 声明。目录组织按功能，编译模块按依赖边界组织。

| 模块 | 职责 | 主要依赖 |
| --- | --- | --- |
| Foundation | 错误、文件、UUID、参数值、数学、任务、项目路径、日志和 CPU 计时 | GLM、spdlog、Threads |
| Serialization | 文件编解码流程、错误定位、JSON 编解码 | Foundation、simdjson |
| ShaderContracts | 后端无关 Shader 契约与 SPIR-V 反射 | Foundation、SPIRV-Reflect |
| AssetData | Handle、Registry、CPU 产品数据、脚本定义和资产字段序列化 | Serialization、ShaderContracts |
| Input | 采样值、动作、输入组、运行域求值、改键草稿和覆盖 | Serialization |
| World | 实体、组件、描述符、层级、场景内容保存与复制 | AssetData、EnTT |
| Runtime | 时间、暂停／单步、会话与 System 生命周期 | World、Input |
| Audio | AudioService、Voice、播放设备与 AudioSystem | Runtime、miniaudio |
| Physics | PhysicsService、刚体世界与 PhysicsSystem | Runtime、Jolt |
| Scripting | 行为 VM、Lua 校验与 ScriptSystem | Runtime、Lua |
| AssetPipeline | 扫描、索引、源导入、Artifact 与任务队列 | AssetData、stb_image、fastgltf |
| RuntimeAssets | 需求、加载、失效、版本编排和资源发布契约 | AssetPipeline、Audio／Script 定义 |
| Platform | Window、GLFW 事件、输入采样与剪贴板 | Input、GLFW |
| Graphics | Vulkan／VMA、设备、资源、命令、同步与 Surface | ShaderContracts、Platform |
| Render | 帧、呈现、场景提取、渲染与资产 GPU 发布 | Graphics、World、Platform |
| GameUi | RmlUi 会话、输入／GPU 适配和项目控制器 | Render、Platform、RmlUi、Lua |

Project、Profile、玩家设置聚合、Application 与 Engine 是组合层。WindowSettings、VulkanSettings、RenderSettings、AudioSettings 等值类型仍归所属功能模块。
模块接收自身所需参数，不接收完整 Config。Shader 编译工具是生成 Shader 所需的独立进程，复用 Foundation，不反向链接 engine。

## 模块依赖方向

下图箭头表示依赖；组合层可以装配各模块，模块不反向访问宿主。

```mermaid
flowchart TD
    Host[Application / Engine / Editor / App] --> RuntimeAssets
    Host --> Runtime
    Host --> Render
    Host --> GameUi
    RuntimeAssets --> AssetPipeline
    AssetPipeline --> AssetData
    World --> AssetData
    Runtime --> World
    Runtime --> Input
    Audio --> Runtime
    Physics --> Runtime
    Scripting --> Runtime
    Render --> World
    Render --> Graphics
    GameUi --> Render
    Graphics --> Platform
    Platform --> Input
    AssetData --> Serialization
    Input --> Serialization
    Serialization --> Foundation
```

- World 和 Runtime 不包含窗口、渲染或具体服务后端。System 从 Scene 取得参数，通过明确服务同步，后端不访问 Scene。
- AssetData 不依赖导入或运行实例。RuntimeAssets 不包含渲染对象；`asset/runtime/render_asset_publisher.h` 声明发布契约，Render 提供实现。
- Render 消费提取数据和资源服务，不读取项目文件或访问 AssetManager；Graphics 不反向依赖 Render。
- Jolt、miniaudio、Lua、GLFW 等头只进入所属后端或适配实现。Engine 不包含 Editor／ImGui。
- Editor 分为无 ImGui 的 `editor_core`、呈现适配 `editor_imgui` 和功能界面 `editor_ui`。Engine 由宿主组合，功能代码显式借用所属模块的工作流和帧快照。
- Renderer 公共头前置声明帧调度、呈现和 Overlay 类型；需要调用这些类型的集成代码显式包含相应头文件。

CTest 检查直接及传递 include，并在临时副本中验证反向依赖会被拒绝。日志、组件描述符、Registry 和第三方全局状态保留唯一所有权。

## Owner 结构

| Owner | 拥有 | 借用／交付 |
| --- | --- | --- |
| Engine | Window、Scene、Registry、TaskScheduler、Renderer、Runtime、音频与物理服务 | 同步调用宿主钩子；提供运行和渲染入口 |
| SceneRuntime | RuntimeSession 和 System 实例 | 借用当前 Scene 与 RuntimeServices，停止后结束借用 |
| System | 脚本实例或实体／组件绑定 | 借用资产定义和服务；同步配置，回写输出 |
| AssetManager | 加载、导入任务、失效和发布编排 | 借用索引与 Registry，通过 RenderAssetPublisher 发布 GPU 版本 |
| Renderer | 图形上下文、资源服务、帧调度、呈现和场景渲染状态 | 交付离屏帧／诊断快照；在途 slot 保活所用版本 |
| EditorAssets | 编辑器项目索引、源文件操作、资产管理和引用维护 | Worker 交付候选；主线程复核并提交 |
| EditorSceneSession／SceneDocument | 保留的 Edit 内容、Play 切换、文档路径和保存点 | 通过 Engine 激活场景；命令历史只编辑 Edit 内容 |
| 宿主玩家设置／ProjectUi | 玩家选择实例、页面和独立 UI 控制器 | 宿主注入读取、保存、应用及确认服务 |

Worker 读取源码、准备 CPU 产品和候选；主线程发布索引、Registry 和 GPU 版本。文件监听只提示复核，不直接修改世界或资源。
候选携带实际输入和版本身份，过期结果拒绝发布；连续保存沿既有任务背压和最新请求合并流程处理。
源文件操作属于 `editor/assets`，运行时加载属于 AssetManager；两者共享索引，分别负责自己的工作流。

## 应用启动与失败清理

启动入口加载当前 Profile。App 再合成项目默认值与玩家选择；Editor 合成本地窗口状态，游戏显示设置只用于项目视口。
Engine／Renderer／设备等工厂先准备完整 owner，成功后交付；部分准备失败随局部对象清理，不发布空句柄成功对象。
`on_init` 开始后，预期错误沿 Result 返回，关闭顺序为 Engine 停止准备、宿主清理、Engine 销毁、Diagnostics 释放。
宿主关闭钩子只执行一次，并能清理部分初始化。未预期异常不由 run／launch 捕获；第三方解析异常在对应适配层转换。
项目描述损坏时启动失败；Editor 的启动场景损坏可报告后打开空场景，供修复，不覆盖源文件。

## 一帧经过哪里

1. Engine 处理事件与时间，宿主 `on_update` 消费上次 UI 请求、维护资产与切换模式。
2. Renderer 回收上传并准备 slot／交换链；帧就绪时调用 `on_frame_ready`，收集 ImGui 请求、属性编辑、Gizmo 和输入授权。
3. `on_runtime_input` 交付只读授权快照；Runtime 执行有界 Fixed Update，再执行一次普通 Update。
4. SceneExtractor 更新变换并交付只读 RenderScene 快照；Engine 保留它直到本次录制完成，Main 的 SceneResolver 固定资源、材质值和选定 Shader 引用，交付只读 RenderWorld 快照。
5. 世界界限、光照和方向光阴影准备一次；所有 View 分别生成相机、裁剪、绘制和后处理，再合并等待、绘制 Overlay、提交并呈现一次。

数据链为 `Scene → SceneExtractor → RenderScene → SceneResolver → RenderWorld → RenderSubmission → SceneRenderer`。

| 数据／状态 | Owner | 消费约束 |
| --- | --- | --- |
| 场景内容、变换与变化记录 | Scene | 受控写入记录变化；提取器和物理系统读取批次，成功后由 Scene 清理 |
| CPU 快照与实体槽位 | SceneExtractor；消费者持有只读快照 | 快照不借用组件，可独立保留；提取器独占写入及自己的索引 |
| 解析后的世界与资源引用 | SceneResolver 生产；Renderer 持有只读 RenderWorld | 独占时复用，旧读者仍在时复制后更新；ResolutionCache 随世界复制，输入检查与输出 item_changes 分开 |
| 世界界限和本帧几何列表 | Renderer／RenderGeometry | 缓存界限值；列表借用世界和缓存，clear_frame 结束借用，reset 同时清空缓存 |
| 光照与方向光深度图 | View 集合共享的 ShadowPass | 每个帧槽有一张深度图；本帧首次录制写入，其余 View 采样同一结果 |
| 相机与诊断、目标、拾取、调试线 | Renderer 的 ViewState／SceneRenderer | 每个 View 独立；隐藏保留目标并消费自身的临时请求 |
| 可见列表、材质帧资源和绘制队列 | SceneRenderer／MaterialRenderer | 同步消费 RenderSubmission；实际使用的 GPU 版本由 FrameSlot 保活 |

App 使用默认 View；Editor 的游戏相机使用默认 View，编辑相机使用另一个 View。Play 中两者观察同一个运行副本。
创建／移除 View 在帧边界完成，场景切换不交换目标；画质重建与热发布不能释放在途帧的旧资源。

增量更新沿提取、解析、几何准备传递同一套槽位和结构编辑规则。Scene 内容版本、输入／输出批次和资产发布版本有不同用途，不能互相代替。
局部批次要求版本连续；跨生产者、漏过批次、大量修改或缺失资源造成压缩时完整核对。手工快照没有变化提示，逐项检查身份与版本。
Registry 总版本变化时检查持有引用的条目版本；已解析材质的参数版本按唯一身份检查，变化后才扫描受影响物体并重新捕获快照。未变资源和界限继续复用，失效位置属于各自世界，不被其他消费者或快照副本消费。
相机、Light 和场景设置仍完整提取。静态 extract 不消费记录；增量 update 返回 `shared_ptr<const RenderScene>`。
未变场景共享同一快照；更新时没有旧读者就复用存储，仍有读者则复制后写入。换场景和 reset 释放生产者持有的状态，旧快照持续有效到最后一个读者退出。
SceneResolver::update 返回 `shared_ptr<const RenderWorld>`；资源刷新和 reset 不修改消费者保留的旧世界，串行帧释放读者后继续复用世界及物体存储。
提取、AssetHandle 解析和材质值捕获属于 Main；GPU 准备只读取世界快照，不重新查询最新 Shader。当前所有阶段仍串行执行，跨线程移交及 GPU 发布 owner 尚未接入。

渲染、物理组件只读访问，通过副本提交修改；同值或拒绝的写入不增加版本。物理姿态回写更新变换和渲染，避免再作为下一步物理输入。
这些进程内身份不写入场景文件，复制 Scene 获得新的实例代号。材质改绑、移除组件和 Runtime 停止会清理对应运行覆盖。

当前所有视图同步消费同一只读世界；借用 RenderSubmission 只在持有世界快照期间有效，不能单独排队。帧延期跳过 UI／提取／绘制，Runtime 仍推进；最小化等待并重置墙钟增量与待处理输入。
Editor 在提取前完成属性编辑；Renderer 只返回拾取值，Editor 在下一次 Main 更新消费结果，选择、Gizmo 和选中框一起更新。
切换 Scene 时结束旧交互、清理请求并重绑选择与引用。

### 输入主链与配置边界

Window 采样键鼠和标准手柄；Input 保存只读物理快照，RuntimeInput 按授权和阶段求值动作。
Fixed Update 累积边沿，重复阶段不重复消费；失焦、连接变化和授权恢复只建立基线，不伪造按下。
App 依据最终 UI 状态授权，Editor 依据视口焦点／悬停授权；输入屏蔽不等同于暂停模拟。

| 设置来源 | 保存位置 | 负责内容 |
| --- | --- | --- |
| C++ 默认值 | 所属模块的设置结构 | 完整、可运行的基础默认值 |
| 开发者 Profile | `config/profiles.json` 的对应分组 | 诊断、底层格式／交换链／在途帧参数、资源预算；启动只选择一个分组，缺省值来自 C++ |
| 项目默认值 | `project.json` | 游戏显示、画质、音量、输入和 UI 入口 |
| 玩家选择 | 按项目 UUID 隔离的用户目录 | display／quality／audio 完整设置与输入稀疏覆盖 |
| 编辑器本地状态 | 用户／项目本地状态目录 | 窗口、布局、快捷键、最近项目和会话 |

Profile 拒绝玩家设置及未知字段。窗口尺寸与最大化状态由宿主持久化；Window 只维护原生窗口与还原尺寸。
`config/player_settings` 用一份 `PlayerSettings<T>` 实现显示、画质和音量的文件检查、版本／项目身份校验、原子保存和保存后应用。
别名保留各功能名称，具体类型的字段读取／写入／校验仍归 DisplaySettings、QualitySettings、AudioSettings。
输入文件是按动作／绑定身份合并的覆盖，保留 PlayerInputSettings 的独立协议，不套用完整值存储。
App 的显示试用由 DisplaySettingsPreview 保存前态和 15 秒期限；确认成功才持久化，取消／超时恢复前态。Editor Play 只应用视口相关设置。

### 序列化与复用边界

| 位置 | 共享能力 | 调用方负责 |
| --- | --- | --- |
| `common/file_io` | 文本读取、有大小上限的二进制读取、原子文本／二进制分块写入 | 文件是否可缺失、预算和业务提交顺序 |
| `common/binary` | 小端整数／浮点数、带长度的字符串读写、分块 FNV-1a 哈希 | Magic、版本、长度前缀宽度、字段限制与领域校验 |
| `common/serialization` | 来源／字段错误定位、Serializer 文件加载／保存流程 | 格式选择和领域编解码 |
| `common/json` | simdjson 解析、对象／数组检查、必填与可选标量字段读取、固定长度浮点向量读写、键校验、可选路径查找、可缺失文件读取、Writer、编解码入口 | 项目、场景、材质等 Schema 与版本，缺省值、颜色及数值范围的领域校验 |

项目、资产、开发者 Profile 与快捷键共用 JSON 工具。JSON DOM 借用 parser；`Json::deserialize` 的回调必须返回拥有数据的结果。
快捷键冲突、资产身份、范围等属于具体功能，不放入通用 Reader。
Editor 的窗口、最近项目、项目会话与玩家输入／显示／画质／音量设置共用可缺失 JSON 文件的读取入口：缺失返回空值，已有文件的错误正常上报，不改写原文件。
各工作流决定默认值、保存时机与内存提交规则，解码结果拥有数据，错误来源字符串覆盖整个解析寿命。
资产 Serializer 不再拥有通用 JSON 文件工具。场景描述符共用于保存、恢复、编辑与内容复制；内容 clone 直接走内存快照，不通过 JSON 往返。
Mesh、ShaderProgram、Environment Artifact 与导入指纹共用二进制基础工具；各资产保留独立头部、版本与校验规则。
Environment 按纹理读取，发布时将头部和四份纹理存储分块原子写入，避免再复制一份完整像素 payload。

### 系统更新与场景边界

Scene 保存实体身份、组件和层级；结构请求在阶段末提交，遍历中不直接改变结构。RuntimeSession 保存本局值、输入组请求和重开意图。
父子关系只由 Scene 修改并保持无环；子树删除直接遍历子节点索引，收集完毕后从后代向祖先清理。
Runtime 统一控制系统启停、部分启动失败、暂停和单步；System 不重复实现生命周期状态机。
ScriptSystem 先执行玩法，PhysicsSystem 同步配置并模拟／回写，接触随后交付脚本；AudioSystem 同步声音源。
物理后端仅接收借用参数及身份值；系统检查必需组件和父级，后端在新建／变化时校验值域。
启动时登记全部刚体；运行时只同步脏实体的 Transform、刚体、碰撞体与父级变化。
Scene 的物理脏集合独立于渲染变更批次，固定步成功同步后清空；相同组件值不入队，销毁时移除脏项。
刚体增删触发登记列表清理，核对实体寿命与组件存在；少量配置修改和稳定步不扫描全部刚体。
运动学目标旋转不变时复用四元数，后端维护自己的运动学刚体列表并每步更新速度；列表不借用 Scene 组件。
AudioService 拥有设备和 Voice，PhysicsService 拥有 Jolt 世界和刚体；System 只保存实体绑定，不让内容组件持有后端对象。
动态姿态只回写实际活动或本步状态变化的刚体，沿同一变换校验入口更新场景／渲染，跳过下一步的物理输入通知。
删除／重建和休眠接触使用有效身份，旧请求不绑定到复用槽位。Jolt 并行尚未实现。

## Lua 脚本与参数

- `asset/script` 保存不可变入口／模块源码、字段默认值和事件声明，不依赖 Entity、Runtime 或 VM。
- `scripting/script_instance` 拥有每实体独立的 VM、定义／self 和模块缓存；ScriptSystem 保存实际运行定义并调度。
- ScriptComponent 保存 Handle 与稀疏参数覆盖；`common/parameters` 共享参数类型和过滤规则，普通参数编辑不重建实例。
- Inspector Edit 查询资产定义，Play 查询实际实例定义。查询核对场景、实体和组件寿命，返回值不延长实例寿命。

源码重载先准备并校验关联资产及候选 VM；准备失败保留旧版，成功后逆序停止旧实例并启动新版。
新版 `on_start` 或运行回调已可能修改世界，执行失败沿 Runtime 停止／Editor 恢复 Edit 路径处理，不承诺回滚副作用。
换版保留兼容参数覆盖，重建 self／模块，不迁移任意 Lua 状态，也不自动改写 Edit 内容；暂停时待继续或单步再换版。
项目 require 使用预先冻结的源码闭包，实例只执行已准备模块，不在运行回调中查找文件。模块为 source-only，不创建资产身份。
内存、指令、日志、实体／事件请求都有预算；实体和接触交付在阶段边界复核身份，停止回调不访问 Scene。
ScriptSystem 不监听文件、不另建资产版本缓存；UI 控制器的 VM 与行为实例分别拥有权限和寿命。

### 脚本材质覆盖

场景组件保存参数覆盖，运行实例只覆盖对应实体；共用 Material 资产保持原内容。
字段类型和值范围复用材质布局契约；Stop／运行副本销毁清除运行覆盖，不把 GPU descriptor 或文件操作暴露给 Lua。

## 项目 UI 控制器

RmlContext 提供通用文档、字体、输入与 Overlay 适配，不认识 demo 动作或菜单。项目的 `ui` 清单决定页面和控制器入口。
ProjectUi 负责清单装载与 UI 控制器桥接，宿主注入输入、显示、画质、音量的读取／保存／应用服务。
HUD、改键、菜单、倒计时、导航和草稿逻辑属于 `demo/assets/ui` 的 RML／RCSS／Lua，App 是通用宿主。
Editor Play 使用相同项目页面；ImGui 项目／玩家面板调用相同引擎业务能力，不复制存储规则。
Editor 的 GameUi 适配器只借用 Window、Renderer、Project 和设置服务，由宿主组合，不持有整个 Engine。
候选页面／控制器先准备，失败保留旧版；事件核对当前文档身份，候选事件不提前改变设置。
RmlUi Core 和 FreeType 静态编入 engine；FreeType 是字体后端。App／Editor 共用 engine 字体，ImGui／RmlUi 分别做图集和回退。
Editor 文案固定中文并内置在所属界面的 C++ 代码中，ImGui 稳定 ID 与显示文字分离。
Inspector 初始化时准备自己的中文显示描述，沿用引擎字段身份和读写回调；项目／脚本名称、原始诊断和日志保持原文。
当前一个进程只允许一个拥有 RmlUi Core 的 RmlContext；多上下文属于后续能力。
RmlRenderer 的文档回调只校验并保存独立 CPU 几何／像素，绘制收集只保存这些只读数据、裁剪、变换和位移，不创建或销毁 GPU 对象。
RmlContext::update 在 Main 更新布局并收集只读 DrawSnapshot；候选文档验证还准备 GPU 资源，成功后才同时替换页面和快照。GPU 录制入口只接收绘制数据，不读取 RmlUi 上下文或回入 DOM。
快照保存当时的几何、纹理、变换及上下文尺寸，裁剪在 GPU 录制时按实际目标缩放；资源 Handle 释放或文档关闭后仍可消费旧快照。
GPU cache 只弱引用 CPU 数据；释放 Handle 不改变本帧收集的数据，失效缓存在下一次后端准备时移除，在途 GPU owner 继续由帧槽保活。
CPU 数据随 Handle 或绘制快照保留，纹理保存 RGBA8 原像素，首次 GPU 准备时转换为线性预乘 RGBA16F；未变化的资源不重复上传。
这些阶段当前仍在宿主线程串行执行，尚未启用渲染线程。ImGui 仍借用当前上下文、动态字体图集及视口后端状态，不能只复制绘制列表后并行调用第三方后端。

## 材质、Shader 与 Pipeline

MaterialData 保存后端无关参数，ShaderProgramData 保存程序契约，MaterialPrograms 保存已发布程序／布局。
MaterialRenderer 负责具体 Pipeline、参数／纹理绑定与绘制；ShaderInterface 由 SPIR-V 反射，不把 Vk 对象写入资产字段。
内置不透明材质采用 Forward PBR、方向／点／聚光灯和单方向光阴影；Skybox、IBL 与辅助线共用场景输出。

### 材质准备与寿命

SceneResolver 解析资源版本，MaterialBinding 持有独立材质值快照和当时选定的 ShaderProgramArtifact；后者发布后不再原地修改。Material 同版本复用快照，标量、向量或纹理引用变化时才生成新副本，旧世界不跟随源材质改变。
MaterialRenderer 从本帧绑定的程序引用准备各 View，编辑器显式材质候选也先固定参数；重载失败时后续绘制显示固定内置的洋红色错误材质，已提交帧保留自己的 GPU 版本。主材质通道负责视锥裁剪和材质分组，阴影按自己的可见范围处理。
各通道复用 RenderGeometry 的世界界限与场景界限。主材质按材质身份、阴影按网格排序，实际准备结果再按布局和网格分组。
材质与阴影共用槽位索引排序：保留上一帧顺序，每帧用当前输入核对，顺序失效或数量变化时重新排序；不另外维护资产失效标记。
材质在有序输入上裁剪，同一连续材质输入共用裁剪策略检查。候选队列只借用本帧物体指针，用完清空；跨帧索引不拥有物体或资源。
兼容物体按网格、材质版本和变换分组实例化；帧槽复用未变化矩阵，变化后上传。准备缓存保留实际版本，过期项按身份清理。
材质参数／descriptor 不原地覆盖在途对象，新版本替换缓存，FrameSlot 保留旧资源直到 GPU 完成。
编辑器材质手势保存原始 CPU 材质，预览准备全部 View 的绑定但不写磁盘；确认保存一次，取消时按当前 View 和布局重新准备原材质。
GPU 候选只用于一次提交，不跨帧保存回退候选；Editor 工作流协调文件和 GPU 发布。

### 场景环境与 IBL

Scene 只保存环境资产引用及开关／强度；Environment 产品包含背景、辐照、预过滤和 BRDF 数据。
Skybox 与材质 IBL 消费同一已发布版本；缺失／关闭使用有效回退绑定，背景和照明分别控制。

### 环境资产准备

CPU Worker 解码、预计算并写可重建 Artifact，任务受源大小、工作集、排队和完成预算约束。
主线程创建 GPU 候选，经 RenderAssetPublisher 发布 Registry；失败保留旧版本，旧帧继续保活原资源。
临时背景用于首次预览，不冒充完整资产就绪；App 启动准备明确等待所需产品。导出／驻留／streaming 仍按资产主线推进。

### 编译、反射与缓存边界

内置 Shader 由构建工具编译并嵌入；项目 Shader 的 CPU 编译由 `tools/shader` 及 `tools/asset` 复用，engine／app 消费产品。
输入快照包含真实字节、依赖路径和缺失 include 候选；缓存身份区分算法版本、设备无关产品与驱动缓存。
Artifact 和依赖文件分别原子写入，单文件成功不代表跨文件事务或 GPU 发布成功。

### 材质 Shader 热发布

Editor 源监控与 CPU 编译交付完整阶段候选；主线程复核请求、输入和运行版本，准备完整 GPU 程序后发布。
内置与项目 Shader 均先为所有 View 准备候选，全部成功后共同发布 Pipeline 和共享 MaterialPrograms 布局。
项目程序失败按产物或实际材质／覆盖变化决定重试；一个 View 失败时所有目标显示错误材质。隐藏 View 也参与候选准备。
项目编译失败后撤销旧 CPU 产物的注册，旧帧仍持有此前捕获的引用；内置程序失败状态由共享 MaterialPrograms 保存，目标重建不清除错误，成功发布才恢复。
旧 RenderState、材质和在途帧持有旧 Pipeline；编译 Worker 不调用 Renderer。
内置材质请求装配／发布与 ShaderReload 的文件／编译职责分开，项目程序则沿资产导入链处理。

### GPU 创建与错误

底层工厂报告 GraphicsError；资产／渲染组合层决定候选替换与旧版保留，低层不认识资产工作流。
创建或提交失败不返回空对象成功，错误保留原生类别；OOM／DeviceLost 不扩展为全设备自动恢复。

### 驱动 PipelineCache 持久化

设备匹配的驱动缓存独立于 Shader 产品缓存；文件限制大小并检查头、设备身份和校验和。
坏缓存可删除重建；OOM／DeviceLost 沿错误路径返回。磁盘写入复用公共原子写，不建立跨进程锁／合并框架。

## 两种完成与资源发布

| 完成机制 | 保护范围 |
| --- | --- |
| FrameSlot fence 与保活 owner | 绘制提交所用资源、UBO／descriptor 与 slot 复用 |
| Queue timeline／GpuCompletionPoint | 上传等提交的永久完成身份 |
| present queue idle 回退 | 无精确呈现完成能力时的旧交换链释放 |

slot 数与交换链 image 数独立；image-available 属于 slot，render-finished 属于 image。循环索引不能代替提交 serial。
提交成功才登记 serial／image 关联；失败不等待未提交 fence，也不呈现部分录制结果。
UploadManager 保活 staging、命令与目标 owner，提交完成才回收；绘制按实际资源汇总上传等待，已完成资源不重复等待。
Presentation 的等待列表与 Queue 的提交／呈现参数转换使用调用内的 PMR 临时缓冲，普通帧使用栈内存，较大批次由标准分配器扩展。
资源版本、图像状态和完成身份分别管理；仅持有 shared_ptr 不能代替同步，barrier 也不能代替队列完成。

## 有序 RenderGraph

当前图声明 imported 资源、有序 pass 和 exported usage，`compile` 生成不拥有 GPU 资源的 Plan；不改变 pass 顺序或自动分配资源。
编译按 image subresource／buffer 范围跟踪 writer、reader 和初始化，生成 RAW／WAR／WAW 与 layout 依赖。
录制前核对绑定、范围和设备，再保活资源并执行 barrier／pass／export；回调失败立即退出该帧，已录命令不回滚。
SceneRenderer 保存编译计划，每帧绑定当前 slot 目标；resize 更换 owner，旧目标由在途帧保留。
图负责场景／阴影／Bloom 的同步；最终输出 RenderPass、ImGui、WSI 与 upload completion 仍有独立职责。
资源别名规划、跨队列调度和独立渲染线程尚未实现，仍保留计划。

## HDR 与 SDR 输出

内部场景用线性 HDR，OutputPass 负责曝光、色调映射和输出编码；设备按完整格式／颜色空间组合协商 SDR 或 HDR。
App 直接呈现，Editor 离屏后经 ImGui 呈现并固定 SDR；场景输出目标与颜色模式是两个参数。
渲染比例缩放场景／Bloom，OutputPass 输出完整分辨率，游戏 UI 随后合成，UI 尺寸不随渲染比例缩小。

### Bloom

高亮提取、横纵模糊和 ping-pong 目标归 BloomPass，目标按帧槽拥有并由图声明依赖。
启停／尺寸／画质通过完整 RenderState 候选替换，失败保留旧状态；当前没有自动曝光或局部后处理区域。

## Swapchain 与关闭

Presentation 协调 slot、交换链与 dependent 的释放／重建；resize、最小化和 OutOfDate 使用现有有界恢复路径。
候选交换链先创建并验证，旧 generation 在 GPU／present 使用结束后释放；候选失败不提前破坏有效 owner。
关闭先停止 Runtime／后台需求，再等待 GPU，解除 Overlay 和纹理注册，释放宿主及渲染资源，最后销毁设备。
渲染生命周期钩子同步执行，不改为通用事件总线；析构等待不等同于 DeviceLost 恢复。

## 编辑命令与视口时序

SceneDocument 管文档与保存点，EditorSceneSession 管 Edit／Play 内容，SceneCommands／CommandHistory 管具体逆操作。
Inspector 和 Gizmo 共用属性事务；Play 修改运行副本，不写 Edit 历史。资产手势／文件工作流与场景命令分别提交。
项目文件与本地会话状态的保存语义不同：项目写入失败报告／补偿，本地状态失败警告，不回滚已成功的内容操作。

## Viewport 和拾取边界

Viewport 使用 Renderer 的离屏帧快照，不访问 SceneRenderer 的目标 owner。ImGui 纹理按 ViewId 与帧槽分别绑定，保活到对应帧完成。
等待当前帧槽完成后复用其 descriptor；视图尺寸确定后重绑本帧图像，保持 UI 已引用的 TextureID，避免连续 resize 时采样未绘制的旧目标。
逻辑窗口坐标、framebuffer 像素和场景渲染比例分别换算；CPU pick 使用同帧提取／解析的几何与实体身份。
拾取请求和结果都携带 scene_lifetime，目标尺寸或场景不匹配时丢弃请求；结果在完成帧提交后才能取回，读取一次即消费。
Viewport 在 Main 更新检查当前场景、Edit 模式和实体存活，过期结果不改变选择；空白命中正常清空选择。隐藏、取消和失败帧清理待处理拾取，渲染过程不回调 Scene／面板。
隐藏视图消费本视图的临时请求但跳过绘制，全部隐藏时才跳过世界准备；输入、UI 和运行推进遵守宿主阶段。
场景面板在 Play 中保留相机导航并禁止拾取、Gizmo 和资产拖入；游戏面板拥有项目 UI、运行控制与游戏输入。
面板绘制完成后复核游戏焦点／弹窗，焦点切到场景面板释放游戏按键；切回不重放仍按住的输入。
ViewportPanel 只处理图像展示、DPI 坐标和尺寸稳定；SceneViewportPanel 拥有编辑交互，GameViewportPanel 拥有运行输入与项目 UI。
画质、Shader 与材质预览均在所有视图准备成功后替换；已录制帧仍使用旧版本。

## 渲染诊断

诊断提供 CPU 阶段、GPU pass、负载计数、内存预算及报告，消费者读取快照，不控制 Renderer 内部生命周期。
GPU pass 与主材质计数覆盖第一个可见 View（默认游戏视图优先），该图同时录制共享方向光阴影；阴影计数属于整个世界帧。
世界准备只测量一次，提交与呈现时间覆盖整帧。
其他视图不重用同一组时间戳查询；多视图 GPU 总耗时与逐视图统计仍待扩展，不能混同当前单视图图耗时。
Presentation 将命令结束与参数准备、队列提交、呈现三段 CPU 时间交给 RenderDiagnostics，成功提交后与同帧场景图的 serial 配对；隐藏／失败帧不产生提交样本。
队列提交与呈现可能包含后端等待，不能视为纯计算；不计入渲染图录制或 GPU 图，关闭诊断时不读取分段时钟，也不增加 GPU 等待。
性能对比使用相同优化构建、设备、场景和采样范围，检查实际负载；等待、录制、模拟和 GPU 成本分别判断。
当前实现边界与后续功能分别维护；具体回归由现有测试承担，架构文档不累计逐轮完成记录。
