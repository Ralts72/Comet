#include "command_context.h"
#include "device.h"
#include "buffer.h"
#include "image.h"
#include "command_buffer.h"
#include "queue.h"
#include "common/logger.h"

namespace Comet {
    CommandContext::CommandContext(Device* device)
        : m_device(device),
          m_command_buffer(device->get_default_command_pool().allocate_command_buffer()) {
        // CommandBuffer 通过 CommandPool 分配
    }

    CommandContext::~CommandContext() {
        // CommandBuffer 由 CommandPool 管理，不需要手动释放
        // 但如果已经开始但未提交，需要结束
        if(m_is_recording && !m_submitted) {
            m_command_buffer.end();
            LOG_WARN("CommandContext destroyed without submitting commands");
        }
    }

    void CommandContext::copy_buffer(Buffer* src, Buffer* dst, size_t size) {
        copy_buffer(src->get(), dst->get(), size);
    }

    void CommandContext::copy_buffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) {
        if(!m_is_recording) {
            m_command_buffer.reset();
            m_command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            m_is_recording = true;
        }

        // 使用 CommandBuffer 的私有方法（需要 friend）
        // 由于 CommandBuffer 是 Device 的 friend，我们可以通过 Device 的 one_time_submit 模式
        // 但这里我们直接使用 CommandBuffer，因为它已经是 friend
        m_command_buffer.copy_buffer(src, dst, size);
    }

    void CommandContext::copy_buffer_to_image(Buffer* src, Image* dst, vk::ImageLayout dst_image_layout,
                                              const vk::Extent3D& extent, uint32_t base_array_layer,
                                              uint32_t layer_count, uint32_t mip_level) {
        copy_buffer_to_image(src->get(), dst->get(), dst_image_layout, extent, base_array_layer, layer_count, mip_level);
    }

    void CommandContext::copy_buffer_to_image(vk::Buffer src, vk::Image dst_image, vk::ImageLayout dst_image_layout,
                                              const vk::Extent3D& extent, uint32_t base_array_layer,
                                              uint32_t layer_count, uint32_t mip_level) {
        if(!m_is_recording) {
            m_command_buffer.reset();
            m_command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            m_is_recording = true;
        }

        m_command_buffer.copy_buffer_to_image(src, dst_image, dst_image_layout, extent, base_array_layer, layer_count, mip_level);
    }

    void CommandContext::transition_image_layout(Image* image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                                 uint32_t base_array_layer, uint32_t layer_count, uint32_t mip_level) {
        transition_image_layout(image->get(), old_layout, new_layout, base_array_layer, layer_count, mip_level);
    }

    void CommandContext::transition_image_layout(vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                                 uint32_t base_array_layer, uint32_t layer_count, uint32_t mip_level) {
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
        auto& graphics_queue = m_device->get_graphics_queue(0);
        graphics_queue.submit(std::span(&m_command_buffer, 1), {}, {}, {});

        // 等待完成
        graphics_queue.wait_idle();

        m_submitted = true;
    }

    void CommandContext::submit() {
        // 未来扩展：异步提交
        // 目前先使用 submit_and_wait
        submit_and_wait();
    }
}

