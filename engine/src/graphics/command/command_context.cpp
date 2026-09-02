#include "graphics/command/command_context.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/command/command_buffer.h"
#include "graphics/convert.h"
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

    void CommandContext::ensure_recording() {
        if(m_submitted) {
            LOG_FATAL("Cannot record commands after CommandContext submission");
        }
        if(m_is_recording) {
            return;
        }

        m_command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        m_is_recording = true;
    }

    void CommandContext::copy_buffer(
        const Buffer& src,
        const Buffer& dst,
        const size_t size,
        const size_t src_offset,
        const size_t dst_offset) {
        ensure_recording();

        m_command_buffer.copy_buffer(
            src.get(),
            dst.get(),
            size,
            src_offset,
            dst_offset);
    }

    void CommandContext::copy_buffer_to_image(const Buffer& src, const Image& dst, const ImageLayout dst_image_layout,
                                              const vk::Extent3D& extent, const uint32_t base_array_layer,
                                              const uint32_t layer_count, const uint32_t mip_level,
                                              const vk::DeviceSize buffer_offset) {
        ensure_recording();

        m_command_buffer.copy_buffer_to_image(
            src.get(),
            dst.get(),
            Graphics::image_layout_to_vk(dst_image_layout),
            extent,
            base_array_layer,
            layer_count,
            mip_level,
            buffer_offset);
    }

    void CommandContext::transition_image_state(
        const Image& image,
        const ImageState& before,
        const ImageState& after) {
        ensure_recording();

        m_command_buffer.transition_image_state(image.get(), before, after);
    }

    void CommandContext::transition_buffer_state(
        const Buffer& buffer,
        const ResourceState& before,
        const ResourceState& after,
        const vk::DeviceSize offset,
        const vk::DeviceSize size) {
        ensure_recording();

        m_command_buffer.transition_buffer_state(
            buffer.get(),
            before,
            after,
            offset,
            size);
    }

    GpuCompletionPoint CommandContext::submit() {
        if(!m_is_recording) {
            LOG_WARN("CommandContext::submit() called without any commands");
            return {};
        }

        if(m_submitted) {
            LOG_WARN("CommandContext already submitted");
            return {};
        }

        // 结束命令缓冲区
        m_command_buffer.end();

        // 提交到队列
        auto& graphics_queue = m_device.get_graphics_queue(0);
        const auto completion = graphics_queue.submit2(
            {},
            std::span(&m_command_buffer, 1),
            {},
            nullptr);

        m_submitted = true;
        return completion;
    }

    void CommandContext::discard() {
        if(m_submitted) {
            LOG_FATAL("Cannot discard a submitted CommandContext");
        }
        m_is_recording = false;
    }

}
