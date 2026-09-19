#pragma once

#include <chrono>
#include <cstddef>

namespace Comet {
    struct AssetAsyncLimits {
        // 两项均必须为正；这是内部调度预算，不是禁用异步加载的开关。
        std::size_t in_flight = 8;
        std::size_t queued = 128;
    };
    struct AssetAsyncStatus {
        std::size_t in_flight;
        std::size_t queued;
    };
    struct AssetCompletionBudget {
        std::size_t max_results = 2;
        std::chrono::nanoseconds max_time = std::chrono::milliseconds(2);
    };
    enum class MeshImportMode { IfNeeded, Force };
}
