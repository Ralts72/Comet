#pragma once
#include "buffer.h"
#include "vk_common.h"

namespace Comet {
    class Device;
    class RenderPass;
    class FrameBuffer;
    class Pipeline;
    class PipelineLayout;
    class RenderTarget;

    class CommandBuffer {
    public:
        friend class CommandPool;
        friend class Device;
        friend class CommandContext;

        CommandBuffer() = delete;

        void begin(vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlags{}) const;

        void end() const;

        void reset() const;

        // render pass
        void begin_render_pass(const RenderPass& render_pass, const FrameBuffer& frame_buffer,
                               const std::vector<ClearValue>& clear_values) const;

        void end_render_pass() const;

        // bind
        void bind_pipeline(const Pipeline& pipeline) const;

        // dynamic state
        void set_viewport(const vk::Viewport& viewport) const;

        void set_scissor(const vk::Rect2D& scissor) const;

        void bind_vertex_buffer(std::span<const Buffer*> buffers, std::span<const uint64_t> offsets,
                                uint32_t first_binding = 0) const;

        void bind_index_buffer(const Buffer& buffer, uint64_t offset,
                               vk::IndexType type = vk::IndexType::eUint32) const;

        void push_constants(const PipelineLayout& layout, Flags<ShaderStage> stage_flags,
                            uint32_t offset, const void* data, size_t size) const;

        // draw
        void draw(uint32_t vertex_count, uint32_t instance_count = 1,
                  uint32_t first_vertex = 0, uint32_t first_instance = 0) const;

        void draw_indexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
                         int32_t vertex_offset = 0, uint32_t first_instance = 0) const;

        // image layout transition
        void transition_image_layout(vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                     uint32_t base_array_layer = 0, uint32_t layer_count = 1, uint32_t mip_level = 0) const;

        [[nodiscard]] vk::CommandBuffer get() const { return m_command_buffer; }

    private:
        explicit CommandBuffer(const vk::CommandBuffer command_buffer) : m_command_buffer(command_buffer) {}

        void copy_buffer(vk::Buffer src_buffer, vk::Buffer dst_buffer, size_t size, size_t src_offset = 0, size_t dst_offset = 0) const;

        void copy_buffer_to_image(vk::Buffer src_buffer, vk::Image dst_image, vk::ImageLayout dst_image_layout,
                                  const vk::Extent3D& extent, uint32_t base_array_layer = 0, uint32_t layer_count = 1, uint32_t mip_level = 0) const;

        vk::CommandBuffer m_command_buffer;
    };

    class CommandPool {
    public:
        CommandPool(Device* device, uint32_t queue_family_index);

        ~CommandPool();

        [[nodiscard]] std::vector<CommandBuffer> allocate_command_buffers(uint32_t count) const;

        [[nodiscard]] CommandBuffer allocate_command_buffer() const;

        [[nodiscard]] vk::CommandPool get() const { return m_command_pool; }

    private:
        Device* m_device;
        vk::CommandPool m_command_pool;
    };
}
