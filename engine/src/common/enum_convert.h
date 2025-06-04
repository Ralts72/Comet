#pragma once
#include "enums.h"
#include "flags.h"
#include "../pch.h"

namespace Comet {
    inline vk::Filter filter2VK(Filter filter);

    inline vk::SamplerAddressMode samplerAddressMode2VK(SamplerAddressMode mode);

    inline vk::CompareOp compareOp2VK(CompareOp op);

    inline vk::ImageType imageType2VK(ImageType type);

    inline vk::SampleCountFlags sampleCount2VK(SampleCount count);

    inline vk::Format format2VK(Format format);

    inline Format VkFormat2Format(vk::Format format);

    inline vk::ImageViewType imageViewType2VK(ImageViewType type);

    inline vk::PrimitiveTopology primitiveTopology2VK(Topology topology);

    inline vk::PolygonMode polygonMode2VK(PolygonMode mode);

    inline vk::FrontFace frontFace2VK(FrontFace face);

    inline vk::BlendOp blendOp2VK(BlendOp op);

    inline vk::BlendFactor blendFactor2VK(BlendFactor factor);

    inline vk::StencilOp stencilOp2VK(StencilOp op);

    inline vk::AttachmentLoadOp attachmentLoadOp2VK(AttachmentLoadOp op);

    inline vk::AttachmentStoreOp attachmentStoreOp2VK(AttachmentStoreOp op);

    inline vk::IndexType indexType2VK(IndexType type);

    inline vk::Format vertexFormat2VK(VertexFormat format);

    inline vk::ShaderStageFlags shaderStage2VK(Flags<ShaderStage> flags);

    inline vk::BorderColor borderColor2VK(BorderColor color);

    inline vk::SamplerMipmapMode samplerMipmapMode2VK(SamplerMipmapMode mode);

    inline vk::DescriptorType bindGroupEntryType2VK(BindGroupEntryType type);

    inline vk::ImageAspectFlags imageAspect2VK(Flags<ImageAspect> flags);

    inline vk::BufferUsageFlags bufferUsage2VK(Flags<BufferUsage> flags);

    inline vk::ImageUsageFlags imageUsage2VK(Flags<ImageUsage> flags);

    inline vk::ColorComponentFlags colorWriteMask2VK(Flags<ColorWriteMask> flags);

    inline vk::CullModeFlags cullMode2VK(Flags<CullMode> mode);

    inline vk::PipelineStageFlags pipelineStage2VK(Flags<PipelineStage> flags);

    inline vk::AccessFlags access2VK(Flags<Access> flags);

    inline vk::DependencyFlags dependency2VK(Flags<Dependency> flags);

    inline vk::ImageLayout imageLayout2VK(ImageLayout layout);

    inline vk::PipelineBindPoint pipelineBindPoint2VK(PipelineBindPoint point);

    inline vk::ImageTiling imageTiling2VK(ImageTiling tiling);

    inline vk::SharingMode sharingMode2VK(SharingMode mode);

    inline vk::ComponentSwizzle componentMapping2VK(ComponentMapping swizzle);

    inline vk::ColorComponentFlags componentMask2VK(Flags<ColorComponent> falgs);

    inline vk::ColorSpaceKHR imageColorSpace2VK(ImageColorSpace space);

    inline ImageColorSpace VkColorSpace2ImageColorSpace(vk::ColorSpaceKHR space);

    inline vk::MemoryPropertyFlags memoryProperty2VK(MemoryType property);

    inline std::optional<uint32_t> findMemoryType(vk::PhysicalDevice physicalDevice, vk::MemoryRequirements requirements, vk::MemoryPropertyFlags properties);
}
