#pragma once

#include "common/result.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace CometEditor::Ui {
    enum class Language { English, Chinese };
    using Translations = std::map<std::string, std::string, std::less<>>;

    // 空路径读取编辑器内置的中文词表；返回值拥有文字，绘制期间保持只读。
    [[nodiscard]] Comet::Result<Translations> load_translations(std::filesystem::path path = {});
    [[nodiscard]] Comet::Result<Translations> parse_translations(std::string_view yaml);
    [[nodiscard]] Comet::Result<Language> load_language_preference(
        const std::filesystem::path& path);
    [[nodiscard]] Comet::Result<void> save_language_preference(
        const std::filesystem::path& path, Language language);

    [[nodiscard]] Language language();
    [[nodiscard]] const char* text(const char* english);
    [[nodiscard]] std::string label(const char* english);

    // 语言只作用于当前 UI 绘制，不进入引擎、资产数据或日志。
    class LanguageScope {
    public:
        explicit LanguageScope(Language language, const Translations* translations = nullptr);
        ~LanguageScope();
        LanguageScope(const LanguageScope&) = delete;
        LanguageScope& operator=(const LanguageScope&) = delete;

    private:
        Language m_previous;
        const Translations* m_previous_translations;
    };
}
