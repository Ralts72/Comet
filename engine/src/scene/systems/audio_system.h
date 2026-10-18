#pragma once

#include "audio/audio_service.h"
#include "scene/entity.h"
#include "scene/systems/system.h"

#include <map>

namespace Comet {
    // 仅桥接场景配置与音频服务；播放实例由服务拥有。
    class COMET_API AudioSystem final: public System {
    public:
        explicit AudioSystem(AudioService& audio) : m_audio(audio) {}

        Result<void, Error> on_start(
            Scene& scene, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene& scene, const Context& context) override;
        Result<void, Error> post_update(Scene& scene, const Context& context) override;
        void on_stop(Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct Entry {
            Entity entity;
            AssetHandle clip;
            uint64_t lifetime;
            uint64_t revision;
            AudioService::VoiceId voice;
            float fade_out = 0;
            float occlusion = 0;
            float occlusion_target = 0;
            double query_elapsed = .1;
        };

        Result<void, Error> synchronize(
            Scene& scene, PhysicsAccess* physics = nullptr, double delta_time = 0);
        AudioService& m_audio;
        std::map<EntityUuid, Entry> m_entries;
    };
}
