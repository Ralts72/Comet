# 052 从 Project 打开 Lua 源码

## 背景与前后对比

Project 已能创建组件脚本和源码模块，保存后的监听、依赖导入与运行实例重建也已接通；
但创建后必须另行去文件夹找源码，编辑闭环缺少一个直接入口。

之前右键只有改名、删除等资产操作。现在组件 Script 和 `.module.lua` 增加“打开源码”：

```cpp
if(asset.type == Comet::AssetType::Script
    && ImGui::MenuItem(Ui::label("Open Source").c_str()))
    m_pending_open_source = asset.path;
```

面板只产生 assets 相对路径请求。宿主在现有 `process_asset_requests` 消费一次：

```cpp
const auto resolved = AssetSourceOperations::resolve_script_source(database, source);
// 路径有效才交付给外部文本编辑器，结果回到原面板错误区及 Log。
if(resolved)
    opened = SystemTextEditor::open(resolved.value());
else
    opened = Result<void>::failure(resolved.error());
```

关闭菜单不改变资产选择或场景；成功也不触发 refresh、acknowledge、Undo、自动保存或资源加载。
用户实际保存文件后，已有源文件监听重新导入并发布，继续沿原有失败保留／暂停边界执行。

## 职责和失败边界

| 层次 | 本次职责 |
| --- | --- |
| ProjectPanel | 显示入口，单次请求与错误反馈；不启动进程或读取 Lua 内容 |
| AssetSourceOperations | 确认项目内 Lua 普通文件，组件仍有 Script 记录；返回绝对路径 |
| Editor 宿主 | 在原资产请求消费点连接路径检查、平台交付及结果反馈 |
| SystemTextEditor | 平台文本编辑请求，不了解项目、Scene、AssetHandle 或热重载 |

路径复用 ProjectPaths 的项目边界；不允许绝对请求、越界路径或指向项目外的符号链接。
不解析或编译 Lua，因此语法错误的源码仍能打开修复；不为模块创建 `.meta`。
这是 Editor 私有能力，Engine／Runtime／共享 ImGui 不增加桌面应用启动依赖。

## 为什么单独保留平台文件

这是第二种明确的桌面操作，但不是回收站操作：不将它塞进 SystemTrash，也不把资产工作流升级为
通用 FileSystem 或新 Manager。三个小文件是接口及平台实现，不持状态或资源缓存。

- macOS：Foundation 启动绝对路径 `/usr/bin/open`，以独立参数传 `-t` 和文件名；
  只等短命启动器返回，不带 `-W`，不等文本编辑器关闭。`-t` 选择默认文本编辑器。
- Windows：显式启动系统目录下的 `notepad.exe`，正确编码文件参数，不查询 `.lua` 的可执行关联。
- Linux：复用已有 GIO 依赖，使用 `text/plain` 默认程序和 GFile；缺少处理程序直接返回失败。

不拼接 shell 命令、不读取 `$EDITOR` 命令串，也不把 Lua 交给脚本解释器执行。
成功只表示打开请求已交付，不能推断外部编辑器已保存或新 Lua 已成功重载。

## 测试结果

- 2 个 CPU 用例：组件／模块、空格／中文路径及坏 Lua；读取不改 revision／身份；
  缺失文件、失效记录、目录、非脚本、绝对／越界及外部符号链接拒绝。
- 1 个真实无窗口 UI 用例：两类菜单都交付正确路径且只消费一次；不改选择／历史；失败显示、成功清除；
  普通图片没有源码打开请求。
- 与 051 共同工作区：Debug 全目标、app Release 构建，1076 CPU／242 UI、Shader 构建契约及模块边界通过；
  1 项既有平台条件跳过，无新增编译警告。

测试不启动用户的文本编辑器，不为注入假的进程启动器增加生产回调。
本机只编译了 macOS 平台实现；Windows／Linux 为源码审查，未冒充跨平台构建或实际程序启动验收。
054 随后补了 macOS 实际交付：同一生产函数成功打开带空格／中文名的临时文件，
文本编辑器保存后的原字节也已核对；在隔离项目中的外部保存进入了 Editor 的源码监听及错误反馈。
根 `.clang-format` 只配置 C++，格式化 `.mm` 时被工具拒绝；C++ 已格式化，Objective-C++ 按现有风格人工整理。

## 限制与后续

首版不提供自选 IDE／命令参数、源码跳行、断点或内嵌编辑器。Windows 记事本是明确的文本回退，
不是宣称使用用户默认 IDE。后续若需要配置 IDE，应在 Editor 用户偏好中独立设计，不写入项目或 Engine 配置。
完整“右键 → 外部编辑 → 保存 → Play 新实例”仍需真实桌面验收；054 仅补齐部分实际链路，
现有鼠标自动操作的坐标差异没有在引擎中绕过。
