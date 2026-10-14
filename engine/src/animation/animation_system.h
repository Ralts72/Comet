#pragma once
#include "scene/systems/system.h"
#include "animation/animation_data.h"
#include "scene/entity_id.h"
#include "asset/handle.h"
#include <functional>
#include <unordered_map>
namespace Comet {
    struct AnimatorComponent;
    class COMET_API AnimationSystem final: public System {
    public:
        using Source = std::function<std::shared_ptr<const AnimationData>(AssetHandle)>;
        explicit AnimationSystem(Source source = {}) : m_source(std::move(source)) {}
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        enum class Mode { Clip, Ground, Crouch, Jump, Fall };
        struct Selection {
            Mode mode = Mode::Clip;
            AnimationBlend sample;
            float movement_speed = 0;
        };
        struct Playback {
            AnimationPlayback frame;
            float transition_time = 0;
            Mode mode = Mode::Clip;
            float movement_speed = 0;
        };
        static Result<Selection, Error> select(Scene&, const AnimatorComponent&,
            const RuntimeServices&, const Playback&, double delta);
        Result<void, Error> advance(Scene&, double delta, const RuntimeServices&);
        Source m_source;
        std::unordered_map<EntityId, Playback> m_playbacks;
    };
}
