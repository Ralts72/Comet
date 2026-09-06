#pragma once

#include "common/export.h"
#include "core/input.h"

#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Scene;

    class COMET_API System {
    public:
        struct Context {
            double delta_time;
            double total_time;
            uint64_t index;
            const Input::Frame& input;
        };
        virtual ~System() = default;
        virtual void on_start(Scene&) {}
        virtual void fixed_update(Scene&, const Context&) {}
        virtual void update(Scene&, const Context&) {}
        // 也用于启动失败后的清理，必须容忍部分初始化。
        virtual void on_stop(Scene&) noexcept {}
    };

    // 主线程串行执行；Scene 必须比其活动的 Runtime 活得更久。
    class COMET_API SceneRuntime {
    public:
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

        SceneRuntime();
        explicit SceneRuntime(Settings settings);
        ~SceneRuntime();
        SceneRuntime(const SceneRuntime&) = delete;
        SceneRuntime& operator=(const SceneRuntime&) = delete;

        void add_system(std::unique_ptr<System> system);
        void clear_systems();
        void start(Scene& scene);
        void stop();
        void advance(double delta_time, const Input::Frame& input);
        [[nodiscard]] bool is_active() const { return m_scene != nullptr; }
        [[nodiscard]] const Timing& get_timing() const { return m_timing; }

    private:
        void require_idle() const;
        void stop_systems() noexcept;

        Settings m_settings;
        Timing m_timing;
        std::vector<std::unique_ptr<System>> m_systems;
        Scene* m_scene = nullptr;
        size_t m_started = 0;
        bool m_executing = false;
        double m_accumulator = 0;
        Input::Frame m_fixed_input;
        std::optional<uint64_t> m_input_serial;
    };
}
