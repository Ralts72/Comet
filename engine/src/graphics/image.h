#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;

    struct ImageInfo {
        Format format;
        Math::Vec3u extent;
        Flags<ImageUsage> usage;
    };

    class Image {
    public:
        virtual ~Image() = default;

        static std::shared_ptr<Image> create(Device* device, const ImageInfo& info, SampleCount sample_count = SampleCount::Count1);

        static std::shared_ptr<Image> wrap(Device* device, vk::Image image, const ImageInfo& info);

        [[nodiscard]] ImageInfo get_info() const { return m_info; }
        [[nodiscard]] vk::Image get() const { return m_image; }

    protected:
        Image(Device* device, const ImageInfo& info);

        vk::Image m_image;
        Device* m_device;
        ImageInfo m_info;
    };

    class OwnedImage final: public Image {
    public:
        OwnedImage(Device* device, const ImageInfo& info, SampleCount sample_count = SampleCount::Count1);

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
