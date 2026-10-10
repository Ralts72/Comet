#include "ui/resource_dependencies.h"
#include "common/file_io.h"

#include <RmlUi/Core/BaseXMLParser.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string_view>

namespace Comet::Ui {
    namespace {
        enum class ResourceType { File, Document, Style, Image };
        struct Reference {
            std::filesystem::path owner;
            std::string source;
            ResourceType type = ResourceType::File;
            bool from_root = false;
            std::optional<std::filesystem::path> directory;
        };
        struct Sources {
            std::vector<Reference> references;
            std::set<std::string> sprites;
        };

        // 只提取 RCSS 的文件引用；语法、布局和字体解码仍由 RmlUi 验证。
        struct Token {
            std::string value;
            bool literal = false;
            bool is(std::string_view text) const { return !literal && value == text; }
        };
        std::vector<Token> style_tokens(std::string_view text) {
            std::vector<Token> tokens;
            for(std::size_t i = 0; i < text.size();) {
                if(std::isspace(static_cast<unsigned char>(text[i]))) {
                    ++i;
                    continue;
                }
                if(text.substr(i, 2) == "/*") {
                    const auto end = text.find("*/", i + 2);
                    i = end == text.npos ? text.size() : end + 2;
                    continue;
                }
                if(text[i] == '\'' || text[i] == '"') {
                    const char quote = text[i++];
                    std::string value;
                    while(i < text.size() && text[i] != quote) {
                        if(text[i] == '\\' && i + 1 < text.size())
                            ++i;
                        value += text[i++];
                    }
                    if(i < text.size())
                        ++i;
                    tokens.push_back({std::move(value), true});
                    continue;
                }
                if(std::string_view("{}():;,@").find(text[i]) != text.npos) {
                    tokens.push_back({std::string(1, text[i++])});
                    continue;
                }
                const auto start = i++;
                while(i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))
                      && std::string_view("{}():;,@'\"").find(text[i]) == text.npos
                      && text.substr(i, 2) != "/*")
                    ++i;
                tokens.push_back({std::string(text.substr(start, i - start))});
            }
            return tokens;
        }

        enum class Block { Rule, Spritesheet, FontFace, Decorator };
        void style_declaration(Sources& sources, const std::filesystem::path& owner,
            const std::vector<Token>& tokens, Block block) {
            if(tokens.size() < 3 || !tokens[1].is(":"))
                return;
            const auto& property = tokens[0].value;
            if(block == Block::Spritesheet && property != "src" && property != "resolution") {
                sources.sprites.insert(property);
                return;
            }
            if(property == "src" && (block == Block::Spritesheet || block == Block::FontFace)) {
                std::string source;
                const auto append = [&] {
                    sources.references.push_back(
                        {owner, source, ResourceType::File, block == Block::FontFace});
                    source.clear();
                };
                for(std::size_t i = 2; i < tokens.size(); ++i) {
                    if(tokens[i].is(",")) {
                        append();
                        continue;
                    }
                    if(!source.empty())
                        source += ' ';
                    source += tokens[i].value;
                }
                append();
                return;
            }
            if(property.ends_with("-src")
                || (block == Block::Decorator
                    && (property == "image" || property.ends_with("-image")))) {
                if(!tokens[2].value.empty() && tokens[2].value != "auto")
                    sources.references.push_back({owner, tokens[2].value, ResourceType::Image});
                return;
            }
            if(property != "decorator")
                return;
            for(std::size_t i = 2; i + 2 < tokens.size(); ++i) {
                const auto& name = tokens[i].value;
                if(tokens[i].literal
                    || (name != "image" && name != "tiled-horizontal" && name != "tiled-vertical"
                        && name != "tiled-box")
                    || !tokens[i + 1].is("("))
                    continue;
                i += 2;
                bool first = true;
                for(; i < tokens.size() && !tokens[i].is(")"); ++i) {
                    if(tokens[i].is(",")) {
                        first = true;
                        continue;
                    }
                    if(first) {
                        if(!tokens[i].value.empty() && tokens[i].value != "auto")
                            sources.references.push_back(
                                {owner, tokens[i].value, ResourceType::Image});
                        first = false;
                    }
                }
            }
        }

        void style_sources(
            Sources& sources, const std::filesystem::path& owner, std::string_view text) {
            std::vector<Block> blocks{Block::Rule};
            std::vector<Token> statement;
            for(auto& token : style_tokens(text)) {
                if(token.is("{")) {
                    auto block = Block::Rule;
                    if(statement.size() >= 2 && statement[0].is("@")) {
                        if(statement[1].is("spritesheet"))
                            block = Block::Spritesheet;
                        else if(statement[1].is("font-face"))
                            block = Block::FontFace;
                        else if(statement[1].is("decorator"))
                            block = Block::Decorator;
                    }
                    blocks.push_back(block);
                    statement.clear();
                } else if(token.is(";") || token.is("}")) {
                    style_declaration(sources, owner, statement, blocks.back());
                    statement.clear();
                    if(token.is("}") && blocks.size() > 1)
                        blocks.pop_back();
                } else
                    statement.push_back(std::move(token));
            }
            style_declaration(sources, owner, statement, blocks.back());
        }

        class DocumentSources final: public Rml::BaseXMLParser {
        public:
            DocumentSources(
                Sources& sources, std::filesystem::path owner, std::filesystem::path document)
                : m_sources(sources), m_owner(std::move(owner)), m_document(std::move(document)) {
                RegisterCDATATag("style");
                RegisterCDATATag("script");
            }
            void HandleElementStart(
                const Rml::String& name, const Rml::XMLAttributes& attributes) override {
                const auto tag = Rml::StringUtilities::ToLower(name);
                m_style = tag == "style";
                const auto attribute = [&](const char* key) {
                    return Rml::Get<Rml::String>(attributes, key, "");
                };
                if(tag == "link") {
                    const auto type = Rml::StringUtilities::ToLower(attribute("type"));
                    if(type == "text/rcss" || type == "text/css" || type == "text/template")
                        m_sources.references.push_back({m_owner, attribute("href"),
                            type == "text/template" ? ResourceType::Document
                                                    : ResourceType::Style});
                } else if(tag == "img" || tag == "script") {
                    const auto source = attribute("src");
                    if(!source.empty()) {
                        Reference reference{m_owner, source};
                        if(tag == "img")
                            reference.directory = m_document.parent_path();
                        m_sources.references.push_back(std::move(reference));
                    }
                }
                if(const auto style = attribute("style"); !style.empty()) {
                    const auto begin = m_sources.references.size();
                    style_sources(m_sources, m_owner, style);
                    for(auto i = begin; i < m_sources.references.size(); ++i) {
                        if(m_sources.references[i].type == ResourceType::Image)
                            m_sources.references[i].directory = m_document.parent_path();
                    }
                }
            }
            void HandleElementEnd(const Rml::String& name) override {
                if(Rml::StringUtilities::ToLower(name) == "style")
                    m_style = false;
            }
            void HandleData(const Rml::String& data, Rml::XMLDataType) override {
                if(m_style)
                    style_sources(m_sources, m_owner, data);
            }

        private:
            Sources& m_sources;
            std::filesystem::path m_owner;
            std::filesystem::path m_document;
            bool m_style = false;
        };

        Result<std::filesystem::path> resolve_reference(
            const std::filesystem::path& root, const Reference& reference) {
            using Resolved = Result<std::filesystem::path>;
            const auto fail = [&](std::string reason) {
                return Resolved::failure(reference.owner.generic_string() + ": " + reason + " '"
                                         + reference.source + "'");
            };
            const std::filesystem::path source(reference.source);
            if(source.empty() || source.is_absolute()
                || reference.source.find(':') != std::string::npos)
                return fail("UI resource must be a relative project file");
            auto directory = reference.owner.parent_path();
            if(reference.from_root)
                directory.clear();
            else if(reference.directory)
                directory = *reference.directory;
            const auto relative = (directory / source).lexically_normal();
            if(relative.empty() || *relative.begin() == "..")
                return fail("UI resource is outside project assets");
            std::error_code error;
            const auto canonical = std::filesystem::canonical(root / relative, error);
            if(error)
                return fail("Cannot resolve UI resource");
            const auto resolved = canonical.lexically_relative(root);
            if(resolved.empty() || resolved.is_absolute() || *resolved.begin() == "..")
                return fail("UI resource is outside project assets");
            if(!std::filesystem::is_regular_file(canonical, error) || error)
                return fail("UI resource is not a regular file");
            const auto size = std::filesystem::file_size(canonical, error);
            if(error || size > 32 * 1024 * 1024)
                return fail("UI resource exceeds the 32 MiB file budget");
            return Resolved::success(relative);
        }
    }

    Result<std::vector<std::filesystem::path>> collect_resource_dependencies(
        const std::filesystem::path& assets, const Project::UiEntry& entry) {
        using Collected = Result<std::vector<std::filesystem::path>>;
        if(entry.document.empty() || entry.document.is_absolute()
            || entry.document.extension() != ".rml" || entry.controller.empty()
            || entry.controller.is_absolute() || !entry.controller.string().ends_with(".ui.lua"))
            return Collected::failure("Invalid project UI entry");
        for(const auto& path : {entry.document, entry.controller}) {
            if(std::ranges::any_of(path, [](const auto& part) { return part == ".."; }))
                return Collected::failure("UI entry must remain inside project assets");
        }
        std::error_code error;
        const auto root = std::filesystem::canonical(assets, error);
        if(error)
            return Collected::failure(
                "Cannot resolve project UI resource root: " + error.message());
        Sources sources;
        sources.references = {
            {"project.json", entry.document.generic_string(), ResourceType::Document},
            {"project.json", entry.controller.generic_string()}};
        std::set<std::filesystem::path> files;
        std::set<std::filesystem::path> parsed;
        std::vector<Reference> images;
        for(std::size_t i = 0; i < sources.references.size(); ++i) {
            // 解析会追加引用，避免持有可能失效的 vector 元素引用。
            const auto reference = sources.references[i];
            if(reference.type == ResourceType::Image) {
                images.push_back(reference);
                continue;
            }
            auto path = resolve_reference(root, reference);
            if(!path)
                return Collected::failure(path.error());
            files.insert(path.value());
            if(reference.type == ResourceType::File || !parsed.insert(path.value()).second)
                continue;
            auto text = read_text_file(root / path.value());
            if(!text)
                return Collected::failure(text.error());
            if(reference.type == ResourceType::Style)
                style_sources(sources, path.value(), text.value());
            else {
                Rml::StreamMemory stream(
                    reinterpret_cast<const Rml::byte*>(text.value().data()), text.value().size());
                DocumentSources parser(sources, path.value(), entry.document);
                parser.Parse(&stream);
            }
        }
        for(const auto& reference : images) {
            if(sources.sprites.contains(reference.source))
                continue;
            auto path = resolve_reference(root, reference);
            if(!path)
                return Collected::failure(path.error());
            files.insert(path.value());
        }
        std::vector<std::filesystem::path> result(files.begin(), files.end());
        std::ranges::sort(result, {}, [](const auto& path) { return path.generic_string(); });
        return Collected::success(std::move(result));
    }
}
