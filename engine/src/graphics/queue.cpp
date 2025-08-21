#include "queue.h"
#include "command_buffer.h"
#include "semaphore.h"
#include "fence.h"

namespace Comet {
    Queue::Queue(const uint32_t family_index, const uint32_t index, const vk::Queue queue, const QueueType type)
    : m_family_index(family_index), m_index(index), m_queue(queue), m_type(type) {}

    void Queue::submit(const std::span<const CommandBuffer> command_buffers, const std::span<const Semaphore> wait_semaphores,
        const std::span<const Semaphore> signal_semaphores, const Fence* fence) const {
        std::vector<vk::Semaphore> vk_wait_semaphores;
        vk_wait_semaphores.reserve(wait_semaphores.size());
        for(const auto& semaphore : wait_semaphores) {
            vk_wait_semaphores.emplace_back(semaphore.get());
        }
        std::vector<vk::Semaphore> vk_signal_semaphores;
        vk_signal_semaphores.reserve(signal_semaphores.size());
        for(const auto& semaphore : signal_semaphores) {
            vk_signal_semaphores.emplace_back(semaphore.get());
        }

        const std::vector<vk::PipelineStageFlags> stage_inflo = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
        std::vector<vk::CommandBuffer> cmd_buffers;
        cmd_buffers.reserve(command_buffers.size());
        for(const auto& cmd_buffer : command_buffers) {
            cmd_buffers.emplace_back(cmd_buffer.get());
        }
        vk::SubmitInfo submit_info = {};
        submit_info.commandBufferCount = static_cast<uint32_t>(cmd_buffers.size());
        submit_info.pCommandBuffers = cmd_buffers.data();
        submit_info.signalSemaphoreCount = static_cast<uint32_t>(vk_signal_semaphores.size());
        submit_info.pSignalSemaphores = vk_signal_semaphores.data();
        submit_info.pWaitDstStageMask = stage_inflo.data();
        submit_info.pWaitSemaphores = vk_wait_semaphores.data();
        submit_info.waitSemaphoreCount = static_cast<uint32_t>(vk_wait_semaphores.size());

        if(!fence) {
            m_queue.submit(submit_info, nullptr);
            return;
        }
        const auto vk_fence = fence->get();
        m_queue.submit(submit_info, vk_fence);
    }
}
