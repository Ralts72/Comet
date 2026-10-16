#pragma once

#include "common/json.h"
#include "common/parameters.h"

namespace Comet::Json {
    // UUID 的 JSON 表达由使用方决定；公共编解码不理解实体引用。
    using ParameterReferenceReader = Result<Uuid> (*)(Node, const Context&, std::string_view);
    using ParameterReferenceWriter = void (*)(Writer&, Uuid);

    [[nodiscard]] COMET_API Result<ParameterMap> read_parameters(Node node, const Context& context,
        std::string_view location, ParameterReferenceReader read_reference = nullptr);
    [[nodiscard]] COMET_API Result<void> write_parameters(Writer& writer,
        const ParameterMap& parameters, const Context& context, std::string_view location,
        ParameterReferenceWriter write_reference = nullptr);
}
