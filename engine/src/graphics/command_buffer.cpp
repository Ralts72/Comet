#include "command_buffer.h"
#include "device.h"
#include "render_pass.h"
#include "core/logger/logger.h"
#include "frame_buffer.h"
#include "pipeline.h"

namespace Comet {
    void CommandBuffer::begin() {
        m_command_buffer.reset();
        vk::CommandBufferBeginInfo begin_info = {};
        begin_info.pInheritanceInfo = nullptr;
        m_command_buffer.begin(begin_info);
    }

    void CommandBuffer::end() {
        m_command_buffer.end();
    }

    void CommandBuffer::reset() {
        m_command_buffer.reset();
    }

    void CommandBuffer::begin_render_pass(const RenderPass& render_pass, const FrameBuffer& frame_buffer,
        const std::vector<vk::ClearValue>& clear_values) {
        vk::RenderPassBeginInfo render_pass_info = {};
        render_pass_info.renderPass = render_pass.get_render_pass();
        render_pass_info.framebuffer = frame_buffer.get_frame_buffer();
        vk::Rect2D render_area = {};
        render_area.extent.width = frame_buffer.get_width();
        render_area.extent.height = frame_buffer.get_height();
        render_area.offset.x = 0;
        render_area.offset.y = 0;
        render_pass_info.renderArea = render_area;
        render_pass_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
        render_pass_info.pClearValues = clear_values.data();

        m_command_buffer.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
        LOG_TRACE("RenderPass: begin render pass with {} clear values", clear_values.size());
    }

    void CommandBuffer::end_render_pass() {
        m_command_buffer.endRenderPass();
    }

    void CommandBuffer::bind_pipeline(const Pipeline& pipeline) {
        m_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.get_pipeline());
        LOG_TRACE("CommandBuffer: bind pipeline {}", pipeline.get_name());
    }

    void CommandBuffer::set_viewport(const vk::Viewport& viewport) {
        m_command_buffer.setViewport(0, 1, &viewport);
    }

    void CommandBuffer::set_scissor(const vk::Rect2D& scissor) {
        m_command_buffer.setScissor(0, 1, &scissor);
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
        m_command_pool = m_device->get_device().createCommandPool(pool_info);
        LOG_INFO("Vulkan command pool created successfully");
    }

    CommandPool::~CommandPool() {
        m_device->get_device().destroyCommandPool(m_command_pool);
    }

    std::vector<CommandBuffer> CommandPool::allocate_command_buffers(const uint32_t count) const {
        std::vector<vk::CommandBuffer> cmd_buffers(count);
        vk::CommandBufferAllocateInfo allocate_info = {};
        allocate_info.commandPool = m_command_pool;
        allocate_info.commandBufferCount = count;
        allocate_info.level = vk::CommandBufferLevel::ePrimary;
        cmd_buffers = m_device->get_device().allocateCommandBuffers(allocate_info);
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
