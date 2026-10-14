#pragma once
#include "scene/systems/system.h"
#include "animation/animation_controller.h"
#include "scene/entity_id.h"
#include "asset/handle.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
namespace Comet {
    struct AnimatorComponent;
    class COMET_API AnimationSystem final: public System {
    public:
        using Source = std::function<std::shared_ptr<const AnimationData>(AssetHandle)>;
        using Controllers = std::function<std::shared_ptr<const AnimationController>(AssetHandle)>;
        explicit AnimationSystem(Source source = {}, Controllers controllers = {})
            : m_source(std::move(source)), m_controllers(std::move(controllers)) {}
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct Selection {
            AnimationBlend sample;
            std::string state;
            float parameter = 0;
            float transition = 0;
        };
        struct Playback {
            AnimationPlayback frame;
            std::shared_ptr<const AnimationController> controller;
            std::string state;
            float parameter = 0;
            float transition_time = 0;
            float transition_duration = 0;
            double progress = 0;
        };
        Result<Selection, Error> select(const AnimatorComponent&, Playback&, double delta);
        Result<void, Error> advance(Scene&, double delta);
        Source m_source;
        Controllers m_controllers;
        std::unordered_map<EntityId, Playback> m_playbacks;
    };
}
