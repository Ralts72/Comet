#pragma once

#include "scene/systems/system.h"
#include "input/runtime_input.h"
#include "scene/runtime_session.h"

#include <memory>
#include <vector>

namespace Comet {
    class Scene;

    // 主线程串行编排；活动 Scene 必须活到 stop 完成之后。
    class COMET_API SceneRuntime final {
    public:
        enum class State { Running, Paused };
        enum class InputStart { Fresh, Rebase };

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
        Result<void, Error> set_services(RuntimeServices services);
        Result<void, Error> set_input_actions(InputActions actions);
        // 活动运行域的绑定替换在下一次输入准备时生效，不重启系统或修改动作定义。
        Result<void, Error> rebind_input_actions(InputActions actions);
        Result<void, Error> add_system(std::unique_ptr<System> system);
        Result<void, Error> clear_systems();
        // Rebase 在首张授权输入上建立基线，不把开局前的按下／位移重放到新局。
        Result<void, Error> start(
            Scene& scene, State state = State::Running, InputStart input = InputStart::Fresh);
        Result<void, Error> stop();
        Result<void, Error> set_state(State state);
        Result<void, Error> request_step();
        Result<void, Error> discard_input();
        // nullptr 关闭输入，不改变运行／暂停状态；输入只在调用期间借用。
        Result<void, Error> advance(double delta_time, const Input::Frame* input = nullptr);
        [[nodiscard]] bool is_active() const { return m_scene != nullptr; }
        [[nodiscard]] State get_state() const { return m_state; }
        [[nodiscard]] const Timing& get_timing() const { return m_timing; }
        [[nodiscard]] RuntimeSession& get_session() { return m_session; }
        [[nodiscard]] const RuntimeSession& get_session() const { return m_session; }
        // 仅宿主更新边界消费；运行回调内不取走本局的重开意图。
        [[nodiscard]] bool take_restart_request();
        // 当前场景和最近授权输入的意图；不是窗口已经捕获的状态。
        [[nodiscard]] bool wants_cursor_capture() const;

    private:
        void stop_systems() noexcept;

        Settings m_settings;
        Timing m_timing;
        std::vector<std::unique_ptr<System>> m_systems;
        Scene* m_scene = nullptr;
        size_t m_started = 0;
        bool m_executing = false;
        State m_state = State::Running;
        bool m_step_pending = false;
        // 状态切换或中断后，旧阶段电平不能恢复窗口捕获。
        bool m_input_prepared = false;
        double m_accumulator = 0;
        RuntimeInput m_input;
        RuntimeSession m_session;
        RuntimeServices m_services;
    };
}
