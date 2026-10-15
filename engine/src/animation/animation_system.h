#pragma once
#include "scene/systems/system.h"
#include "animation/animation_controller.h"
#include "scene/entity_id.h"
#include "asset/handle.h"
#include "asset/data/animation_library.h"
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
        using Libraries = std::function<std::shared_ptr<const AnimationLibrary>(AssetHandle)>;
        explicit AnimationSystem(
            Source source = {}, Controllers controllers = {}, Libraries libraries = {})
            : m_source(std::move(source)), m_controllers(std::move(controllers)),
              m_libraries(std::move(libraries)) {}
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct Selection {
            AnimationBlend sample;
            std::string state;
            Math::Vec2 parameter{0};
            float transition = 0;
        };
        struct Playback {
            AnimationPlayback frame;
            std::shared_ptr<const AnimationController> controller;
            std::string state;
            Math::Vec2 parameter{0};
            float transition_time = 0;
            float transition_duration = 0;
            double progress = 0;
            bool events_started = false;
            std::shared_ptr<const AnimationLibrary> library;
            std::vector<Math::Vec2> layer_parameters;
        };
        Result<void, Error> emit_events(Scene&, EntityId, Playback&, double from, double to);
        Result<Selection, Error> select(const AnimatorComponent&, Playback&, double delta);
        Result<void, Error> advance(Scene&, double delta);
        Source m_source;
        Controllers m_controllers;
        Libraries m_libraries;
        std::unordered_map<EntityId, Playback> m_playbacks;
    };
}
