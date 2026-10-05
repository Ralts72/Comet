# 053 外部编辑后的 Lua UTF-8 文本

## 背景与问题

052 接通“打开源码”后，外部文本编辑器可能把脚本保存成带 BOM 的 UTF-8。
Lua 的文件加载辅助接口会跳过这个标记，但 Comet 不能直接用该接口：
我们必须执行已准备好的源文件快照，不能在建立实例时重新读取磁盘。
原来的内存解析接口不处理 BOM，因此有效脚本会报 `unexpected symbol near '<\239>'`。

## 前后代码与调用链

之前组件和模块分别直接调用内存解析接口：

```cpp
luaL_loadbufferx(state, source.data(), source.size(), name, "t");
```

现在两处共用 `Script::Instance::Impl` 内的短函数：

```cpp
static int load_text(lua_State* state, std::string_view source, const char* name) {
    if(source.starts_with("\xef\xbb\xbf"))
        source.remove_prefix(3);
    return luaL_loadbufferx(state, source.data(), source.size(), name, "t");
}
```

`Script::create`、独立 `load`、项目组件及其 `require` 模块最终都走这里。
只跳过一个完整的文件开头标记，不删除换行、不改变源码名，也不放宽纯文本加载模式。
没有修改外部编辑器、Importer、文件监听、ScriptSystem 或 Runtime 的职责。

## 为什么只调整解析视图

`string_view::remove_prefix` 不改原字符串。磁盘原字节仍用于源码大小限制、准备快照、
`inputs_are_current` 和 `has_same_sources`：仅添加／移除 BOM 也会被识别为文件变化。
旧 Script 仍可从自身快照建立实例，不会混用修改后的模块；错误行号也保持原样。
若在文件读取层统一删除 BOM，就会把解析规则泄漏给其他资产，并使快照比较不再对应原文件。

## 验证

- 先增加两项测试，原实现均失败：主脚本和模块分别拒绝 BOM；修复后转绿。
- 内存创建和独立文件加载覆盖 BOM＋CRLF、声明参数、实例回调和文件未被改写。
- 模块覆盖调用栈行号、只移除 BOM 后快照失效、新旧内容比较及旧实例快照保持。
- 不完整标记、重复 BOM 和 UTF-16 仍拒绝，不默默修复任意坏编码。
- 16 项定向测试通过；Debug 全目标、app Release 构建通过。
- 完整 1078 CPU／242 UI、Shader 构建契约及模块边界通过；1 项既有平台条件跳过。
  本次增量编译未产生新警告。

## 限制与后续

源码约定仍为 UTF-8，LF／CRLF 沿 Lua 原有词法处理；不新增 UTF-16 转码、脚本解释器启动或 shebang 支持。
README 增加了实际编码约定。外部编辑器真正打开、保存和 Play 重载的桌面闭环仍保留为人工验收，
本项的自动测试不能替代它。没有新增抽象类、文件格式版本或仅供测试的生产接口。
