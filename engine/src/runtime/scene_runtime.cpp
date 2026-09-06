#include "scene_runtime.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Comet {
    namespace {
        void clear_edges(Input::Frame& frame) {
            const auto clear = [](auto& buttons) {
                for(auto& button : buttons) {
                    button.pressed = false;
                    button.released = false;
                }
            };
            clear(frame.keys);
            clear(frame.mouse_buttons);
            for(auto& pad : frame.gamepads)
                clear(pad.buttons);
            frame.cursor_delta = {};
            frame.scroll = {};
        }

        void merge_input(Input::Frame& pending, const Input::Frame& latest) {
            auto merged = latest;
            const auto merge = [](auto& target, const auto& previous, bool accept_press) {
                for(size_t i = 0; i < target.size(); ++i) {
                    if(accept_press)
                        target[i].pressed |= previous[i].pressed;
                    target[i].released |= previous[i].released;
                }
            };
            merge(merged.keys, pending.keys, latest.focused);
            merge(merged.mouse_buttons, pending.mouse_buttons, latest.focused);
            for(size_t i = 0; i < merged.gamepads.size(); ++i)
                merge(merged.gamepads[i].buttons, pending.gamepads[i].buttons,
                    latest.focused && merged.gamepads[i].connected);
            if(latest.focused) {
                merged.cursor_delta += pending.cursor_delta;
                merged.scroll += pending.scroll;
            }
            pending = merged;
        }
    }

    SceneRuntime::SceneRuntime() : SceneRuntime(Settings{}) {}

    SceneRuntime::SceneRuntime(Settings settings) : m_settings(settings) {
        if(!std::isfinite(settings.fixed_delta) || settings.fixed_delta < 1e-6
            || settings.fixed_delta > 1 || !std::isfinite(settings.max_frame_delta)
            || settings.max_frame_delta <= 0 || settings.max_frame_delta > 1
            || settings.max_fixed_steps == 0 || settings.max_fixed_steps > 1024)
            throw std::invalid_argument("Invalid scene runtime timing settings");
    }

    SceneRuntime::~SceneRuntime() {
        stop_systems();
    }

    void SceneRuntime::require_idle() const {
        if(m_executing)
            throw std::logic_error("Cannot reenter or change an executing scene runtime");
    }

    void SceneRuntime::add_system(std::unique_ptr<System> system) {
        require_idle();
        if(is_active())
            throw std::logic_error("Stop the scene runtime before adding systems");
        if(!system)
            throw std::invalid_argument("Cannot add a null system");
        m_systems.push_back(std::move(system));
    }

    void SceneRuntime::clear_systems() {
        require_idle();
        if(is_active())
            throw std::logic_error("Stop the scene runtime before clearing systems");
        m_systems.clear();
    }

    void SceneRuntime::start(Scene& scene) {
        require_idle();
        if(is_active())
            throw std::logic_error("Scene runtime is already active");
        m_scene = &scene;
        m_timing = {};
        m_accumulator = 0;
        m_fixed_input = {};
        m_input_serial.reset();
        m_executing = true;
        try {
            while(m_started < m_systems.size()) {
                auto& system = m_systems[m_started++];
                system->on_start(scene);
            }
            m_executing = false;
        } catch(...) {
            stop_systems();
            throw;
        }
    }

    void SceneRuntime::stop_systems() noexcept {
        m_executing = true;
        while(m_started > 0)
            m_systems[--m_started]->on_stop(*m_scene);
        m_scene = nullptr;
        m_accumulator = 0;
        m_fixed_input = {};
        m_input_serial.reset();
        m_executing = false;
    }

    void SceneRuntime::stop() {
        require_idle();
        stop_systems();
    }

    void SceneRuntime::advance(double delta_time, const Input::Frame& input) {
        require_idle();
        if(!is_active())
            return;
        if(!std::isfinite(delta_time) || delta_time < 0)
            throw std::invalid_argument(
                "Scene runtime delta must be finite and nonnegative");
        if(m_input_serial && input.serial < *m_input_serial)
            throw std::invalid_argument("Input serial moved backwards");
        auto frame_input = input;
        if(m_input_serial && input.serial == *m_input_serial)
            clear_edges(frame_input);
        else {
            merge_input(m_fixed_input, input);
            m_input_serial = input.serial;
        }

        const double delta = std::min(delta_time, m_settings.max_frame_delta);
        m_timing.dropped_time = delta_time - delta;
        m_timing.fixed_steps = 0;
        m_accumulator += delta;
        const double step = m_settings.fixed_delta;
        const double epsilon = step * 1e-9;
        m_executing = true;
        try {
            while(m_accumulator + epsilon >= step
                  && m_timing.fixed_steps < m_settings.max_fixed_steps) {
                ++m_timing.fixed_index;
                ++m_timing.fixed_steps;
                m_timing.fixed_time = m_timing.fixed_index * step;
                const System::Context context{
                    step, m_timing.fixed_time, m_timing.fixed_index, m_fixed_input};
                for(auto& system : m_systems)
                    system->fixed_update(*m_scene, context);
                clear_edges(m_fixed_input);
                m_accumulator = std::max(0.0, m_accumulator - step);
            }
            // 只丢弃整步积压，避免长帧形成无限追赶；保留插值余量。
            const double dropped = std::floor((m_accumulator + epsilon) / step) * step;
            m_timing.dropped_time += dropped;
            m_accumulator = std::max(0.0, m_accumulator - dropped);
            m_timing.interpolation = m_accumulator / step;
            m_timing.total_time += delta;
            ++m_timing.frame_index;
            const System::Context context{
                delta, m_timing.total_time, m_timing.frame_index, frame_input};
            for(auto& system : m_systems)
                system->update(*m_scene, context);
            m_executing = false;
        } catch(...) {
            // 部分 Scene 修改不能自动回滚；停止运行并向调用者报告错误。
            stop_systems();
            throw;
        }
    }
}
