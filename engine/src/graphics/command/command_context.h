#pragma once
#include "graphics/vk_common.h"
#include "graphics/command/command_buffer.h"
#include "graphics/synchronization/gpu_completion_point.h"
#include "graphics/synchronization/resource_state.h"

namespace Comet {
    class Device;
    class Buffer;
    class Image;

    class CommandContext {
    public:
        explicit CommandContext(Device& device);

        ~CommandContext();

        // 禁止拷贝
        CommandContext(const CommandContext&) = delete;

        CommandContext& operator=(const CommandContext&) = delete;

        // GPU 操作接口
        void copy_buffer(
            const Buffer& src,
            const Buffer& dst,
            size_t size,
            size_t src_offset = 0,
            size_t dst_offset = 0);

        void copy_buffer(
            vk::Buffer src,
            vk::Buffer dst,
            vk::DeviceSize size,
            vk::DeviceSize src_offset = 0,
            vk::DeviceSize dst_offset = 0);

        void copy_buffer_to_image(const Buffer& src, const Image& dst, ImageLayout dst_image_layout,
                                  const vk::Extent3D& extent, uint32_t base_array_layer = 0,
                                  uint32_t layer_count = 1, uint32_t mip_level = 0,
                                  vk::DeviceSize buffer_offset = 0);

        void copy_buffer_to_image(vk::Buffer src, vk::Image dst_image, ImageLayout dst_image_layout,
                                  const vk::Extent3D& extent, uint32_t base_array_layer = 0,
                                  uint32_t layer_count = 1, uint32_t mip_level = 0,
                                  vk::DeviceSize buffer_offset = 0);

        void transition_image_state(
            const Image& image,
            const ImageState& before,
            const ImageState& after);

        void transition_image_state(
            vk::Image image,
            const ImageState& before,
            const ImageState& after);

        void transition_buffer_state(
            const Buffer& buffer,
            const ResourceState& before,
            const ResourceState& after,
            vk::DeviceSize offset = 0,
            vk::DeviceSize size = VK_WHOLE_SIZE);

        [[nodiscard]] GpuCompletionPoint submit();

        void discard();

    private:
        Device& m_device;
        CommandBuffer m_command_buffer;
        bool m_is_recording = false;
        bool m_submitted = false;
    };
}
