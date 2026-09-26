#pragma once

#include "audio/audio.h"
#include "scene/entity.h"
#include "scene/systems/system.h"

#include <map>
#include <memory>
#include <vector>

namespace Comet {
    class AssetRegistry;

    // Scene 保存声音配置；System 只在运行期间拥有设备与播放实例。
    class COMET_API AudioSystem final: public System {
    public:
        explicit AudioSystem(
            const AssetRegistry& assets, AudioPlayback::Mode mode = AudioPlayback::Mode::Realtime)
            : m_assets(assets), m_mode(mode) {}

        Result<void, Error> on_start(Scene& scene) override;
        Result<void, Error> update(Scene& scene, const Context& context) override;
        void on_stop(Scene& scene) noexcept override;

    private:
        struct Entry {
            Entity entity;
            AssetHandle clip;
            uint64_t lifetime;
            std::unique_ptr<AudioPlayback::Voice> voice;
        };

        Result<void, Error> synchronize(Scene& scene);
        Result<void, Error> prepare_playback();

        const AssetRegistry& m_assets;
        AudioPlayback::Mode m_mode;
        Scene* m_scene = nullptr;
        std::unique_ptr<AudioPlayback> m_playback;
        std::map<EntityUuid, Entry> m_entries;
        std::vector<std::unique_ptr<AudioPlayback::Voice>> m_one_shots;
        bool m_device_unavailable = false;
    };
}
