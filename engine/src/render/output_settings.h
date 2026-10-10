#pragma once

#include "common/json.h"
#include "graphics/enums.h"

namespace Comet {
    struct COMET_API OutputSettings {
        OutputMode mode = OutputMode::Sdr;
        float hdr_headroom = 4;
        // 相对于系统合成器白色的倍率；不是固定 nits，也不是场景曝光。
        float hdr_white_level = 1;

        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] static Result<OutputMode> parse_mode(std::string_view name);
        [[nodiscard]] static std::string_view mode_name(OutputMode mode);
        [[nodiscard]] static Result<OutputSettings> read(
            Json::Node node, const Json::Context& context, std::string_view location);
        void write(Json::Writer& writer) const;
        bool operator==(const OutputSettings&) const = default;
    };
}
