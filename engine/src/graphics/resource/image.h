#pragma once

#include "graphics/resource/allocator.h"
#include "graphics/vk_common.h"
#include "common/export.h"

#include <string_view>

namespace Comet {
    class Device;

    struct ImageInfo {
        Format format;
        Math::Vec3u extent;
        Flags<ImageUsage> usage;
    };

    class COMET_API Image {
    public:
        virtual ~Image() = default;

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
        Image(Image&&) noexcept = delete;
        Image& operator=(Image&&) noexcept = delete;

        static std::shared_ptr<Image> create(
            Device& device,
            const ImageInfo& info,
            SampleCount sample_count = SampleCount::Count1,
            std::string_view debug_name = {});

        static GpuResourceResult<std::shared_ptr<Image>> try_create(
            Device& device,
            const ImageInfo& info,
            bool within_budget,
            SampleCount sample_count = SampleCount::Count1,
            std::string_view debug_name = {});

        static std::shared_ptr<Image> wrap(Device& device, vk::Image image, const ImageInfo& info);

        [[nodiscard]] ImageInfo get_info() const { return m_info; }
        [[nodiscard]] vk::Image get() const { return m_image; }

    protected:
        Image(Device& device, const ImageInfo& info);

        vk::Image m_image = VK_NULL_HANDLE;
        Device& m_device;
        ImageInfo m_info;
    };

    class COMET_API OwnedImage final: public Image {
    public:
        ~OwnedImage() override;

    private:
        friend class Image;

        OwnedImage(Device& device, const ImageInfo& info,
                   Allocator::ImageAllocation allocation);

        Allocation m_allocation;
    };

    class COMET_API BorrowedImage final: public Image {
    public:
        BorrowedImage(Device& device, vk::Image image, const ImageInfo& info);

        ~BorrowedImage() override = default;
    };
}
