#include "queue.h"
#include "graphics/command/command_buffer.h"
#include "graphics/synchronization/semaphore.h"
#include "graphics/synchronization/fence.h"
#include "diagnostics/profiler.h"
#include "swapchain.h"

namespace Comet {
    namespace {
        vk::SemaphoreSubmitInfo make_semaphore_submit_info(const Semaphore& semaphore,
            const uint64_t value, const Flags<PipelineStage> stage_mask) {
            const vk::PipelineStageFlags2 vk_stage_mask =
                Graphics::pipeline_stage_to_vk2(stage_mask);
            if(!vk_stage_mask) {
                LOG_FATAL("Queue semaphore stage mask must not be empty");
            }
            if((semaphore.get_type() == Semaphore::Type::Binary && value != 0)
                || (semaphore.get_type() == Semaphore::Type::Timeline && value == 0)) {
                LOG_FATAL("Queue semaphore value does not match semaphore type");
            }

            vk::SemaphoreSubmitInfo info{};
            info.semaphore = semaphore.get();
            info.value = value;
            info.stageMask = vk_stage_mask;
            return info;
        }
    }

    QueueSemaphoreSubmit::QueueSemaphoreSubmit(
        const GpuCompletionPoint& completion, const Flags<PipelineStage> stage_mask)
        : semaphore(completion.m_timeline), value(completion.m_value),
          stage_mask(stage_mask) {
        if(!completion.is_valid()) {
            LOG_FATAL("Cannot submit an invalid GPU completion point");
        }
    }

    Queue::Queue(Device& device, const vk::Queue queue)
        : m_queue(queue), m_completion_timeline(std::make_unique<Semaphore>(
                              device, Semaphore::Type::Timeline)) {}

    GpuCompletionPoint Queue::submit2(const std::span<const QueueSemaphoreSubmit> waits,
        const std::span<const CommandBuffer> command_buffers,
        const std::span<const QueueSemaphoreSubmit> signals, const Fence* fence) {
        std::vector<vk::SemaphoreSubmitInfo> wait_infos;
        wait_infos.reserve(waits.size());
        for(const auto& wait : waits) {
            wait_infos.push_back(
                make_semaphore_submit_info(*wait.semaphore, wait.value, wait.stage_mask));
        }

        std::vector<vk::CommandBufferSubmitInfo> command_infos;
        command_infos.reserve(command_buffers.size());
        for(const auto& command_buffer : command_buffers) {
            vk::CommandBufferSubmitInfo info{};
            info.commandBuffer = command_buffer.get();
            command_infos.push_back(info);
        }

        std::vector<vk::SemaphoreSubmitInfo> signal_infos;
        signal_infos.reserve(signals.size() + 1);
        for(const auto& signal : signals) {
            signal_infos.push_back(make_semaphore_submit_info(
                *signal.semaphore, signal.value, signal.stage_mask));
        }
        if(m_next_completion_value == std::numeric_limits<uint64_t>::max()) {
            LOG_FATAL("Queue completion timeline value exhausted");
        }
        const uint64_t completion_value = m_next_completion_value++;
        signal_infos.push_back(make_semaphore_submit_info(*m_completion_timeline,
            completion_value, Flags<PipelineStage>(PipelineStage::AllCommands)));

        vk::SubmitInfo2 submit_info{};
        submit_info.waitSemaphoreInfoCount = static_cast<uint32_t>(wait_infos.size());
        submit_info.pWaitSemaphoreInfos = wait_infos.data();
        submit_info.commandBufferInfoCount = static_cast<uint32_t>(command_infos.size());
        submit_info.pCommandBufferInfos = command_infos.data();
        submit_info.signalSemaphoreInfoCount = static_cast<uint32_t>(signal_infos.size());
        submit_info.pSignalSemaphoreInfos = signal_infos.data();

        const vk::Fence vk_fence = fence ? fence->get() : vk::Fence{};
        const vk::Result result = m_queue.submit2(1, &submit_info, vk_fence);
        if(result != vk::Result::eSuccess) {
            LOG_FATAL("Queue submit2 failed: {}", vk::to_string(result));
        }
        return GpuCompletionPoint(*m_completion_timeline, completion_value);
    }

    vk::Result Queue::present(const Swapchain& swapchain,
        const std::span<const Semaphore> wait_semaphores, uint32_t image_index) const {
        PROFILE_SCOPE("queue: swapchain present");
        std::vector<vk::Semaphore> vk_wait_semaphores;
        for(const auto& wait_sem : wait_semaphores) {
            vk_wait_semaphores.emplace_back(wait_sem.get());
        }
        vk::PresentInfoKHR present_info = {};
        present_info.waitSemaphoreCount =
            static_cast<uint32_t>(vk_wait_semaphores.size());
        present_info.pWaitSemaphores = vk_wait_semaphores.data();
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain.get();
        present_info.pImageIndices = &image_index;
        const auto result = m_queue.presentKHR(present_info);
        if(result == vk::Result::eSuboptimalKHR
            || result == vk::Result::eErrorOutOfDateKHR) {
            LOG_WARN("swapchain requires recreation: {}", vk::to_string(result));
        } else if(result != vk::Result::eSuccess) {
            LOG_ERROR("Failed to present swapchain image: {}", vk::to_string(result));
        } else {
            LOG_DEBUG("Presented swapchain image at index: {}", image_index);
        }
        return result;
    }
}
