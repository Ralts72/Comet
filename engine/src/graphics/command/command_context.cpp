#include "graphics/command/command_context.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/command/command_buffer.h"
#include "graphics/queue.h"
#include "diagnostics/logger.h"

namespace Comet {
    CommandContext::CommandContext(Device& device)
        : m_device(device),
          m_command_buffer(device.get_default_command_pool().allocate_command_buffer()) {
    }

    CommandContext::~CommandContext() {
        if(m_is_recording && !m_submitted) {
            LOG_WARN("CommandContext destroyed without submitting commands");
        }

        m_device.get_default_command_pool().free_command_buffer(m_command_buffer);
    }

    void CommandContext::copy_buffer(const Buffer& src, const Buffer& dst, const size_t size) {
        copy_buffer(src.get(), dst.get(), size);
    }

    void CommandContext::copy_buffer(const vk::Buffer src, const vk::Buffer dst, const vk::DeviceSize size) {
        if(!m_is_recording) {
            m_command_buffer.reset();
            m_command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            m_is_recording = true;
        }

        m_command_buffer.copy_buffer(src, dst, size);
    }

    void CommandContext::copy_buffer_to_image(const Buffer& src, const Image& dst, const vk::ImageLayout dst_image_layout,
                                              const vk::Extent3D& extent, const uint32_t base_array_layer,
                                              const uint32_t layer_count, const uint32_t mip_level) {
        copy_buffer_to_image(src.get(), dst.get(), dst_image_layout, extent, base_array_layer, layer_count, mip_level);
    }

    void CommandContext::copy_buffer_to_image(const vk::Buffer src, const vk::Image dst_image, const vk::ImageLayout dst_image_layout,
                                              const vk::Extent3D& extent, const uint32_t base_array_layer,
                                              const uint32_t layer_count, const uint32_t mip_level) {
        if(!m_is_recording) {
            m_command_buffer.reset();
            m_command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            m_is_recording = true;
        }

        m_command_buffer.copy_buffer_to_image(src, dst_image, dst_image_layout, extent, base_array_layer, layer_count, mip_level);
    }

    void CommandContext::transition_image_layout(const Image& image, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout,
                                                 const uint32_t base_array_layer, const uint32_t layer_count, const uint32_t mip_level) {
        transition_image_layout(image.get(), old_layout, new_layout, base_array_layer, layer_count, mip_level);
    }

    void CommandContext::transition_image_layout(const vk::Image image, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout,
                                                 const uint32_t base_array_layer, const uint32_t layer_count, const uint32_t mip_level) {
        if(!m_is_recording) {
            m_command_buffer.reset();
            m_command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            m_is_recording = true;
        }

        m_command_buffer.transition_image_layout(image, old_layout, new_layout, base_array_layer, layer_count, mip_level);
    }

    void CommandContext::submit_and_wait() {
        if(!m_is_recording) {
            LOG_WARN("CommandContext::submit_and_wait() called without any commands");
            return;
        }

        if(m_submitted) {
            LOG_WARN("CommandContext already submitted");
            return;
        }

        // 结束命令缓冲区
        m_command_buffer.end();

        // 提交到队列
        const auto& graphics_queue = m_device.get_graphics_queue(0);
        graphics_queue.submit2({}, std::span(&m_command_buffer, 1), {}, nullptr);

        // 等待完成
        graphics_queue.wait_idle();

        m_submitted = true;
    }

}
