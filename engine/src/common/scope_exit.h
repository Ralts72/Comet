#pragma once

#include <utility>

namespace Comet {
    template<typename Cleanup> class ScopeExit final {
    public:
        explicit ScopeExit(Cleanup cleanup) : m_cleanup(std::move(cleanup)) {}
        ~ScopeExit() noexcept {
            if(m_active)
                m_cleanup();
        }
        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;
        void release() noexcept { m_active = false; }

    private:
        Cleanup m_cleanup;
        bool m_active = true;
    };
}
