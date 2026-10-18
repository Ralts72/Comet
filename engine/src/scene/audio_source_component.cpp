#include "scene/components.h"

#include "common/version.h"

namespace Comet {
    namespace {
        VersionSequence next_audio_source_lifetime;
    }

    AudioSourceComponent::AudioSourceComponent() : m_lifetime(next_audio_source_lifetime.next()) {}

    AudioSourceComponent::AudioSourceComponent(const AudioSourceComponent& other)
        : clip(other.clip), play_on_start(other.play_on_start), loop(other.loop),
          volume(other.volume), category(other.category), spatial(other.spatial),
          min_distance(other.min_distance), max_distance(other.max_distance),
          rolloff(other.rolloff), fade_in(other.fade_in), fade_out(other.fade_out),
          reverb_mix(other.reverb_mix), reverb_decay(other.reverb_decay),
          occlusion(other.occlusion), occlusion_strength(other.occlusion_strength),
          occlusion_layers(other.occlusion_layers), m_lifetime(next_audio_source_lifetime.next()) {}

    AudioSourceComponent& AudioSourceComponent::operator=(const AudioSourceComponent& other) {
        if(this != &other) {
            clip = other.clip;
            play_on_start = other.play_on_start;
            loop = other.loop;
            volume = other.volume;
            category = other.category;
            spatial = other.spatial;
            min_distance = other.min_distance;
            max_distance = other.max_distance;
            rolloff = other.rolloff;
            fade_in = other.fade_in;
            fade_out = other.fade_out;
            reverb_mix = other.reverb_mix;
            reverb_decay = other.reverb_decay;
            occlusion = other.occlusion;
            occlusion_strength = other.occlusion_strength;
            occlusion_layers = other.occlusion_layers;
            m_lifetime = next_audio_source_lifetime.next();
        }
        return *this;
    }
}
