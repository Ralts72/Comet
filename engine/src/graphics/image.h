#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;
    enum class ImageOwnership {
        Owned,    // 拥有资源，负责销毁
        Borrowed  // 借用资源，不负责销毁
    };

    struct ImageInfo {
        vk::Format format;
        vk::Extent3D extent;
        vk::ImageUsageFlags usage;
    };

    class Image {
    public:
        Image(Device* device, const ImageInfo& info, vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1);
        Image(Device* device, vk::Image image, const ImageInfo& info);
        ~Image();

        [[nodiscard]] ImageInfo get_info() const {return m_info;}
        [[nodiscard]] vk::Image get_image() const {return m_image;}
    private:
        vk::Image m_image;
        Device *m_device;
        ImageInfo m_info;
        vk::DeviceMemory m_memory;
        ImageOwnership m_owner;
    };

    inline bool is_depth_only_format(const vk::Format format) {
        return format == vk::Format::eD16Unorm || format == vk::Format::eD32Sfloat;
    }

    inline bool is_depth_stencil_format(const vk::Format format) {
        return is_depth_only_format(format)
               || format == vk::Format::eD16UnormS8Uint
               || format == vk::Format::eD24UnormS8Uint
               || format == vk::Format::eD32SfloatS8Uint;
    }
}