#pragma once
#include "vk_common.h"
#include "buffer.h"
#include "queue.h"
#include "command_buffer.h"

namespace Comet {
    class Context;
    class Queue;
    class Fence;
    class CommandPool;
    class CommandBuffer;
    class CommandContext;

    class Device {
    public:
        Device(Context* context, uint32_t graphics_queue_count, uint32_t present_queue_count);

        ~Device();

        void wait_for_fences(std::span<const Fence> fences, bool wait_all = true,
                             uint64_t timeout = std::numeric_limits<uint64_t>::max()) const;

        void reset_fences(std::span<const Fence> fences) const;

        void wait_idle() const;

        [[nodiscard]] vk::Device get() const { return m_device; }

        [[nodiscard]] Queue& get_graphics_queue(const uint32_t index = 0) {
            return m_graphics_queues.at(index);
        }

        [[nodiscard]] const Queue& get_graphics_queue(const uint32_t index = 0) const {
            return m_graphics_queues.at(index);
        }

        [[nodiscard]] Queue& get_present_queue(const uint32_t index = 0) {
            return m_present_queues.at(index);
        }

        [[nodiscard]] const Queue& get_present_queue(const uint32_t index = 0) const {
            return m_present_queues.at(index);
        }

        [[nodiscard]] vk::PipelineCache get_pipeline_cache() const { return m_pipeline_cache; }

        [[nodiscard]] uint32_t get_memory_index(Flags<MemoryType> mem_props, uint32_t memory_type_bits) const;

        [[nodiscard]] CommandPool& get_default_command_pool() { return *m_default_command_pool; }
        [[nodiscard]] const CommandPool& get_default_command_pool() const { return *m_default_command_pool; }

        std::unique_ptr<CommandContext> create_command_context();

    private:
        void create_pipeline_cache();

        void create_default_command_pool();

        vk::Device m_device;
        Context* m_context;

        std::vector<Queue> m_graphics_queues;
        std::vector<Queue> m_present_queues;
        vk::PipelineCache m_pipeline_cache;
        std::unique_ptr<CommandPool> m_default_command_pool;
    };
}
