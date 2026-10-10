#pragma once

#include "common/result.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace CometEditor::Ui {
    using Translations = std::map<std::string, std::string, std::less<>>;

    // 空路径读取编辑器内置的中文词表；返回值拥有文字，绘制期间保持只读。
    [[nodiscard]] Comet::Result<Translations> load_translations(std::filesystem::path path = {});
    [[nodiscard]] Comet::Result<Translations> parse_translations(
        std::string_view yaml, std::string_view source = "<memory>");
    // 借用当前绘制作用域的中文词表；未设置词表时返回空表。
    [[nodiscard]] const Translations& translations();
    [[nodiscard]] const char* text(const char* english);
    [[nodiscard]] std::string label(const char* english);

    // 借用词表只作用于当前 UI 绘制，不进入引擎、资产数据或日志。
    class TextScope {
    public:
        explicit TextScope(const Translations& translations);
        ~TextScope();
        TextScope(const TextScope&) = delete;
        TextScope& operator=(const TextScope&) = delete;

    private:
        const Translations* m_previous_translations;
    };
}
