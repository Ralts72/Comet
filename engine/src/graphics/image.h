#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;
    enum class ImageType {
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
        virtual ~Image() = default;

        static std::shared_ptr<Image> create_owned_image(Device* device, const ImageInfo& info, vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1);
        static std::shared_ptr<Image> create_borrowed_image(Device* device, vk::Image image, const ImageInfo& info);

        [[nodiscard]] ImageInfo get_info() const { return m_info; }
        [[nodiscard]] vk::Image get() const { return m_image; }
        [[nodiscard]] ImageType get_type() const { return m_type; }

    protected:
        Image(Device* device, const ImageInfo& info, const ImageType type);

        vk::Image m_image;
        Device* m_device;
        ImageInfo m_info;
        ImageType m_type;
    };

    class OwnedImage final: public Image {
    public:
        OwnedImage(Device* device, const ImageInfo& info, vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1);
        ~OwnedImage() override;
    private:
        vk::DeviceMemory m_memory;
    };

    class BorrowedImage final: public Image {
    public:
        BorrowedImage(Device* device, vk::Image image, const ImageInfo& info);

        ~BorrowedImage() override = default;
    };
}