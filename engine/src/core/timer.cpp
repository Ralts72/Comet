#include "timer.h"

namespace Comet {
    void Timer::tick() {
        const auto now = std::chrono::steady_clock::now();

        // 单调时钟不受系统时间调整影响。
        const std::chrono::duration<double> frame_time = now - m_last_frame;
        m_delta_time = static_cast<float>(frame_time.count());
        m_total_time += m_delta_time;
        m_last_frame = now;

        m_frame_count++;
        m_fps_timer += m_delta_time;

        // 每 0.5 秒采样一次，再使用指数加权平均平滑 FPS。
        if(m_fps_timer >= 0.5f) {
            const float current_fps = static_cast<float>(m_frame_count) / m_fps_timer;

            if(m_fps == 0.0f) {
                m_fps = current_fps;
            } else {
                // 保留 90% 的旧值，加入 10% 的新值。
                m_fps = m_fps * 0.9f + current_fps * 0.1f;
            }

            m_frame_count = 0;
            m_fps_timer = 0.0f;
        }
        m_frame_index++;
    }

    UpdateContext Timer::get_update_context() const {
        return {.delta_time = m_delta_time,
            .total_time = m_total_time,
            .frame_index = m_frame_index,
            .fps = m_fps};
    }
}
