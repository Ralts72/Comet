#pragma once

#include "scene/systems/system.h"

#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Scene;

    // 主线程串行编排；活动 Scene 必须活到 stop 完成之后。
    class COMET_API SceneRuntime final {
    public:
        enum class State { Running, Paused };

        struct Settings {
            double fixed_delta = 1.0 / 60.0;
            double max_frame_delta = 0.25;
            uint32_t max_fixed_steps = 8;
        };
        struct Timing {
            uint64_t frame_index = 0;
            uint64_t fixed_index = 0;
            uint32_t fixed_steps = 0;
            double total_time = 0;
            double fixed_time = 0;
            double interpolation = 0;
            double dropped_time = 0;
        };

        SceneRuntime() = default;
        ~SceneRuntime();
        SceneRuntime(const SceneRuntime&) = delete;
        SceneRuntime& operator=(const SceneRuntime&) = delete;

        Result<void, Error> set_settings(Settings settings);
        Result<void, Error> add_system(std::unique_ptr<System> system);
        Result<void, Error> clear_systems();
        Result<void, Error> start(Scene& scene);
        Result<void, Error> stop();
        Result<void, Error> set_state(State state);
        Result<void, Error> request_step();
        // nullptr 关闭输入，不改变运行／暂停状态；输入只在调用期间借用。
        Result<void, Error> advance(double delta_time, const Input::Frame* input = nullptr);
        [[nodiscard]] bool is_active() const { return m_scene != nullptr; }
        [[nodiscard]] State get_state() const { return m_state; }
        [[nodiscard]] const Timing& get_timing() const { return m_timing; }

    private:
        void stop_systems() noexcept;
        Input::Frame consume_input(const Input::Frame* input);

        Settings m_settings;
        Timing m_timing;
        std::vector<std::unique_ptr<System>> m_systems;
        Scene* m_scene = nullptr;
        size_t m_started = 0;
        bool m_executing = false;
        State m_state = State::Running;
        bool m_step_pending = false;
        bool m_rebase_input = false;
        double m_accumulator = 0;
        Input::Frame m_fixed_input;
        std::optional<uint64_t> m_input_serial;
    };
}
