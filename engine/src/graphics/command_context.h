#pragma once
#include "vk_common.h"
#include "command_buffer.h"
#include <memory>

namespace Comet {
    class Device;
    class Buffer;
    class Image;

    class CommandContext {
    public:
        explicit CommandContext(Device* device);

        ~CommandContext();

        // 禁止拷贝
        CommandContext(const CommandContext&) = delete;

        CommandContext& operator=(const CommandContext&) = delete;

        // GPU 操作接口
        void copy_buffer(const Buffer* src, const Buffer* dst, size_t size);

        void copy_buffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size);

        void copy_buffer_to_image(const Buffer* src, const Image* dst, vk::ImageLayout dst_image_layout,
                                  const vk::Extent3D& extent, uint32_t base_array_layer = 0,
                                  uint32_t layer_count = 1, uint32_t mip_level = 0);

        void copy_buffer_to_image(vk::Buffer src, vk::Image dst_image, vk::ImageLayout dst_image_layout,
                                  const vk::Extent3D& extent, uint32_t base_array_layer = 0,
                                  uint32_t layer_count = 1, uint32_t mip_level = 0);

        void transition_image_layout(const Image* image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                     uint32_t base_array_layer = 0, uint32_t layer_count = 1, uint32_t mip_level = 0);

        void transition_image_layout(vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                     uint32_t base_array_layer = 0, uint32_t layer_count = 1, uint32_t mip_level = 0);

        // 提交和同步
        void submit_and_wait(); // 立即提交并等待完成（用于资源上传）
        void submit(); // 异步提交（未来扩展）

    private:
        Device* m_device;
        CommandBuffer m_command_buffer;
        bool m_is_recording = false;
        bool m_submitted = false;
    };
}

