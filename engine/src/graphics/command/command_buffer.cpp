#include "graphics/command/command_buffer.h"
#include "graphics/device.h"
#include "graphics/render_pass.h"
#include "diagnostics/logger.h"
#include "graphics/convert.h"
#include "graphics/frame_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/synchronization/barrier.h"
#include "diagnostics/profiler.h"

namespace Comet {
    void CommandBuffer::begin(const vk::CommandBufferUsageFlags flags) const {
        m_command_buffer.reset();
        vk::CommandBufferBeginInfo begin_info = {};
        begin_info.pInheritanceInfo = nullptr;
        begin_info.flags = flags;
        m_command_buffer.begin(begin_info);
    }

    void CommandBuffer::end() const {
        m_command_buffer.end();
    }

    void CommandBuffer::reset() const {
        m_command_buffer.reset();
    }

    void CommandBuffer::begin_render_pass(const RenderPass& render_pass,
        const FrameBuffer& frame_buffer,
        const std::vector<ClearValue>& clear_values) const {
        std::vector<vk::ClearValue> vk_clear_value;
        vk_clear_value.reserve(clear_values.size());
        for(auto& clear_value : clear_values) {
            vk_clear_value.push_back(clear_value.vk_value());
        }
        PROFILE_SCOPE("CommandBuffer::BeginRenderPass");
        vk::RenderPassBeginInfo render_pass_info = {};
        render_pass_info.renderPass = render_pass.get();
        render_pass_info.framebuffer = frame_buffer.get();
        vk::Rect2D render_area = {};
        render_area.extent.width = frame_buffer.get_width();
        render_area.extent.height = frame_buffer.get_height();
        render_area.offset.x = 0;
        render_area.offset.y = 0;
        render_pass_info.renderArea = render_area;
        render_pass_info.clearValueCount = static_cast<uint32_t>(vk_clear_value.size());
        render_pass_info.pClearValues = vk_clear_value.data();

        m_command_buffer.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
        LOG_TRACE(
            "RenderPass: begin render pass with {} clear values", clear_values.size());
    }

    void CommandBuffer::end_render_pass() const {
        m_command_buffer.endRenderPass();
    }

    void CommandBuffer::bind_pipeline(const Pipeline& pipeline) const {
        m_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.get());
        LOG_TRACE("CommandBuffer: bind pipeline {}", pipeline.get_name());
    }

    void CommandBuffer::set_viewport(const vk::Viewport& viewport) const {
        m_command_buffer.setViewport(0, 1, &viewport);
    }

    void CommandBuffer::set_scissor(const vk::Rect2D& scissor) const {
        m_command_buffer.setScissor(0, 1, &scissor);
    }

    void CommandBuffer::bind_vertex_buffer(
        const VertexBufferBinding& binding, const uint32_t first_binding) const {
        const vk::Buffer buffer = binding.buffer.get();
        m_command_buffer.bindVertexBuffers(first_binding, 1, &buffer, &binding.offset);
    }

    void CommandBuffer::bind_vertex_buffers(
        const std::span<const VertexBufferBinding> bindings,
        const uint32_t first_binding) const {
        if(bindings.empty()) {
            return;
        }

        std::vector<vk::Buffer> buffers;
        std::vector<uint64_t> offsets;
        buffers.reserve(bindings.size());
        offsets.reserve(bindings.size());
        for(const auto& [buffer, offset] : bindings) {
            buffers.push_back(buffer.get());
            offsets.push_back(offset);
        }

        m_command_buffer.bindVertexBuffers(first_binding,
            static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
    }

    void CommandBuffer::bind_index_buffer(
        const Buffer& buffer, const uint64_t offset, const IndexType type) const {
        m_command_buffer.bindIndexBuffer(
            buffer.get(), offset, Graphics::index_type_to_vk(type));
    }

    void CommandBuffer::push_constants(const PipelineLayout& layout,
        const Flags<ShaderStage> stage_flags, const uint32_t offset, const void* data,
        const size_t size) const {
        m_command_buffer.pushConstants(
            layout.get(), Graphics::shader_stage_to_vk(stage_flags), offset, size, data);
    }

    void CommandBuffer::draw(const uint32_t vertex_count, const uint32_t instance_count,
        const uint32_t first_vertex, const uint32_t first_instance) const {
        m_command_buffer.draw(vertex_count, instance_count, first_vertex, first_instance);
    }
    void CommandBuffer::draw_indexed(const uint32_t index_count,
        const uint32_t instance_count, const uint32_t first_index,
        const int32_t vertex_offset, const uint32_t first_instance) const {
        m_command_buffer.drawIndexed(
            index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    void CommandBuffer::copy_buffer(const vk::Buffer src_buffer,
        const vk::Buffer dst_buffer, const size_t size, const size_t src_offset,
        const size_t dst_offset) const {
        vk::BufferCopy copy_buffer{};
        copy_buffer.srcOffset = src_offset;
        copy_buffer.dstOffset = dst_offset;
        copy_buffer.size = size;
        m_command_buffer.copyBuffer(src_buffer, dst_buffer, 1, &copy_buffer);
    }

    void CommandBuffer::copy_buffer_to_image(const vk::Buffer src_buffer,
        const vk::Image dst_image, const vk::ImageLayout dst_image_layout,
        const vk::Extent3D& extent, const uint32_t base_array_layer,
        const uint32_t layer_count, const uint32_t mip_level,
        const vk::DeviceSize buffer_offset) const {
        vk::BufferImageCopy buffer_image_copy{};
        buffer_image_copy.bufferOffset = buffer_offset;
        buffer_image_copy.bufferRowLength = extent.width;
        buffer_image_copy.bufferImageHeight = extent.height;
        buffer_image_copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        buffer_image_copy.imageSubresource.mipLevel = mip_level;
        buffer_image_copy.imageSubresource.baseArrayLayer = base_array_layer;
        buffer_image_copy.imageSubresource.layerCount = layer_count;
        buffer_image_copy.imageOffset = vk::Offset3D{0, 0, 0};
        buffer_image_copy.imageExtent = extent;
        m_command_buffer.copyBufferToImage(
            src_buffer, dst_image, dst_image_layout, 1, &buffer_image_copy);
    }

    void CommandBuffer::transition_image_state(
        const vk::Image image, const ImageState& before, const ImageState& after) const {
        const auto barrier = Graphics::build_image_memory_barrier(image, before, after);
        if(!barrier) {
            LOG_FATAL("Invalid image state transition");
        }

        vk::DependencyInfo dependency_info{};
        dependency_info.imageMemoryBarrierCount = 1;
        dependency_info.pImageMemoryBarriers = &*barrier;
        m_command_buffer.pipelineBarrier2(dependency_info);
    }

    void CommandBuffer::transition_buffer_state(const vk::Buffer buffer,
        const ResourceState& before, const ResourceState& after,
        const vk::DeviceSize offset, const vk::DeviceSize size) const {
        const auto barrier =
            Graphics::build_buffer_memory_barrier(buffer, before, after, offset, size);
        if(!barrier) {
            LOG_FATAL("Invalid buffer state transition");
        }

        vk::DependencyInfo dependency_info{};
        dependency_info.bufferMemoryBarrierCount = 1;
        dependency_info.pBufferMemoryBarriers = &*barrier;
        m_command_buffer.pipelineBarrier2(dependency_info);
    }

    CommandPool::CommandPool(Device& device, const uint32_t queue_family_index)
        : m_device(device) {
        vk::CommandPoolCreateInfo pool_info = {};
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pool_info.queueFamilyIndex = queue_family_index;
        m_command_pool = m_device.get().createCommandPool(pool_info);
        LOG_INFO("Vulkan command pool created successfully");
    }

    CommandPool::~CommandPool() {
        m_device.get().destroyCommandPool(m_command_pool);
    }

    std::vector<CommandBuffer> CommandPool::allocate_command_buffers(
        const uint32_t count) const {
        std::vector<vk::CommandBuffer> cmd_buffers(count);
        vk::CommandBufferAllocateInfo allocate_info = {};
        allocate_info.commandPool = m_command_pool;
        allocate_info.commandBufferCount = count;
        allocate_info.level = vk::CommandBufferLevel::ePrimary;
        cmd_buffers = m_device.get().allocateCommandBuffers(allocate_info);
        std::vector<CommandBuffer> command_buffers;
        command_buffers.reserve(count);
        for(const auto& cmd_buffer : cmd_buffers) {
            command_buffers.emplace_back(CommandBuffer(cmd_buffer));
        }
        LOG_INFO("Vulkan command buffers allocated successfully (count: {})", count);
        return command_buffers;
    }

    CommandBuffer CommandPool::allocate_command_buffer() const {
        const auto buffer = allocate_command_buffers(1);
        return buffer[0];
    }

    void CommandPool::free_command_buffer(const CommandBuffer& command_buffer) const {
        free_command_buffers(std::span(&command_buffer, 1));
    }

    void CommandPool::free_command_buffers(
        const std::span<const CommandBuffer> command_buffers) const {
        if(command_buffers.empty()) {
            return;
        }

        std::vector<vk::CommandBuffer> vk_command_buffers;
        vk_command_buffers.reserve(command_buffers.size());
        for(const auto& command_buffer : command_buffers) {
            vk_command_buffers.emplace_back(command_buffer.get());
        }

        m_device.get().freeCommandBuffers(m_command_pool, vk_command_buffers);
    }
}
