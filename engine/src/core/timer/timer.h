#pragma once
#include "pch.h"
#include "common/export.h"

namespace Comet {

    struct COMET_API UpdateContext {
        float deltaTime = 0.0f;
        float totalTime = 0.0f;
        int frameIndex = 0;
        float fps = 0.0f;
    };

    class Timer {
    public:
        Timer();
        ~Timer() = default;
        void tick();
        void reset();

        [[nodiscard]] float get_delta_time() const {return m_delta_time;}
        [[nodiscard]] float get_total_time() const {return m_total_time;}
        [[nodiscard]] float get_fps() const { return m_fps; }
        [[nodiscard]] UpdateContext get_update_context() const;
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> m_last_frame;
        float m_total_time = 0.0f;
        float m_delta_time = 0.0f;

        float m_fps = 0.0f;
        float m_fps_timer = 0.0f;
        int m_frame_count = 0;
        int m_frame_index = 0;
    };
}
