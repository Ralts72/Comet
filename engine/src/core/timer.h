#pragma once

#include "common/export.h"

#include <chrono>

namespace Comet {

    struct COMET_API UpdateContext {
        float delta_time = 0.0f;
        float total_time = 0.0f;
        int frame_index = 0;
        float fps = 0.0f;
    };

    class COMET_API Timer {
    public:
        Timer() = default;
        ~Timer() = default;
        void tick();

        [[nodiscard]] UpdateContext get_update_context() const;

    private:
        std::chrono::steady_clock::time_point m_last_frame =
            std::chrono::steady_clock::now();
        float m_total_time = 0.0f;
        float m_delta_time = 0.0f;

        float m_fps = 0.0f;
        float m_fps_timer = 0.0f;
        int m_frame_count = 0;
        int m_frame_index = 0;
    };
}
