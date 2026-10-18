#include "scene/component_registration.h"
#include "scene/component_changes.h"

namespace Comet {
    void ComponentRegistration::audio(ComponentRegistry& registry) {
        add(registry,
            describe_component<AudioListenerComponent>("audio_listener", "Audio Listener")
                .field(COMET_FIELD(AudioListenerComponent, enabled), "Enabled", {.required = false})
                .build());

        add(registry,
            describe_component<AudioSourceComponent>("audio_source", "Audio Source")
                .field(COMET_FIELD(AudioSourceComponent, clip), "Audio Clip",
                    {.asset_type = AssetType::Audio})
                .field(COMET_FIELD(AudioSourceComponent, play_on_start), "Play On Start")
                .field(COMET_FIELD(AudioSourceComponent, loop), "Loop")
                .enum_field(COMET_FIELD(AudioSourceComponent, category), "Audio Category",
                    {{AudioCategory::Effects, {"effects", "Sound Effects"}},
                        {AudioCategory::Music, {"music", "Music"}}},
                    {.required = false})
                .field(COMET_FIELD(AudioSourceComponent, volume), "Volume",
                    {.numeric = {.speed = 0.01f,
                         .minimum = 0.0f,
                         .maximum = 1.0f,
                         .enforce_bounds = true}})
                .field(COMET_FIELD(AudioSourceComponent, spatial), "Spatial Audio",
                    {.required = false})
                .field(COMET_FIELD(AudioSourceComponent, min_distance), "Min Distance",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(AudioSourceComponent, max_distance), "Max Distance",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0.01f, .enforce_bounds = true}})
                .field(COMET_FIELD(AudioSourceComponent, rolloff), "Rolloff",
                    {.required = false,
                        .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(AudioSourceComponent, fade_in), "淡入时间（秒）",
                    {.required = false,
                        .numeric = {.speed = .01f,
                            .minimum = 0.0f,
                            .maximum = 60.0f,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(AudioSourceComponent, fade_out), "淡出时间（秒）",
                    {.required = false,
                        .numeric = {.speed = .01f,
                            .minimum = 0.0f,
                            .maximum = 60.0f,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(AudioSourceComponent, reverb_mix), "混响比例",
                    {.required = false,
                        .numeric = {.speed = .01f,
                            .minimum = 0.0f,
                            .maximum = 1.0f,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(AudioSourceComponent, reverb_decay), "混响衰减（秒）",
                    {.required = false,
                        .numeric = {.speed = .01f,
                            .minimum = 0.1f,
                            .maximum = 10.0f,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(AudioSourceComponent, occlusion_strength), "遮挡强度",
                    {.required = false,
                        .numeric = {.speed = .01f,
                            .minimum = 0.0f,
                            .maximum = 1.0f,
                            .enforce_bounds = true}})
                .field(
                    COMET_FIELD(AudioSourceComponent, occlusion), "几何遮挡", {.required = false})
                .field(COMET_FIELD(AudioSourceComponent, occlusion_layers), "遮挡碰撞层",
                    {.required = false, .bitmask = true})
                .build());
    }
}
