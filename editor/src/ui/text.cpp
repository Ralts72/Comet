#include "ui/text.h"
#include "common/file_io.h"
#include "common/yaml.h"

#include <optional>
#include <utility>
#include <vector>

namespace CometEditor::Ui {
    namespace {
        thread_local const Translations* current_translations = nullptr;

        // 保留完整 printf 占位符，不允许翻译改变参数类型、顺序或动态宽度参数。
        std::optional<std::vector<std::string_view>> format_tokens(std::string_view text) {
            std::vector<std::string_view> tokens;
            size_t cursor = 0;
            const auto skip_digits = [&] {
                while(cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
                    ++cursor;
            };
            while((cursor = text.find('%', cursor)) != std::string_view::npos) {
                const auto start = cursor++;
                if(cursor < text.size() && text[cursor] == '%') {
                    tokens.push_back(text.substr(start, 2));
                    ++cursor;
                    continue;
                }
                while(cursor < text.size()
                      && std::string_view("-+ #0").find(text[cursor]) != std::string_view::npos)
                    ++cursor;
                if(cursor < text.size() && text[cursor] == '*')
                    ++cursor;
                else
                    skip_digits();
                if(cursor < text.size() && text[cursor] == '.') {
                    ++cursor;
                    if(cursor < text.size() && text[cursor] == '*')
                        ++cursor;
                    else
                        skip_digits();
                }
                if(text.substr(cursor, 2) == "hh" || text.substr(cursor, 2) == "ll")
                    cursor += 2;
                else if(cursor < text.size()
                        && std::string_view("hljztL").find(text[cursor]) != std::string_view::npos)
                    ++cursor;
                if(cursor == text.size()
                    || std::string_view("diouxXfFeEgGaAcsp").find(text[cursor])
                           == std::string_view::npos)
                    return std::nullopt;
                tokens.push_back(text.substr(start, ++cursor - start));
            }
            return tokens;
        }
    }

    Comet::Result<Translations> parse_translations(std::string_view yaml, std::string_view source) {
        using Result = Comet::Result<Translations>;
        const Comet::Yaml::Context context("translations", source);
        auto root = context.parse(yaml);
        if(!root)
            return Result::failure(root.error());
        if(auto valid = context.mapping(root.value(), "<root>"); !valid)
            return Result::failure(valid.error());

        Translations translations;
        for(const auto& entry : root.value()) {
            if(!Comet::Yaml::is_string(entry.first) || !Comet::Yaml::is_string(entry.second))
                return Result::failure(
                    context.error("<root>", "Translation keys and values must be strings"));
            const auto& key = entry.first.Scalar();
            const auto& value = entry.second.Scalar();
            if(key.empty() || value.empty() || key.find('\0') != std::string::npos
                || value.find('\0') != std::string::npos)
                return Result::failure(context.error(
                    key, "Translation keys and values must be nonempty and contain no NUL"));
            const auto source_tokens = format_tokens(key);
            const auto translated_tokens = format_tokens(value);
            if(!source_tokens || !translated_tokens || *source_tokens != *translated_tokens)
                return Result::failure(
                    context.error(key, "Translation format placeholders must match"));
            translations.emplace(key, value);
        }
        return Result::success(std::move(translations));
    }

    Comet::Result<Translations> load_translations(std::filesystem::path path) {
        using Result = Comet::Result<Translations>;
        if(path.empty())
            path = std::filesystem::path(COMET_EDITOR_RESOURCE_DIRECTORY) / "locales/zh-CN.yaml";
        auto contents = Comet::read_text_file(path);
        if(!contents)
            return Result::failure(contents.error());
        return parse_translations(contents.value(), path.string());
    }

    const Translations& translations() {
        static const Translations empty;
        return current_translations ? *current_translations : empty;
    }

    const char* text(const char* english) {
        const auto& active_translations = translations();
        const auto found = active_translations.find(english);
        return found == active_translations.end() ? english : found->second.c_str();
    }

    std::string label(const char* english) {
        const auto* translated = text(english);
        if(translated == english)
            return english;
        return std::string(translated) + "###" + english;
    }

    TextScope::TextScope(const Translations& translations)
        : m_previous_translations(current_translations) {
        current_translations = &translations;
    }
    TextScope::~TextScope() {
        current_translations = m_previous_translations;
    }
}
