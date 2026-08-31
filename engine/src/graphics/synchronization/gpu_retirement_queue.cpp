#include "graphics/synchronization/gpu_retirement_queue.h"

#include "diagnostics/logger.h"

#include <algorithm>
#include <iterator>

namespace Comet {
    GpuRetirementQueue::~GpuRetirementQueue() {
        wait_and_clear();
    }

    void GpuRetirementQueue::retire_batch(
        const GpuCompletionPoint& completion,
        std::vector<std::shared_ptr<void>> resources) {
        std::erase(resources, nullptr);
        if(resources.empty()) {
            return;
        }
        if(!completion.is_valid()) {
            LOG_FATAL("Cannot retire GPU resources without a valid completion point");
        }
        if(completion.is_complete()) {
            return;
        }

        if(!m_pending_batches.empty()
           && m_pending_batches.back().completion == completion) {
            auto& pending_resources = m_pending_batches.back().resources;
            pending_resources.insert(
                pending_resources.end(),
                std::make_move_iterator(resources.begin()),
                std::make_move_iterator(resources.end()));
            return;
        }

        m_pending_batches.push_back({
            .completion = completion,
            .resources = std::move(resources)
        });
    }

    void GpuRetirementQueue::collect_completed() {
        std::erase_if(
            m_pending_batches,
            [](const RetiredBatch& batch) {
                return batch.completion.is_complete();
            });
    }

    void GpuRetirementQueue::wait_and_clear() {
        for(const RetiredBatch& batch: m_pending_batches) {
            batch.completion.wait();
        }
        m_pending_batches.clear();
    }

    size_t GpuRetirementQueue::get_pending_resource_count() const noexcept {
        size_t count = 0;
        for(const RetiredBatch& batch: m_pending_batches) {
            count += batch.resources.size();
        }
        return count;
    }
}
