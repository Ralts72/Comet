#pragma once
#include "scene/scene.h"
#include "scene/systems/system.h"

#include <memory>
#include <utility>

namespace Comet::Tests {
    struct RuntimeCalls {
        int starts = 0;
        int fixed_updates = 0;
        int updates = 0;
        int stops = 0;
        bool input_focused = false;
        bool fail_update = false;
        Scene* started_scene = nullptr;
        Scene* stopped_scene = nullptr;
    };
    class SceneMotionSystem final: public System {
    public:
        explicit SceneMotionSystem(std::shared_ptr<RuntimeCalls> calls)
            : m_calls(std::move(calls)) {}
        Result<void, Error> on_start(Scene& scene) override {
            ++m_calls->starts;
            m_calls->started_scene = &scene;
            return Result<void, Error>::success();
        }
        Result<void, Error> update(Scene& scene, const Context& context) override {
            ++m_calls->updates;
            m_calls->input_focused = context.input.focused;
            if(auto object = scene.find_entity(EntityId(2)))
                object.get_component<TransformComponent>().translation.x = 0;
            if(m_calls->fail_update)
                return Result<void, Error>::failure({"runtime update failed"});
            return Result<void, Error>::success();
        }
        Result<void, Error> fixed_update(Scene&, const Context&) override {
            ++m_calls->fixed_updates;
            return Result<void, Error>::success();
        }
        void on_stop(Scene& scene) noexcept override {
            ++m_calls->stops;
            m_calls->stopped_scene = &scene;
        }

    private:
        std::shared_ptr<RuntimeCalls> m_calls;
    };
}
