#pragma once

#include "common/export.h"

#include <cstdint>
#include <limits>
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

        [[nodiscard]] bool reaches_usage_percentage(
            uint64_t additional_bytes,
            uint32_t percentage) const noexcept {
            if(budget_bytes == 0 || percentage == 0 || percentage > 100) {
                return false;
            }

            const uint64_t whole = budget_bytes / 100 * percentage;
            const uint64_t remainder = budget_bytes % 100 * percentage;
            const uint64_t threshold = whole + (remainder + 99) / 100;
            const uint64_t projected_usage =
                additional_bytes >
                    std::numeric_limits<uint64_t>::max() - usage_bytes
                ? std::numeric_limits<uint64_t>::max()
                : usage_bytes + additional_bytes;
            return projected_usage >= threshold;
        }
    };

    struct COMET_API MemoryBudgetSnapshot {
        std::vector<MemoryHeapBudget> heaps;
        bool driver_reported = false;
    };
}
