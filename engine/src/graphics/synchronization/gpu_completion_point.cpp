#include "graphics/synchronization/gpu_completion_point.h"

#include "diagnostics/logger.h"
#include "graphics/synchronization/semaphore.h"

namespace Comet {
    bool GpuCompletionPoint::is_complete() const {
        return is_valid() && m_timeline->get_counter_value() >= m_value;
    }

    bool GpuCompletionPoint::wait(const uint64_t timeout) const {
        if(!is_valid()) {
            LOG_FATAL("Cannot wait for an invalid GPU completion point");
        }
        return m_timeline->wait(m_value, timeout);
    }
}
