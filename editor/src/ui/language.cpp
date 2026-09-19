#include "ui/language.h"
#include "common/file_io.h"

#include <optional>
#include <utility>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace CometEditor::Ui {
    namespace {
        thread_local Language current = Language::English;
        thread_local const Translations* current_translations = nullptr;

        bool is_string(const YAML::Node& node) {
            if(!node.IsScalar())
                return false;
            if(node.Tag() == "!" || node.Tag() == "tag:yaml.org,2002:str")
                return true;
            if(node.Tag() != "?")
                return false;
            bool boolean = false;
            double number = 0;
            return !YAML::convert<bool>::decode(node, boolean)
                   && !YAML::convert<double>::decode(node, number);
        }

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

    Comet::Result<Translations> parse_translations(std::string_view yaml) {
        using Result = Comet::Result<Translations>;
        YAML::Node root;
        try {
            root = YAML::Load(std::string(yaml));
        } catch(const YAML::Exception& error) {
            return Result::failure(error.what());
        }
        if(!root.IsMap())
            return Result::failure("Expected a translation mapping");

        Translations translations;
        for(const auto& entry : root) {
            if(!is_string(entry.first) || !is_string(entry.second))
                return Result::failure("Translation keys and values must be strings");
            const auto& key = entry.first.Scalar();
            const auto& value = entry.second.Scalar();
            if(key.empty() || value.empty() || key.find('\0') != std::string::npos
                || value.find('\0') != std::string::npos)
                return Result::failure(
                    "Translation keys and values must be nonempty and contain no NUL");
            const auto source_tokens = format_tokens(key);
            const auto translated_tokens = format_tokens(value);
            if(!source_tokens || !translated_tokens || *source_tokens != *translated_tokens)
                return Result::failure("Translation format placeholders must match: " + key);
            if(!translations.emplace(key, value).second)
                return Result::failure("Duplicate translation key: " + key);
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
        auto translations = parse_translations(contents.value());
        if(!translations)
            return Result::failure(
                "Invalid translation file '" + path.string() + "': " + translations.error());
        return translations;
    }

    Language language() {
        return current;
    }

    const char* text(const char* english) {
        if(current == Language::Chinese && current_translations)
            if(const auto found = current_translations->find(english);
                found != current_translations->end())
                return found->second.c_str();
        return english;
    }

    std::string label(const char* english) {
        const auto* translated = text(english);
        if(translated == english)
            return english;
        return std::string(translated) + "###" + english;
    }

    LanguageScope::LanguageScope(Language language, const Translations* translations)
        : m_previous(current), m_previous_translations(current_translations) {
        current = language;
        if(translations)
            current_translations = translations;
    }
    LanguageScope::~LanguageScope() {
        current = m_previous;
        current_translations = m_previous_translations;
    }
}
