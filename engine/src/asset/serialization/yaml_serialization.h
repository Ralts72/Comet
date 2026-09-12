#pragma once

#include "asset/result.h"
#include "common/file_io.h"
#include "common/yaml_utils.h"

#include <initializer_list>
#include <string_view>

namespace Comet::AssetSerialization {
    class YamlContext final {
    private:
        auto error_factory() const {
            return [this](std::string_view, std::string_view location,
                       const std::string& detail) {
                return std::runtime_error(error(location, detail));
            };
        }

    public:
        YamlContext(std::string_view kind, std::string_view source)
            : m_kind(kind), m_source(source) {}

        std::string error(std::string_view location, std::string_view detail) const;
        void require_map(const YAML::Node& node, std::string_view location) const;
        void validate_keys(const YAML::Node& node,
            std::initializer_list<std::string_view> allowed,
            std::string_view location = "<root>") const;
        YAML::Node required_child(const YAML::Node& node, std::string_view key,
            std::string_view location = "<root>") const;

        template<typename T>
        T read_scalar(const YAML::Node& node, std::string_view location,
            std::string_view expected) const {
            return Yaml::read_scalar<T>(
                node, m_source, location, expected, error_factory());
        }

    private:
        std::string_view m_kind;
        std::string_view m_source;
    };

    template<typename Data, typename Encode>
    AssetResult<std::string> serialize_yaml(
        std::string_view kind, const Data& data, Encode encode) {
        try {
            const YamlContext context(kind, "<memory>");
            auto root = encode(data, context);
            if(!root)
                return AssetResult<std::string>::failure(root.error());
            YAML::Emitter emitter;
            emitter << root.value();
            if(!emitter.good())
                return AssetResult<std::string>::failure("Failed to serialize "
                                                         + std::string(kind) + ": "
                                                         + emitter.GetLastError());
            return AssetResult<std::string>::success(std::string(emitter.c_str()) + '\n');
        } catch(const std::runtime_error& error) {
            return AssetResult<std::string>::failure(error.what());
        }
    }

    template<typename T, typename Decode>
    AssetResult<T> deserialize_yaml(std::string_view kind, std::string_view contents,
        std::string_view source, Decode decode) {
        const YamlContext context(kind, source);
        try {
            return decode(YAML::Load(std::string(contents)), context);
        } catch(const YAML::Exception& error) {
            return AssetResult<T>::failure(context.error("<yaml>", error.what()));
        } catch(const std::runtime_error& error) {
            return AssetResult<T>::failure(error.what());
        }
    }

    template<typename Serializer, typename Data>
    AssetResult<void> save(const Serializer& serializer, const Data& data,
        const std::filesystem::path& path) {
        auto contents = serializer.serialize(data);
        if(!contents)
            return AssetResult<void>::failure(contents.error());
        try {
            write_text_file_atomic(path, contents.value());
            return AssetResult<void>::success();
        } catch(const std::runtime_error& error) {
            return AssetResult<void>::failure(error.what());
        }
    }

    template<typename Serializer>
    auto load(const Serializer& serializer, const std::filesystem::path& path)
        -> decltype(serializer.deserialize(std::string_view{}, std::string_view{})) {
        using Result =
            decltype(serializer.deserialize(std::string_view{}, std::string_view{}));
        try {
            return serializer.deserialize(read_text_file(path), path.string());
        } catch(const std::runtime_error& error) {
            return Result::failure(error.what());
        }
    }
}
