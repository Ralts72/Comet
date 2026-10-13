#pragma once
#include "scene/systems/system.h"
#include "animation/animation_data.h"
#include "scene/entity_id.h"
#include <unordered_map>
namespace Comet {
    class COMET_API AnimationSystem final: public System {
    public:
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct Playback {
            AnimationPlayback frame;
            float transition_time = 0;
        };
        Result<void, Error> advance(Scene&, double delta, const RuntimeServices&);
        std::unordered_map<EntityId, Playback> m_playbacks;
    };
}
