#include "scene/scene_runtime.h"
#include "scene/scene.h"
#include "common/scope_exit.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Comet {
    SceneRuntime::~SceneRuntime() {
        stop_systems();
    }

    Result<void, Error> SceneRuntime::set_settings(const Settings settings) {
        if(m_executing || is_active())
            return Result<void, Error>::failure({"Stop the scene runtime before configuring it"});
        if(!std::isfinite(settings.fixed_delta) || settings.fixed_delta < 1e-6
            || settings.fixed_delta > 1 || !std::isfinite(settings.max_frame_delta)
            || settings.max_frame_delta <= 0 || settings.max_frame_delta > 1
            || settings.max_fixed_steps == 0 || settings.max_fixed_steps > 1024)
            return Result<void, Error>::failure({"Invalid scene runtime timing settings"});
        m_settings = settings;
        return Result<void, Error>::success();
    }

    Result<void, Error> SceneRuntime::add_system(std::unique_ptr<System> system) {
        if(m_executing || is_active())
            return Result<void, Error>::failure({"Stop the scene runtime before adding systems"});
        if(!system)
            return Result<void, Error>::failure({"Cannot add a null system"});
        m_systems.push_back(std::move(system));
        return Result<void, Error>::success();
    }

    Result<void, Error> SceneRuntime::set_input_actions(InputActions actions) {
        if(m_executing || is_active())
            return Result<void, Error>::failure({"Stop the scene runtime before rebinding input"});
        m_input.configure(std::move(actions));
        return Result<void, Error>::success();
    }

    Result<void, Error> SceneRuntime::clear_systems() {
        if(m_executing || is_active())
            return Result<void, Error>::failure({"Stop the scene runtime before clearing systems"});
        m_systems.clear();
        return Result<void, Error>::success();
    }

    Result<void, Error> SceneRuntime::start(Scene& scene) {
        if(m_executing || is_active())
            return Result<void, Error>::failure({"Scene runtime is already active or executing"});
        m_scene = &scene;
        m_state = State::Running;
        m_step_pending = false;
        m_timing = {};
        m_accumulator = 0;
        m_input.reset();
        scene.begin_entity_requests();
        m_executing = true;
        ScopeExit cleanup([&] { stop_systems(); });
        while(m_started < m_systems.size()) {
            if(auto result = m_systems[m_started++]->on_start(scene); !result)
                return result;
        }
        if(!scene.commit_entity_requests())
            return Result<void, Error>::failure({"Cannot commit startup entity requests"});
        cleanup.release();
        m_executing = false;
        return Result<void, Error>::success();
    }

    void SceneRuntime::stop_systems() noexcept {
        m_executing = true;
        while(m_started > 0)
            m_systems[--m_started]->on_stop(*m_scene);
        if(m_scene)
            m_scene->end_entity_requests();
        m_scene = nullptr;
        m_state = State::Running;
        m_step_pending = false;
        m_accumulator = 0;
        m_input.reset();
        m_executing = false;
    }

    Result<void, Error> SceneRuntime::stop() {
        if(m_executing)
            return Result<void, Error>::failure({"Cannot stop an executing scene runtime"});
        stop_systems();
        return Result<void, Error>::success();
    }

    Result<void, Error> SceneRuntime::set_state(State state) {
        if(m_executing || !is_active())
            return Result<void, Error>::failure({"Runtime state requires an idle active scene"});
        if(state != State::Running && state != State::Paused)
            return Result<void, Error>::failure({"Invalid scene runtime state"});
        if(m_state == state)
            return Result<void, Error>::success();
        m_state = state;
        m_step_pending = false;
        m_accumulator = 0;
        m_input.rebase();
        m_timing.fixed_steps = 0;
        m_timing.interpolation = 0;
        m_timing.dropped_time = 0;
        return Result<void, Error>::success();
    }

    Result<void, Error> SceneRuntime::discard_input() {
        if(m_executing)
            return Result<void, Error>::failure({"Cannot discard input during runtime callbacks"});
        if(is_active())
            m_input.discard();
        return Result<void, Error>::success();
    }

    Result<void, Error> SceneRuntime::request_step() {
        if(m_executing || !is_active() || m_state != State::Paused)
            return Result<void, Error>::failure({"Single step requires an idle paused scene"});
        m_step_pending = true;
        return Result<void, Error>::success();
    }

    Result<void, Error> SceneRuntime::advance(double delta_time, const Input::Frame* input) {
        if(m_executing)
            return Result<void, Error>::failure({"Cannot reenter an executing scene runtime"});
        if(!std::isfinite(delta_time) || delta_time < 0)
            return Result<void, Error>::failure(
                {"Scene runtime delta must be finite and nonnegative"});
        if(!is_active())
            return Result<void, Error>::success();
        m_input.prepare(input, m_state == State::Paused);
        const bool stepping = std::exchange(m_step_pending, false);
        m_timing.fixed_steps = 0;
        m_timing.dropped_time = 0;
        if(m_state == State::Paused && !stepping)
            return Result<void, Error>::success();

        double delta = m_settings.fixed_delta;
        if(!stepping) {
            delta = std::min(delta_time, m_settings.max_frame_delta);
            m_timing.dropped_time = delta_time - delta;
        }
        m_accumulator += delta;
        const double step = m_settings.fixed_delta;
        const double epsilon = step * 1e-9;
        m_executing = true;
        // 更新失败可能已写入部分组件；停止并上报，不自动重试。
        ScopeExit cleanup([&] { stop_systems(); });
        while(
            m_accumulator + epsilon >= step && m_timing.fixed_steps < m_settings.max_fixed_steps) {
            ++m_timing.fixed_index;
            ++m_timing.fixed_steps;
            m_timing.fixed_time = m_timing.fixed_index * step;
            const System::Context context{
                step, m_timing.fixed_time, m_timing.fixed_index, m_input.consume_fixed()};
            for(auto& system : m_systems)
                if(auto result = system->fixed_update(*m_scene, context); !result)
                    return result;
            if(!m_scene->commit_entity_requests())
                return Result<void, Error>::failure({"Cannot commit fixed-step entity requests"});
            m_accumulator = std::max(0.0, m_accumulator - step);
        }
        const double dropped = std::floor((m_accumulator + epsilon) / step) * step;
        m_timing.dropped_time += dropped;
        m_accumulator = std::max(0.0, m_accumulator - dropped);
        m_timing.interpolation = m_accumulator / step;
        m_timing.total_time += delta;
        ++m_timing.frame_index;
        const System::Context context{
            delta, m_timing.total_time, m_timing.frame_index, m_input.update()};
        for(auto& system : m_systems)
            if(auto result = system->update(*m_scene, context); !result)
                return result;
        if(!m_scene->commit_entity_requests())
            return Result<void, Error>::failure({"Cannot commit update entity requests"});
        cleanup.release();
        m_executing = false;
        return Result<void, Error>::success();
    }
}
