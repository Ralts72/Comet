#pragma once
#include "../common/enums.h"
#include "../common/flags.h"
#include "../pch.h"

namespace Comet {
    class Image;
    class ImageRes;
    class ImageViewRes;

    class ImageView {
    public:
        struct Descriptor {
            struct ImageSubresourceRange {
                Flags<ImageAspect> aspectMask = ImageAspect::None;
                uint32_t baseMipLevel = 0;
                uint32_t levelCount = 1;
                uint32_t baseArrayLayer = 0;
                uint32_t layerCount = 1;
            };

            ImageViewType viewType;
            Format format;
            ComponentMapping components;
            ImageSubresourceRange subresourceRange;
        };

        ImageView() = default;

        explicit ImageView(ImageViewRes*);

        ImageView(const ImageView&);

        ImageView(ImageView&&) noexcept;

        ImageView& operator=(const ImageView&) noexcept;

        ImageView& operator=(ImageView&&) noexcept;

        ~ImageView();

        Image getImage() const;

        const ImageViewRes& getRes() const noexcept { return *m_res; }

        ImageViewRes& getRes() noexcept { return *m_res; }

        operator bool() const noexcept { return m_res; }

        void release();

    private:
        ImageViewRes* m_res{};
    };

    class Image {
    public:
        struct Descriptor {
            bool is_cube_map = false;
            ImageType imageType = ImageType::Dim2;
            Format format = Format::R8G8B8A8_SRGB;
            Vec3i extent;
            uint32_t mipLevels = 1;
            uint32_t arrayLayers = 1;
            SampleCount samples = SampleCount::Count1;
            ImageTiling tiling = ImageTiling::Linear;
            Flags<ImageUsage> usage = ImageUsage::Sampled;
            SharingMode sharingMode = SharingMode::Concurrent;
            ImageLayout initialLayout = ImageLayout::Undefined;
        };

        Image() = default;

        explicit Image(ImageRes*);

        Image(const Image&);

        Image(Image&&) noexcept;

        Image& operator=(const Image&) noexcept;

        Image& operator=(Image&&) noexcept;

        ImageView createView(const ImageView::Descriptor&);

        ~Image();

        const ImageRes& getRes() const noexcept { return *m_res; }

        ImageRes& getRes() noexcept { return *m_res; }

        operator bool() const noexcept { return m_res; }

        void release();

    private:
        ImageRes* m_res{};
    };
}
