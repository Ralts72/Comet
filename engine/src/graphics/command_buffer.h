#pragma once
#include "buffer.h"
#include "vk_common.h"

namespace Comet {
    class Device;
    class RenderPass;
    class FrameBuffer;
    class Pipeline;
    class PipelineLayout;

    class CommandBuffer {
    public:
        friend class CommandPool;
        CommandBuffer() = delete;

        void begin(vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlags{});
        void end();
        void reset();

        // render pass
        void begin_render_pass(const RenderPass& render_pass, const FrameBuffer& frame_buffer,
            const std::vector<vk::ClearValue>& clear_values);
        void end_render_pass();

        // bind
        void bind_pipeline(const Pipeline& pipeline);
        
        // dynamic state
        void set_viewport(const vk::Viewport& viewport);
        void set_scissor(const vk::Rect2D& scissor);

        void copy_buffer(vk::Buffer src_buffer, vk::Buffer dst_buffer, size_t size, size_t src_offset = 0, size_t dst_offset = 0);

        void bind_vertex_buffer(std::span<const Buffer*> buffers, std::span<const vk::DeviceSize> offsets, uint32_t first_binding = 0);

        void bind_index_buffer(const Buffer& buffer, vk::DeviceSize offset, vk::IndexType type = vk::IndexType::eUint32);

        void push_constants(const PipelineLayout& layout, vk::ShaderStageFlags stage_flags,
            uint32_t offset, const void* data, size_t size);
        // void bind_descriptor_set(const vk::PipelineLayout& layout,
        //                          const DescriptorSet& set,
        //                          vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics)
        // {
        //     m_cmd.bindDescriptorSets(bindPoint, layout, 0, set.get_handle(), {});
        // }

        // draw
        void draw(uint32_t vertex_count, uint32_t instance_count = 1,
              uint32_t first_vertex = 0, uint32_t first_instance = 0);
        void draw_indexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
            int32_t vertex_offset = 0, uint32_t first_instance = 0);

        [[nodiscard]] vk::CommandBuffer get_command_buffer() const { return m_command_buffer; }

    private:
        explicit CommandBuffer(const vk::CommandBuffer command_buffer): m_command_buffer(command_buffer) {}
        vk::CommandBuffer m_command_buffer;
    };

    class CommandPool {
    public:
        CommandPool(Device* device, uint32_t queue_family_index);
        ~CommandPool();

        [[nodiscard]] std::vector<CommandBuffer> allocate_command_buffers(uint32_t count) const;
        [[nodiscard]] CommandBuffer allocate_command_buffer() const;
        [[nodiscard]] vk::CommandPool get_command_pool() const { return m_command_pool; }

    private:
        Device* m_device;
        vk::CommandPool m_command_pool;
    };
}
