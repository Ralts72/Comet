#include "common/retry_backoff.h"

#include <algorithm>
#include <cassert>

namespace Comet {
    RetryBackoff::RetryBackoff(Policy policy) : m_policy(policy) {
        assert(policy.initial_delay.count() > 0 && policy.max_delay >= policy.initial_delay);
    }

    bool RetryBackoff::schedule(Clock::time_point now) {
        if(m_due)
            return true;
        if(m_retry_count >= m_policy.max_retries)
            return false;
        auto delay = m_policy.initial_delay;
        for(uint32_t step = 0; step < m_retry_count && delay < m_policy.max_delay; ++step)
            delay += std::min(delay, m_policy.max_delay - delay);
        m_due = now + delay;
        ++m_retry_count;
        return true;
    }

    bool RetryBackoff::consume(Clock::time_point now) {
        if(!m_due || now < *m_due)
            return false;
        m_due.reset();
        return true;
    }

    void RetryBackoff::reset() {
        m_due.reset();
        m_retry_count = 0;
    }
}
