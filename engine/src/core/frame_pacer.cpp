#include "core/frame_pacer.h"

namespace Comet {
    Result<void> FramePacer::validate_limit(int limit) {
        if(limit < 0 || limit > MAX_LIMIT)
            return Result<void>::failure("Frame rate limit must be an integer from 0 to 1000");
        return Result<void>::success();
    }

    Result<void> FramePacer::set_limit(int limit) {
        if(auto valid = validate_limit(limit); !valid)
            return valid;
        m_limit = limit;
        m_interval = {};
        if(limit != 0)
            m_interval = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1.0 / limit));
        return Result<void>::success();
    }

    std::optional<FramePacer::Clock::time_point> FramePacer::deadline(
        Clock::time_point frame_start) const {
        if(m_limit == 0)
            return {};
        return frame_start + m_interval;
    }
}
