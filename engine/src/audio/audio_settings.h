#pragma once

#include "audio/audio_category.h"
#include "common/export.h"
#include "common/json.h"

namespace Comet {
    struct COMET_API AudioSettings {
        float master_volume = 1;
        float effects_volume = 1;
        float music_volume = 1;

        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] static Result<AudioSettings> read(
            Json::Node node, const Json::Context& context, std::string_view location);
        void write(Json::Writer& writer) const;
        bool operator==(const AudioSettings&) const = default;
    };
}
