#include "scene/components.h"

#include "common/version.h"

namespace Comet {
    namespace {
        VersionSequence next_audio_source_lifetime;
    }

    AudioSourceComponent::AudioSourceComponent() : m_lifetime(next_audio_source_lifetime.next()) {}

    AudioSourceComponent::AudioSourceComponent(const AudioSourceComponent& other)
        : clip(other.clip), play_on_start(other.play_on_start), loop(other.loop),
          volume(other.volume), category(other.category),
          m_lifetime(next_audio_source_lifetime.next()) {}

    AudioSourceComponent& AudioSourceComponent::operator=(const AudioSourceComponent& other) {
        if(this != &other) {
            clip = other.clip;
            play_on_start = other.play_on_start;
            loop = other.loop;
            volume = other.volume;
            category = other.category;
            m_lifetime = next_audio_source_lifetime.next();
        }
        return *this;
    }
}
