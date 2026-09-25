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
    struct AssetImportLimits {
        AssetAsyncLimits async;
        std::size_t source_bytes = 512ull * 1024 * 1024;
        std::size_t texture_working_bytes = 1024ull * 1024 * 1024;
        std::size_t mesh_working_bytes = 1024ull * 1024 * 1024;
        std::size_t mesh_owner_inspect_bytes = 64ull * 1024;
        std::size_t external_file_bytes = 512ull * 1024 * 1024;
        std::size_t external_file_queue = 8;
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
