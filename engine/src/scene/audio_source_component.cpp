#include "scene/components.h"

#include <atomic>

namespace Comet {
    namespace {
        std::atomic<uint64_t> next_audio_source_lifetime{0};

        uint64_t new_lifetime() {
            return next_audio_source_lifetime.fetch_add(1, std::memory_order_relaxed) + 1;
        }
    }

    AudioSourceComponent::AudioSourceComponent() : m_lifetime(new_lifetime()) {}

    AudioSourceComponent::AudioSourceComponent(const AudioSourceComponent& other)
        : clip(other.clip), loop(other.loop), volume(other.volume), m_lifetime(new_lifetime()) {}

    AudioSourceComponent& AudioSourceComponent::operator=(const AudioSourceComponent& other) {
        if(this != &other) {
            clip = other.clip;
            loop = other.loop;
            volume = other.volume;
            m_lifetime = new_lifetime();
        }
        return *this;
    }
}
