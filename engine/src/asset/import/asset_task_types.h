#pragma once

#include <chrono>
#include <cstddef>

namespace Comet {
    struct AssetAsyncLimits {
        // 各项均必须为正；这是内部调度预算，不是禁用异步加载的开关。
        std::size_t in_flight = 8;
        std::size_t queued = 128;
        std::size_t working_bytes = 2ull * 1024 * 1024 * 1024;
    };
    struct AssetAsyncStatus {
        std::size_t in_flight;
        std::size_t queued;
        std::size_t reserved_bytes = 0;
    };
    struct AssetCompletionBudget {
        std::size_t max_results = 2;
        std::chrono::nanoseconds max_time = std::chrono::milliseconds(2);
    };
    enum class MeshImportMode { IfNeeded, Force };
}
