#pragma once

#include "audio/audio_category.h"
#include "common/export.h"
#include "common/json.h"
#include "core/math_utils.h"

namespace Comet {
    struct COMET_API AudioSpatialSettings {
        bool enabled = false;
        Math::Vec3 position{0};
        float min_distance = 1;
        float max_distance = 30;
        float rolloff = 1;

        [[nodiscard]] Result<void> validate() const;
    };

    struct COMET_API AudioEffectSettings {
        float reverb_mix = 0;
        float reverb_decay = 1.2f; // RT60，秒。
        float occlusion = 0;       // 0～1，同步控制低通与直达声衰减。
        [[nodiscard]] Result<void> validate() const;
    };

    struct AudioListenerPose {
        Math::Vec3 position{0};
        Math::Vec3 forward{0, 0, -1};
        Math::Vec3 up{0, 1, 0};
    };

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
