#pragma once

#include <atomic>
#include <cstdint>

namespace Comet {
    template<typename Identity> struct VersionStamp {
        Identity identity{};
        uint64_t revision = 0;

        bool operator==(const VersionStamp&) const = default;
    };

    // 每个身份域持有自己的序列；只生成非零标识，不同步对应数据的读写。
    class VersionSequence {
    public:
        [[nodiscard]] uint64_t next() noexcept {
            return m_next.fetch_add(1, std::memory_order_relaxed);
        }

    private:
        std::atomic<uint64_t> m_next{1};
    };
}
