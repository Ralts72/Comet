#pragma once

#include "asset/result.h"
#include "common/file_io.h"
#include "common/json.h"

#include <initializer_list>
#include <string_view>

namespace Comet::AssetSerialization {
    template<typename Data, typename Encode>
    AssetResult<std::string> serialize_json(
        std::string_view kind, const Data& data, Encode encode) {
        try {
            const Json::Context context(kind, "<memory>");
            Json::Writer writer;
            auto result = encode(data, context, writer);
            if(!result)
                return AssetResult<std::string>::failure(result.error());
            return AssetResult<std::string>::success(std::move(writer).finish());
        } catch(const std::runtime_error& error) {
            return AssetResult<std::string>::failure(error.what());
        }
    }

    template<typename T, typename Decode>
    AssetResult<T> deserialize_json(std::string_view kind, std::string_view contents,
        std::string_view source, Decode decode) {
        const Json::Context context(kind, source);
        try {
            simdjson::dom::parser parser;
            return decode(context.parse(parser, contents), context);
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
