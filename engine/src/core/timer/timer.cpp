#include "timer.h"

namespace Comet {
    Timer::Timer() {
        reset();
    }

    void Timer::tick() {
        const auto now = std::chrono::high_resolution_clock::now();

        // 计算 deltaTime
        const std::chrono::duration<double> frame_time = now - m_last_frame;
        m_delta_time = static_cast<float>(frame_time.count());
        m_total_time += m_delta_time;
        m_last_frame = now;

        // FPS 统计 - 使用滑动平均平滑FPS
        m_frame_count++;
        m_fps_timer += m_delta_time;
        
        // 每0.5秒更新一次FPS，提供稳定的显示
        if (m_fps_timer >= 0.5f) {
            const float current_fps = static_cast<float>(m_frame_count) / m_fps_timer;
            
            // 如果是第一次计算，直接使用当前值
            if (m_fps == 0.0f) {
                m_fps = current_fps;
            } else {
                // 使用指数加权移动平均平滑FPS (90%旧值 + 10%新值)
                m_fps = m_fps * 0.9f + current_fps * 0.1f;
            }
            
            m_frame_count = 0;
            m_fps_timer = 0.0f;
        }
        m_frame_index++;
    }

    void Timer::reset() {
        m_last_frame = std::chrono::high_resolution_clock::now();
        m_delta_time = 0.0f;
        m_total_time = 0.0f;
        m_fps = 0.0f;
        m_frame_count = 0;
        m_fps_timer = 0.0f;
        m_frame_index = 0;
    }

    UpdateContext Timer::get_update_context() const {
        UpdateContext ctx;
        ctx.deltaTime = m_delta_time;
        ctx.totalTime = m_total_time;
        ctx.frameIndex = m_frame_index;
        ctx.fps = m_fps;
        return ctx;
    }
}
