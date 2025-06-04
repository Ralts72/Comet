#pragma once
#include "refcount.h"
#include "../graphics/image.h"
#include "../pch.h"
#include "memory_res.h"

namespace Comet {
    class Device;
    class Adapter;
    class MemoryRes;

    class ImageViewRes: public Refcount {
    public:
        ImageViewRes(Device&, const Image& image, const ImageView::Descriptor&);

        // only for swapchain image view hack
        ImageViewRes(Device&, vk::ImageView);

        ImageViewRes(const ImageViewRes&) = delete;

        ImageViewRes(ImageViewRes&&) = delete;

        ImageViewRes& operator=(const ImageViewRes&) = delete;

        ImageViewRes& operator=(ImageViewRes&&) = delete;

        ~ImageViewRes() override;

        void decrease() override;

        [[nodiscard]] vk::ImageView getImageView() const { return m_view; }
        [[nodiscard]] Image getImage() const { return m_image; }

    private:
        vk::ImageView m_view;
        Image m_image;
        Device& m_device;
    };

    class ImageRes: public Refcount {
    public:
        ImageRes(const Adapter&, Device&, const Image::Descriptor&);

        ImageRes(const ImageRes&) = delete;

        ImageRes(ImageRes&&) = delete;

        ImageRes& operator=(const ImageRes&) = delete;

        ImageRes& operator=(ImageRes&&) = delete;

        ~ImageRes() override;

        vk::ImageType getType() const { return m_create_info.imageType; }

        Vec3i getExtent() const;

        vk::Format getFormat() const { return m_create_info.format; }

        uint32_t getMipLevelCount() const { return m_create_info.mipLevels; }

        vk::SampleCountFlags getSampleCount() const { return m_create_info.samples; }

        Flags<vk::ImageUsageFlagBits> getUsage() const;

        ImageView createView(const Image& image, const ImageView::Descriptor&);

        void decrease() override;

        vk::Image getVkImage() const { return m_image; }

        MemoryRes* getMemory() const { return m_memory_res; }
        std::vector<ImageLayout> getLayouts() const { return m_layouts; }

    private:
        void createImage(const Image::Descriptor&, Device&);

        void allocMemory(vk::PhysicalDevice phyDevice);

        void findSupportedFormat(vk::Format candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);

        vk::Image m_image;
        MemoryRes* m_memory_res{};
        std::vector<ImageLayout> m_layouts;
        Device& m_device;
        vk::ImageCreateInfo m_create_info;
    };
}
