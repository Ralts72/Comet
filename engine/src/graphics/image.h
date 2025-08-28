#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;

    struct ImageInfo {
        Format format;
        // vk::Extent3D extent;
        Math::Vec3u extent;
        Flags<ImageUsage> usage;
        // vk::ImageUsageFlags usage;
    };

    class Image {
    public:
        enum class Ownership { Owned, Borrowed };
        virtual ~Image() = default;

        static std::shared_ptr<Image> create_owned_image(Device* device, const ImageInfo& info, SampleCount sample_count = SampleCount::Count1);
        static std::shared_ptr<Image> create_borrowed_image(Device* device, vk::Image image, const ImageInfo& info);

        [[nodiscard]] ImageInfo get_info() const { return m_info; }
        [[nodiscard]] vk::Image get() const { return m_image; }
        [[nodiscard]] Ownership get_type() const { return m_type; }

    protected:
        Image(Device* device, const ImageInfo& info, Ownership type);

        vk::Image m_image;
        Device* m_device;
        ImageInfo m_info;
        Ownership m_type;
    };

    class OwnedImage final: public Image {
    public:
        OwnedImage(Device* device, const ImageInfo& info,SampleCount sample_count =SampleCount::Count1);
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