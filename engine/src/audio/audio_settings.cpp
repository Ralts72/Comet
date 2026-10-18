#include "audio/audio_settings.h"

#include <cmath>

namespace Comet {
    Result<void> AudioSpatialSettings::validate() const {
        if(!Math::is_finite(position) || !std::isfinite(min_distance)
            || !std::isfinite(max_distance) || !std::isfinite(rolloff) || min_distance < 0
            || max_distance <= min_distance || rolloff < 0)
            return Result<void>::failure(
                "Spatial audio requires finite values, 0 <= min distance < max distance and rolloff >= 0");
        return Result<void>::success();
    }

    Result<void> AudioEffectSettings::validate() const {
        if(!std::isfinite(reverb_mix) || reverb_mix < 0 || reverb_mix > 1
            || !std::isfinite(reverb_decay) || reverb_decay < .1f || reverb_decay > 10
            || !std::isfinite(occlusion) || occlusion < 0 || occlusion > 1)
            return Result<void>::failure("Invalid audio reverb or occlusion settings");
        return Result<void>::success();
    }

    Result<void> AudioSettings::validate() const {
        for(const auto volume : {master_volume, effects_volume, music_volume}) {
            if(!std::isfinite(volume) || volume < 0 || volume > 1)
                return Result<void>::failure("Audio volumes must be finite values in [0, 1]");
        }
        return Result<void>::success();
    }

    Result<AudioSettings> AudioSettings::read(
        Json::Node node, const Json::Context& context, std::string_view location) {
        using Read = Result<AudioSettings>;
        if(auto valid = context.validate_keys(
               node, {"master_volume", "effects_volume", "music_volume"}, location);
            !valid)
            return Read::failure(valid.error());
        AudioSettings settings;
        for(const auto& [field, target] : {std::pair{"master_volume", &settings.master_volume},
                std::pair{"effects_volume", &settings.effects_volume},
                std::pair{"music_volume", &settings.music_volume}}) {
            auto value = context.read_field<float>(node, field, "a volume in [0, 1]", location);
            if(!value)
                return Read::failure(value.error());
            *target = value.value();
        }
        if(auto valid = settings.validate(); !valid)
            return Read::failure(context.error(location, valid.error()));
        return Read::success(settings);
    }

    void AudioSettings::write(Json::Writer& writer) const {
        writer.begin_object();
        writer.field("master_volume", master_volume);
        writer.field("effects_volume", effects_volume);
        writer.field("music_volume", music_volume);
        writer.end_object();
    }
}
