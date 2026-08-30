#pragma once

#include "common/export.h"

#include <cstdint>
#include <vector>

namespace Comet {
    struct COMET_API MemoryHeapBudget {
        uint32_t block_count = 0;
        uint32_t allocation_count = 0;
        uint64_t block_bytes = 0;
        uint64_t allocation_bytes = 0;
        uint64_t usage_bytes = 0;
        uint64_t budget_bytes = 0;

        [[nodiscard]] uint64_t available_bytes() const noexcept {
            return budget_bytes > usage_bytes
                ? budget_bytes - usage_bytes
                : 0;
        }
    };

    struct COMET_API MemoryBudgetSnapshot {
        std::vector<MemoryHeapBudget> heaps;
        bool driver_reported = false;
    };
}
