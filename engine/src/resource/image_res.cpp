#include "image_res.h"
#include "../common/enum_convert.h"
#include "graphics/device.h"
#include "memory.h"

namespace Comet {
    ImageViewRes::ImageViewRes(Device& device, const Image& image, const ImageView::Descriptor& desc): m_image(image), m_device(device) {
        vk::ImageViewCreateInfo ci{};
        ci.format = format2VK(desc.format);
        ci.image = image.getRes().getVkImage();
        ci.viewType = imageViewType2VK(desc.viewType);
        vk::ImageSubresourceRange range{};
        range.aspectMask = imageAspect2VK(desc.subresourceRange.aspectMask);
        range.layerCount = desc.subresourceRange.layerCount;
        range.levelCount = desc.subresourceRange.levelCount;
        range.baseArrayLayer = desc.subresourceRange.baseArrayLayer;
        range.baseMipLevel = desc.subresourceRange.baseMipLevel;
        ci.subresourceRange = range;

        m_view = m_device.getVkDevice().createImageView(ci);
    }

    ImageViewRes::ImageViewRes(Device& device, vk::ImageView view): m_view(view), m_device(device) {}

    ImageViewRes::~ImageViewRes() {
        m_device.getVkDevice().destroyImageView(m_view);
    }

    void ImageViewRes::decrease() {
        Refcount::decrease();
        if(getCount() == 0) {
            // TODO:
            // m_device.m_pending_delete_image_views.push_back(this);
        }
    }

    ImageRes::ImageRes(const Adapter& adapter, Device& device, const Image::Descriptor& desc): m_device(device) {
        createImage(desc, device);
        allocMemomry(adapter.getPhysicalDevice());

        for(int i = 0; i < desc.arrayLayers; i++) {
            m_layouts.push_back(desc.initialLayout);
        }
        m_device.getVkDevice().bindImageMemory(m_image, m_memory_res->getMemory(), 0);
    }

    ImageRes::~ImageRes() {
        m_device.getVkDevice().destroyImage(m_image);
        delete m_memory_res;
    }

    Vec3i ImageRes::getExtent() const {
        const Vec3i extent = {m_create_info.extent.width, m_create_info.extent.height, m_create_info.extent.depth};
        return extent;
    }

    Flags<vk::ImageUsageFlagBits> ImageRes::getUsage() const {
        return Flags<vk::ImageUsageFlagBits>{static_cast<uint32_t>(m_create_info.usage)};
    }

    ImageView ImageRes::createView(const Image& image, const ImageView::Descriptor&) {
        // TODO
        // return m_device.CreateImageView(image, desc);
        return ImageView();
    }

    void ImageRes::decrease() {
        Refcount::decrease();
        if(getCount() == 0) {
            // TODO:
        }
    }

    void ImageRes::createImage(const Image::Descriptor& desc, Device& device) {
        vk::FormatFeatureFlags features{};
        const auto usage = desc.usage;
        if(usage & ImageUsage::Sampled) {
            features |= vk::FormatFeatureFlagBits::eSampledImage;
        }
        if(usage & ImageUsage::ColorAttachment) {
            features |= vk::FormatFeatureFlagBits::eColorAttachment;
        }
        if(usage & ImageUsage::CopyDst) {
            features |= vk::FormatFeatureFlagBits::eTransferDst;
        }
        if(usage & ImageUsage::CopySrc) {
            features |= vk::FormatFeatureFlagBits::eTransferSrc;
        }
        if(usage & ImageUsage::StorageBinding) {
            features |= vk::FormatFeatureFlagBits::eStorageImage;
        }
        if(usage & ImageUsage::DepthStencilAttachment) {
            features |= vk::FormatFeatureFlagBits::eDepthStencilAttachment;
        }

        // TODO: if format not support, return a similar format and report a warning
        findSupportedFormat(format2VK(desc.format), imageTiling2VK(desc.tiling), features);

        vk::ImageCreateInfo ci{};
        if(desc.is_cube_map) {
            ci.flags = vk::ImageCreateFlagBits::eCubeCompatible;
        }
        ci.imageType = imageType2VK(desc.imageType);
        ci.format = format2VK(desc.format);
        ci.extent = vk::Extent3D{static_cast<unsigned int>(desc.extent.x), static_cast<unsigned int>(desc.extent.y), static_cast<unsigned int>(desc.extent.z)};
        ci.mipLevels = desc.mipLevels;
        ci.arrayLayers = desc.arrayLayers;
        ci.samples = vk::SampleCountFlagBits{static_cast<uint32_t>(sampleCount2VK(desc.samples))};
        ci.tiling = imageTiling2VK(desc.tiling);
        ci.usage = imageUsage2VK(desc.usage);
        ci.sharingMode = sharingMode2VK(desc.sharingMode);
        ci.initialLayout = imageLayout2VK(desc.initialLayout);
        auto indices = device.getQueueFamilyIndices().getIndices();
        ci.pQueueFamilyIndices = indices.data();
        ci.queueFamilyIndexCount = indices.size();
        if(indices.size() > 1) {
            ci.sharingMode = vk::SharingMode::eConcurrent;
        } else {
            ci.sharingMode = vk::SharingMode::eExclusive;
        }
        m_create_info = ci;

        m_image = m_device.getVkDevice().createImage(ci);
    }

    void ImageRes::allocMemomry(vk::PhysicalDevice phyDevice) {}
    void ImageRes::findSupportedFormat(vk::Format candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {}
}
