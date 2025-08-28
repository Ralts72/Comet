#include "command_buffer.h"
#include "device.h"
#include "render_pass.h"
#include "common/logger.h"
#include "frame_buffer.h"
#include "pipeline.h"
#include "common/profiler.h"
#include "render/render_target.h"

namespace Comet {
    void CommandBuffer::begin(const vk::CommandBufferUsageFlags flags) {
        m_command_buffer.reset();
        vk::CommandBufferBeginInfo begin_info = {};
        begin_info.pInheritanceInfo = nullptr;
        begin_info.flags = flags;
        m_command_buffer.begin(begin_info);
    }

    void CommandBuffer::end() {
        m_command_buffer.end();
    }

    void CommandBuffer::reset() {
        m_command_buffer.reset();
    }

    void CommandBuffer::begin_render_pass(const RenderPass& render_pass, const FrameBuffer& frame_buffer,
        const std::vector<ClearValue>& clear_values) {
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
        LOG_TRACE("RenderPass: begin render pass with {} clear values", clear_values.size());
    }

    void CommandBuffer::end_render_pass() {
        m_command_buffer.endRenderPass();
    }

    void CommandBuffer::bind_pipeline(const Pipeline& pipeline) {
        m_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.get());
        LOG_TRACE("CommandBuffer: bind pipeline {}", pipeline.get_name());
    }

    void CommandBuffer::set_viewport(const vk::Viewport& viewport) {
        m_command_buffer.setViewport(0, 1, &viewport);
    }

    void CommandBuffer::set_scissor(const vk::Rect2D& scissor) {
        m_command_buffer.setScissor(0, 1, &scissor);
    }

    void CommandBuffer::copy_buffer(vk::Buffer src_buffer, vk::Buffer dst_buffer, size_t size, size_t src_offset, size_t dst_offset) {
        vk::BufferCopy copy_buffer{};
        copy_buffer.srcOffset = src_offset;
        copy_buffer.dstOffset = dst_offset;
        copy_buffer.size = size;
        m_command_buffer.copyBuffer(src_buffer, dst_buffer, 1, &copy_buffer);
    }

    void CommandBuffer::copy_buffer_to_image(vk::Buffer src_buffer, vk::Image dst_image, vk::ImageLayout dst_image_layout,
        const vk::Extent3D& extent, uint32_t base_array_layer, uint32_t layer_count, uint32_t mip_level) {
        vk::BufferImageCopy buffer_image_copy{};
        buffer_image_copy.bufferOffset = 0;
        buffer_image_copy.bufferRowLength = extent.width;
        buffer_image_copy.bufferImageHeight = extent.height;
        buffer_image_copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        buffer_image_copy.imageSubresource.mipLevel = mip_level;
        buffer_image_copy.imageSubresource.baseArrayLayer = base_array_layer;
        buffer_image_copy.imageSubresource.layerCount = layer_count;
        buffer_image_copy.imageOffset = vk::Offset3D{0, 0, 0};
        buffer_image_copy.imageExtent = extent;
        m_command_buffer.copyBufferToImage(src_buffer, dst_image, dst_image_layout, 1, &buffer_image_copy);
    }

    void CommandBuffer::transition_image_layout(vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
        uint32_t base_array_layer, uint32_t layer_count, uint32_t mip_level) {
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = mip_level;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = base_array_layer;
        barrier.subresourceRange.layerCount = layer_count;

        vk::PipelineStageFlags source_stage;
        vk::PipelineStageFlags destination_stage;

        if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferDstOptimal) {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            source_stage = vk::PipelineStageFlagBits::eTopOfPipe;
            destination_stage = vk::PipelineStageFlagBits::eTransfer;
        } else if (old_layout == vk::ImageLayout::eTransferDstOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            source_stage = vk::PipelineStageFlagBits::eTransfer;
            destination_stage = vk::PipelineStageFlagBits::eFragmentShader;
        } else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        m_command_buffer.pipelineBarrier(source_stage, destination_stage, {}, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void CommandBuffer::bind_vertex_buffer(std::span<const Buffer*> buffers, std::span<const vk::DeviceSize> offsets, uint32_t first_binding) {
        if(buffers.size() != offsets.size()) {
            LOG_FATAL("buffers and offsets must have the same size");
        }
        std::vector<vk::Buffer> vk_buffers;
        vk_buffers.reserve(buffers.size());
        for (const auto buf : buffers) {
            vk_buffers.push_back(buf->get());
        }
        m_command_buffer.bindVertexBuffers(first_binding, static_cast<uint32_t>(vk_buffers.size()),
            vk_buffers.data(), offsets.data());
    }

    void CommandBuffer::bind_index_buffer(const Buffer& buffer, vk::DeviceSize offset, vk::IndexType type) {
        m_command_buffer.bindIndexBuffer(buffer.get(), offset, type);
    }

    void CommandBuffer::push_constants(const PipelineLayout& layout, vk::ShaderStageFlags stage_flags,
        uint32_t offset, const void* data, size_t size) {
        m_command_buffer.pushConstants(layout.get(), stage_flags, offset, size, data);
    }

    void CommandBuffer::draw(const uint32_t vertex_count, const uint32_t instance_count, const uint32_t first_vertex,
                             const uint32_t first_instance) {
        m_command_buffer.draw(vertex_count, instance_count, first_vertex, first_instance);
    }
    void CommandBuffer::draw_indexed(const uint32_t index_count, const uint32_t instance_count,
        const uint32_t first_index, const int32_t vertex_offset, const uint32_t first_instance) {
        m_command_buffer.drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
    }


    CommandPool::CommandPool(Device* device, const uint32_t queue_family_index): m_device(device) {
        vk::CommandPoolCreateInfo pool_info = {};
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pool_info.queueFamilyIndex = queue_family_index;
        m_command_pool = m_device->get().createCommandPool(pool_info);
        LOG_INFO("Vulkan command pool created successfully");
    }

    CommandPool::~CommandPool() {
        m_device->get().destroyCommandPool(m_command_pool);
    }

    std::vector<CommandBuffer> CommandPool::allocate_command_buffers(const uint32_t count) const {
        std::vector<vk::CommandBuffer> cmd_buffers(count);
        vk::CommandBufferAllocateInfo allocate_info = {};
        allocate_info.commandPool = m_command_pool;
        allocate_info.commandBufferCount = count;
        allocate_info.level = vk::CommandBufferLevel::ePrimary;
        cmd_buffers = m_device->get().allocateCommandBuffers(allocate_info);
        std::vector<CommandBuffer> command_buffers;
        command_buffers.reserve(count);
        for (const auto& cmd_buffer : cmd_buffers) {
            command_buffers.emplace_back(CommandBuffer(cmd_buffer));
        }
        LOG_INFO("Vulkan command buffers allocated successfully (count: {})", count);
        return command_buffers;
    }

    CommandBuffer CommandPool::allocate_command_buffer() const {
        const auto buffer = allocate_command_buffers(1);
        return buffer[0];
    }
}
