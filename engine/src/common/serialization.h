#pragma once

#include "common/file_io.h"

#include <string>
#include <string_view>
#include <utility>

namespace Comet::Serialization {
    class Context {
    public:
        Context(std::string_view kind, std::string_view source) : m_kind(kind), m_source(source) {}

        std::string error(std::string_view location, std::string_view detail) const {
            return "Invalid " + std::string(m_kind) + " '" + std::string(m_source) + "' at '"
                   + std::string(location) + "': " + std::string(detail);
        }

    private:
        std::string_view m_kind;
        std::string_view m_source;
    };

    template<typename Serializer, typename Data>
    Result<void> save(
        const Serializer& serializer, const Data& data, const std::filesystem::path& path) {
        auto contents = serializer.serialize(data);
        if(!contents)
            return Result<void>::failure(contents.error());
        return write_text_file_atomic(path, contents.value());
    }

    template<typename Serializer>
    auto load(const Serializer& serializer, const std::filesystem::path& path)
        -> decltype(serializer.deserialize(std::string_view{}, std::string_view{})) {
        using Loaded = decltype(serializer.deserialize(std::string_view{}, std::string_view{}));
        auto contents = read_text_file(path);
        if(!contents)
            return Loaded::failure(contents.error());
        return serializer.deserialize(contents.value(), path.string());
    }
}
