#pragma once

#include "common/result.h"
#include "common/file_io.h"
#include "common/json.h"

#include <string_view>

namespace Comet::AssetSerialization {
    template<typename Data, typename Encode>
    Result<std::string> serialize_json(std::string_view kind, const Data& data, Encode encode) {
        const Json::Context context(kind, "<memory>");
        Json::Writer writer;
        auto result = encode(data, context, writer);
        if(!result)
            return Result<std::string>::failure(result.error());
        return std::move(writer).finish();
    }

    template<typename T, typename Decode>
    Result<T> deserialize_json(
        std::string_view kind, std::string_view contents, std::string_view source, Decode decode) {
        const Json::Context context(kind, source);
        simdjson::dom::parser parser;
        auto root = context.parse(parser, contents);
        if(!root)
            return Result<T>::failure(root.error());
        return decode(root.value(), context);
    }

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
        using LoadResult = decltype(serializer.deserialize(std::string_view{}, std::string_view{}));
        auto contents = read_text_file(path);
        if(!contents)
            return LoadResult::failure(contents.error());
        return serializer.deserialize(contents.value(), path.string());
    }
}
