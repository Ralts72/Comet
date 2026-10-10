#pragma once

#include "common/json.h"
#include "core/geometry.h"

#include <cstdint>

namespace Comet {
    struct COMET_API QualitySettings {
        std::uint32_t msaa_samples = 4;
        float max_anisotropy = 8;
        float render_scale = 1;

        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] Math::Vec2u scene_size(Math::Vec2u output) const;
        [[nodiscard]] static Result<QualitySettings> read(
            Json::Node node, const Json::Context& context, std::string_view location);
        void write(Json::Writer& writer) const;
        bool operator==(const QualitySettings&) const = default;
    };
}
