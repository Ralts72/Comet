#pragma once

#include "common/export.h"
#include "graphics/synchronization/gpu_completion_point.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace Comet {
    class COMET_API GpuRetirementQueue {
    public:
        GpuRetirementQueue() = default;
        ~GpuRetirementQueue();

        GpuRetirementQueue(const GpuRetirementQueue&) = delete;
        GpuRetirementQueue& operator=(const GpuRetirementQueue&) = delete;
        GpuRetirementQueue(GpuRetirementQueue&&) noexcept = delete;
        GpuRetirementQueue& operator=(GpuRetirementQueue&&) noexcept = delete;

        template<typename T>
        void retire(
            const GpuCompletionPoint& completion,
            std::shared_ptr<T> resource) {
            if(!resource) {
                return;
            }
            std::vector<std::shared_ptr<void>> resources;
            resources.emplace_back(std::move(resource));
            retire_batch(completion, std::move(resources));
        }

        void retire_batch(
            const GpuCompletionPoint& completion,
            std::vector<std::shared_ptr<void>> resources);
        void collect_completed();
        void wait_and_clear();

        [[nodiscard]] size_t get_pending_batch_count() const noexcept {
            return m_pending_batches.size();
        }
        [[nodiscard]] size_t get_pending_resource_count() const noexcept;

    private:
        struct RetiredBatch {
            GpuCompletionPoint completion;
            std::vector<std::shared_ptr<void>> resources;
        };

        std::vector<RetiredBatch> m_pending_batches;
    };
}
