#include "enum_convert.h"
#include "macro.h"

namespace Comet {
#define CASE(a, b) \
    case (a):      \
        return (b);

#define TRY_SET_BIT(src, dst) \
    if (flags & (src)) bits |= (dst);

    vk::Filter filter2VK(Filter filter) {
        switch(filter) {
            CASE(Filter::Nearest, vk::Filter::eNearest)
            CASE(Filter::Linear, vk::Filter::eLinear)
        }
        CANT_REACH();
        return {};
    }

    vk::SamplerAddressMode samplerAddressMode2VK(SamplerAddressMode mode) {
        switch(mode) {
            CASE(SamplerAddressMode::ClampToEdge, vk::SamplerAddressMode::eClampToEdge)
            CASE(SamplerAddressMode::Repeat, vk::SamplerAddressMode::eRepeat)
            CASE(SamplerAddressMode::MirrorRepeat, vk::SamplerAddressMode::eMirroredRepeat);
        }
        CANT_REACH();
        return {};
    }

    vk::CompareOp compareOp2VK(CompareOp op) {
        switch(op) {
            CASE(CompareOp::Never, vk::CompareOp::eNever)
            CASE(CompareOp::Less, vk::CompareOp::eLess)
            CASE(CompareOp::Equal, vk::CompareOp::eEqual)
            CASE(CompareOp::LessEqual, vk::CompareOp::eLessOrEqual)
            CASE(CompareOp::Greater, vk::CompareOp::eGreater)
            CASE(CompareOp::NotEqual, vk::CompareOp::eNotEqual)
            CASE(CompareOp::GreaterEqual, vk::CompareOp::eGreaterOrEqual)
            CASE(CompareOp::Always, vk::CompareOp::eAlways)
        }
        CANT_REACH();
        return {};
    }

    vk::ImageType imageType2VK(ImageType type) {
        switch(type) {
            CASE(ImageType::Dim1, vk::ImageType::e1D);
            CASE(ImageType::Dim2, vk::ImageType::e2D);
            CASE(ImageType::Dim3, vk::ImageType::e3D);
        }
        CANT_REACH();
        return {};
    }

    vk::SampleCountFlags sampleCount2VK(SampleCount count) {
        switch(count) {
            CASE(SampleCount::Count1, vk::SampleCountFlagBits::e1);
            CASE(SampleCount::Count2, vk::SampleCountFlagBits::e2);
            CASE(SampleCount::Count4, vk::SampleCountFlagBits::e4);
            CASE(SampleCount::Count8, vk::SampleCountFlagBits::e8);
            CASE(SampleCount::Count16, vk::SampleCountFlagBits::e16);
            CASE(SampleCount::Count32, vk::SampleCountFlagBits::e32);
            CASE(SampleCount::Count64, vk::SampleCountFlagBits::e64);
        }
        CANT_REACH();
        return {};
    }

    vk::Format format2VK(Format format) {
        return static_cast<vk::Format>(format);
    }

    Format VkFormat2Format(vk::Format format) {
        return static_cast<Format>(format);
    }

    vk::ImageViewType imageViewType2VK(ImageViewType type) {
        switch(type) {
            CASE(ImageViewType::Dim1, vk::ImageViewType::e1D);
            CASE(ImageViewType::Dim2, vk::ImageViewType::e2D);
            CASE(ImageViewType::Dim3, vk::ImageViewType::e3D);
            CASE(ImageViewType::Dim2Array, vk::ImageViewType::e2DArray);
            CASE(ImageViewType::Cube, vk::ImageViewType::eCube);
            CASE(ImageViewType::CubeArray, vk::ImageViewType::eCubeArray);
        }
        CANT_REACH();
        return {};
    }

    vk::PrimitiveTopology primitiveTopology2VK(Topology topology) {
        switch(topology) {
            CASE(Topology::LineList, vk::PrimitiveTopology::eLineList);
            CASE(Topology::LineStrip, vk::PrimitiveTopology::eLineStrip);
            CASE(Topology::PointList, vk::PrimitiveTopology::ePointList);
            CASE(Topology::TriangleList, vk::PrimitiveTopology::eTriangleList);
            CASE(Topology::TriangleStrip, vk::PrimitiveTopology::eTriangleStrip);
            CASE(Topology::TriangleFan, vk::PrimitiveTopology::eTriangleFan);
        }
        CANT_REACH();
        return {};
    }

    vk::PolygonMode polygonMode2VK(PolygonMode mode) {
        switch(mode) {
            CASE(PolygonMode::Line, vk::PolygonMode::eLine);
            CASE(PolygonMode::Fill, vk::PolygonMode::eFill);
            CASE(PolygonMode::Point, vk::PolygonMode::ePoint);
        }
        CANT_REACH();
        return {};
    }

    vk::FrontFace frontFace2VK(FrontFace face) {
        switch(face) {
            CASE(FrontFace::CCW, vk::FrontFace::eCounterClockwise);
            CASE(FrontFace::CW, vk::FrontFace::eClockwise);
        }
        CANT_REACH();
        return {};
    }

    vk::BlendOp blendOp2VK(BlendOp op) {
        switch(op) {
            CASE(BlendOp::Add, vk::BlendOp::eAdd);
            CASE(BlendOp::Subtract, vk::BlendOp::eSubtract);
            CASE(BlendOp::ReverseSubtract, vk::BlendOp::eReverseSubtract);
            CASE(BlendOp::Min, vk::BlendOp::eMin);
            CASE(BlendOp::Max, vk::BlendOp::eMax);
        }
        CANT_REACH();
        return {};
    }

    vk::BlendFactor blendFactor2VK(BlendFactor factor) {
        switch(factor) {
            CASE(BlendFactor::Zero, vk::BlendFactor::eZero);
            CASE(BlendFactor::One, vk::BlendFactor::eOne);
            CASE(BlendFactor::SrcColor, vk::BlendFactor::eSrcColor);
            CASE(BlendFactor::OneMinusSrcColor, vk::BlendFactor::eOneMinusSrcColor);
            CASE(BlendFactor::DstColor, vk::BlendFactor::eDstColor);
            CASE(BlendFactor::OneMinusDstColor, vk::BlendFactor::eOneMinusDstColor);
            CASE(BlendFactor::SrcAlpha, vk::BlendFactor::eSrcAlpha);
            CASE(BlendFactor::OneMinusSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha);
            CASE(BlendFactor::DstAlpha, vk::BlendFactor::eDstAlpha);
            CASE(BlendFactor::OneMinusDstAlpha, vk::BlendFactor::eOneMinusDstAlpha);
            CASE(BlendFactor::ConstantColor, vk::BlendFactor::eConstantColor);
            CASE(BlendFactor::OneMinusConstantColor, vk::BlendFactor::eOneMinusConstantColor);
            CASE(BlendFactor::ConstantAlpha, vk::BlendFactor::eConstantAlpha);
            CASE(BlendFactor::OneMinusConstantAlpha, vk::BlendFactor::eOneMinusConstantAlpha);
            CASE(BlendFactor::SrcAlphaSaturate, vk::BlendFactor::eSrcAlphaSaturate);
        }
        CANT_REACH();
        return {};
    }

    vk::StencilOp stencilOp2VK(StencilOp op) {
        switch(op) {
            CASE(StencilOp::Keep, vk::StencilOp::eKeep);
            CASE(StencilOp::Zero, vk::StencilOp::eZero);
            CASE(StencilOp::Replace, vk::StencilOp::eReplace);
            CASE(StencilOp::IncrementAndClamp, vk::StencilOp::eIncrementAndClamp);
            CASE(StencilOp::DecrementAndClamp, vk::StencilOp::eDecrementAndClamp);
            CASE(StencilOp::Invert, vk::StencilOp::eInvert);
            CASE(StencilOp::IncrementAndWrap, vk::StencilOp::eIncrementAndWrap);
            CASE(StencilOp::DecrementAndWrap, vk::StencilOp::eDecrementAndWrap);
        }
        CANT_REACH();
        return {};
    }

    vk::AttachmentLoadOp attachmentLoadOp2VK(AttachmentLoadOp op) {
        switch(op) {
            CASE(AttachmentLoadOp::Clear, vk::AttachmentLoadOp::eClear);
            CASE(AttachmentLoadOp::Load, vk::AttachmentLoadOp::eLoad);
            CASE(AttachmentLoadOp::DontCare, vk::AttachmentLoadOp::eDontCare);
        }
        CANT_REACH();
        return {};
    }

    vk::AttachmentStoreOp attachmentStoreOp2VK(AttachmentStoreOp op) {
        switch(op) {
            CASE(AttachmentStoreOp::Store, vk::AttachmentStoreOp::eStore);
            CASE(AttachmentStoreOp::DontCare, vk::AttachmentStoreOp::eDontCare);
        }
        CANT_REACH();
        return {};
    }

    vk::IndexType indexType2VK(IndexType type) {
        switch(type) {
            CASE(IndexType::Uint16, vk::IndexType::eUint16);
            CASE(IndexType::Uint32, vk::IndexType::eUint32);
        }
        CANT_REACH();
        return {};
    }

    vk::Format vertexFormat2VK(VertexFormat format) {
        switch(format) {
            CASE(VertexFormat::Uint8x2, vk::Format::eR8G8Uint);
            CASE(VertexFormat::Uint8x4, vk::Format::eR8G8B8A8Uint);
            CASE(VertexFormat::Sint8x2, vk::Format::eR8G8Sint);
            CASE(VertexFormat::Sint8x4, vk::Format::eR8G8B8A8Sint);
            CASE(VertexFormat::Unorm8x2, vk::Format::eR8G8Unorm);
            CASE(VertexFormat::Unorm8x4, vk::Format::eR8G8B8A8Unorm);
            CASE(VertexFormat::Snorm8x2, vk::Format::eR8G8Snorm);
            CASE(VertexFormat::Snorm8x4, vk::Format::eR8G8B8A8Snorm);
            CASE(VertexFormat::Uint16x2, vk::Format::eR16G16Uint);
            CASE(VertexFormat::Uint16x4, vk::Format::eR16G16B16A16Uint);
            CASE(VertexFormat::Sint16x2, vk::Format::eR16G16Sint);
            CASE(VertexFormat::Sint16x4, vk::Format::eR16G16B16A16Sint);
            CASE(VertexFormat::Unorm16x2, vk::Format::eR16G16Unorm);
            CASE(VertexFormat::Unorm16x4, vk::Format::eR16G16B16A16Unorm);
            CASE(VertexFormat::Snorm16x2, vk::Format::eR16G16Snorm);
            CASE(VertexFormat::Snorm16x4, vk::Format::eR16G16B16A16Snorm);
            CASE(VertexFormat::Float16x2, vk::Format::eR16G16Sfloat);
            CASE(VertexFormat::Float16x4, vk::Format::eR16G16B16A16Sfloat);
            CASE(VertexFormat::Float32, vk::Format::eR32Sfloat);
            CASE(VertexFormat::Float32x2, vk::Format::eR32G32Sfloat);
            CASE(VertexFormat::Float32x3, vk::Format::eR32G32B32Sfloat);
            CASE(VertexFormat::Float32x4, vk::Format::eR32G32B32A32Sfloat);
            CASE(VertexFormat::Uint32, vk::Format::eR32Uint);
            CASE(VertexFormat::Uint32x2, vk::Format::eR32G32Uint);
            CASE(VertexFormat::Uint32x3, vk::Format::eR32G32B32Uint);
            CASE(VertexFormat::Uint32x4, vk::Format::eR32G32B32A32Uint);
            CASE(VertexFormat::Sint32, vk::Format::eR32Sint);
            CASE(VertexFormat::Sint32x2, vk::Format::eR32G32Sint);
            CASE(VertexFormat::Sint32x3, vk::Format::eR32G32B32Sint);
            CASE(VertexFormat::Sint32x4, vk::Format::eR32G32B32A32Sint);
        }
        CANT_REACH();
        return {};
    }

    vk::ShaderStageFlags shaderStage2VK(Flags<ShaderStage> flags) {
        if(flags == ShaderStage::All) {
            return vk::ShaderStageFlagBits::eAll;
        }
        if(flags == ShaderStage::AllGraphics) {
            return vk::ShaderStageFlagBits::eAllGraphics;
        }
        vk::ShaderStageFlags bits{};
        TRY_SET_BIT(ShaderStage::Vertex, vk::ShaderStageFlagBits::eVertex)
        TRY_SET_BIT(ShaderStage::TessellationControl, vk::ShaderStageFlagBits::eTessellationControl)
        TRY_SET_BIT(ShaderStage::TessellationEvaluation, vk::ShaderStageFlagBits::eTessellationEvaluation)
        TRY_SET_BIT(ShaderStage::Geometry, vk::ShaderStageFlagBits::eGeometry)
        TRY_SET_BIT(ShaderStage::Fragment, vk::ShaderStageFlagBits::eFragment)
        TRY_SET_BIT(ShaderStage::Compute, vk::ShaderStageFlagBits::eCompute)
        return bits;
    }

    vk::BorderColor borderColor2VK(BorderColor color) {
        switch(color) {
            CASE(BorderColor::FloatTransparentBlack, vk::BorderColor::eFloatTransparentBlack);
            CASE(BorderColor::IntTransparentBlack, vk::BorderColor::eIntTransparentBlack);
            CASE(BorderColor::FloatOpaqueBlack, vk::BorderColor::eFloatOpaqueBlack);
            CASE(BorderColor::IntOpaqueBlack, vk::BorderColor::eIntOpaqueBlack);
            CASE(BorderColor::FloatOpaqueWhite, vk::BorderColor::eFloatOpaqueWhite);
            CASE(BorderColor::IntOpaqueWhite, vk::BorderColor::eIntOpaqueWhite);
        }
        CANT_REACH();
        return {};
    }

    vk::SamplerMipmapMode samplerMipmapMode2VK(SamplerMipmapMode mode) {
        switch(mode) {
            CASE(SamplerMipmapMode::Nearest, vk::SamplerMipmapMode::eNearest);
            CASE(SamplerMipmapMode::Linear, vk::SamplerMipmapMode::eLinear);
        }
        CANT_REACH();
        return {};
    }

    vk::DescriptorType bindGroupEntryType2VK(BindGroupEntryType type) {
        switch(type) {
            CASE(BindGroupEntryType::Sampler, vk::DescriptorType::eSampler)
            CASE(BindGroupEntryType::CombinedImageSampler, vk::DescriptorType::eCombinedImageSampler)
            CASE(BindGroupEntryType::SampledImage, vk::DescriptorType::eSampledImage)
            CASE(BindGroupEntryType::StorageImage, vk::DescriptorType::eStorageImage)
            CASE(BindGroupEntryType::UniformTexelBuffer, vk::DescriptorType::eUniformTexelBuffer)
            CASE(BindGroupEntryType::StorageTexelBuffer, vk::DescriptorType::eStorageTexelBuffer)
            CASE(BindGroupEntryType::UniformBuffer, vk::DescriptorType::eUniformBuffer)
            CASE(BindGroupEntryType::StorageBuffer, vk::DescriptorType::eStorageBuffer)
            CASE(BindGroupEntryType::UniformBufferDynamic, vk::DescriptorType::eUniformBufferDynamic)
            CASE(BindGroupEntryType::StoragesBufferDynamic, vk::DescriptorType::eStorageBufferDynamic)
            CASE(BindGroupEntryType::InputAttachment, vk::DescriptorType::eInputAttachment)
            CASE(BindGroupEntryType::InlineUniformBlock, vk::DescriptorType::eUniformBufferDynamic)
        }
        CANT_REACH();
        return {};
    }

    vk::ImageAspectFlags imageAspect2VK(Flags<ImageAspect> flags) {
        vk::ImageAspectFlags bits{};
        TRY_SET_BIT(ImageAspect::Color, vk::ImageAspectFlagBits::eColor)
        TRY_SET_BIT(ImageAspect::Depth, vk::ImageAspectFlagBits::eDepth)
        TRY_SET_BIT(ImageAspect::Stencil, vk::ImageAspectFlagBits::eStencil)
        return bits;
    }

    vk::BufferUsageFlags bufferUsage2VK(Flags<BufferUsage> flags) {
        vk::BufferUsageFlags bits{};
        TRY_SET_BIT(BufferUsage::CopySrc, vk::BufferUsageFlagBits::eTransferSrc)
        TRY_SET_BIT(BufferUsage::CopyDst, vk::BufferUsageFlagBits::eTransferDst)
        TRY_SET_BIT(BufferUsage::Vertex, vk::BufferUsageFlagBits::eVertexBuffer)
        TRY_SET_BIT(BufferUsage::Index, vk::BufferUsageFlagBits::eIndexBuffer)
        TRY_SET_BIT(BufferUsage::Uniform, vk::BufferUsageFlagBits::eUniformBuffer)
        TRY_SET_BIT(BufferUsage::Storage, vk::BufferUsageFlagBits::eStorageBuffer)
        return bits;
    }

    vk::ImageUsageFlags imageUsage2VK(Flags<ImageUsage> flags) {
        vk::ImageUsageFlags bits{};
        TRY_SET_BIT(ImageUsage::Sampled, vk::ImageUsageFlagBits::eSampled)
        TRY_SET_BIT(ImageUsage::CopySrc, vk::ImageUsageFlagBits::eTransferSrc)
        TRY_SET_BIT(ImageUsage::CopyDst, vk::ImageUsageFlagBits::eTransferDst)
        TRY_SET_BIT(ImageUsage::StorageBinding, vk::ImageUsageFlagBits::eStorage)
        TRY_SET_BIT(ImageUsage::ColorAttachment, vk::ImageUsageFlagBits::eColorAttachment)
        TRY_SET_BIT(ImageUsage::DepthStencilAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment)
        return bits;
    }

    vk::ColorComponentFlags colorWriteMask2VK(Flags<ColorWriteMask> flags) {
        if(flags == ColorWriteMask::All) {
            return vk::ColorComponentFlagBits::eA |
                   vk::ColorComponentFlagBits::eB |
                   vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eR;
        }
        vk::ColorComponentFlags bits{};
        TRY_SET_BIT(ColorWriteMask::Alpha, vk::ColorComponentFlagBits::eA)
        TRY_SET_BIT(ColorWriteMask::Green, vk::ColorComponentFlagBits::eG)
        TRY_SET_BIT(ColorWriteMask::Blue, vk::ColorComponentFlagBits::eB)
        TRY_SET_BIT(ColorWriteMask::Red, vk::ColorComponentFlagBits::eR)
        return bits;
    }

    vk::CullModeFlags cullMode2VK(Flags<CullMode> mode) {
        if(mode == CullMode::None) {
            return vk::CullModeFlagBits::eNone;
        }
        if((mode & CullMode::Front) && (mode & CullMode::Back)) {
            return vk::CullModeFlagBits::eFrontAndBack;
        }
        if(mode & CullMode::Front) {
            return vk::CullModeFlagBits::eFront;
        }
        if(mode & CullMode::Back) {
            return vk::CullModeFlagBits::eBack;
        }
        CANT_REACH();
        return {};
    }

    vk::PipelineStageFlags pipelineStage2VK(Flags<PipelineStage> flags) {
        vk::PipelineStageFlags bits{};
        if(flags == PipelineStage::None) {
            return vk::PipelineStageFlagBits::eNone;
        }
        TRY_SET_BIT(PipelineStage::Host, vk::PipelineStageFlagBits::eHost)
        TRY_SET_BIT(PipelineStage::Transfer, vk::PipelineStageFlagBits::eTransfer)
        TRY_SET_BIT(PipelineStage::AllCommands, vk::PipelineStageFlagBits::eAllCommands)
        TRY_SET_BIT(PipelineStage::AllGraphics, vk::PipelineStageFlagBits::eAllGraphics)
        TRY_SET_BIT(PipelineStage::ComputeShader, vk::PipelineStageFlagBits::eComputeShader)
        TRY_SET_BIT(PipelineStage::DrawIndirect, vk::PipelineStageFlagBits::eDrawIndirect)
        TRY_SET_BIT(PipelineStage::FragmentShader, vk::PipelineStageFlagBits::eFragmentShader)
        TRY_SET_BIT(PipelineStage::VertexInput, vk::PipelineStageFlagBits::eVertexInput)
        TRY_SET_BIT(PipelineStage::VertexShader, vk::PipelineStageFlagBits::eVertexShader)
        TRY_SET_BIT(PipelineStage::GeometryShader, vk::PipelineStageFlagBits::eGeometryShader)
        TRY_SET_BIT(PipelineStage::BottomOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe)
        TRY_SET_BIT(PipelineStage::ColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput)
        TRY_SET_BIT(PipelineStage::EarlyFragmentTests, vk::PipelineStageFlagBits::eEarlyFragmentTests)
        TRY_SET_BIT(PipelineStage::LateFragmentTests, vk::PipelineStageFlagBits::eLateFragmentTests)
        TRY_SET_BIT(PipelineStage::TessellationControlShader, vk::PipelineStageFlagBits::eTessellationControlShader)
        TRY_SET_BIT(PipelineStage::TessellationEvaluationShader, vk::PipelineStageFlagBits::eTessellationEvaluationShader)
        TRY_SET_BIT(PipelineStage::TopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe)
        return bits;
    }

    vk::AccessFlags access2VK(Flags<Access> flags) {
        if(flags == Access::None) {
            return vk::AccessFlagBits::eNone;
        }
        vk::AccessFlags bits{};
        TRY_SET_BIT(Access::HostRead, vk::AccessFlagBits::eHostRead)
        TRY_SET_BIT(Access::HostWrite, vk::AccessFlagBits::eHostWrite)
        TRY_SET_BIT(Access::IndexRead, vk::AccessFlagBits::eIndexRead)
        TRY_SET_BIT(Access::MemoryRead, vk::AccessFlagBits::eMemoryRead)
        TRY_SET_BIT(Access::MemoryWrite, vk::AccessFlagBits::eMemoryWrite)
        TRY_SET_BIT(Access::ShaderRead, vk::AccessFlagBits::eShaderRead)
        TRY_SET_BIT(Access::ShaderWrite, vk::AccessFlagBits::eShaderWrite)
        TRY_SET_BIT(Access::TransferRead, vk::AccessFlagBits::eTransferRead)
        TRY_SET_BIT(Access::TransferWrite, vk::AccessFlagBits::eTransferWrite)
        TRY_SET_BIT(Access::UniformRead, vk::AccessFlagBits::eUniformRead)
        TRY_SET_BIT(Access::ColorAttachmentRead, vk::AccessFlagBits::eColorAttachmentRead)
        TRY_SET_BIT(Access::ColorAttachmentWrite, vk::AccessFlagBits::eColorAttachmentWrite)
        TRY_SET_BIT(Access::IndirectCommandRead, vk::AccessFlagBits::eIndirectCommandRead)
        TRY_SET_BIT(Access::InputAttachmentRead, vk::AccessFlagBits::eInputAttachmentRead)
        TRY_SET_BIT(Access::VertexAttributeRead, vk::AccessFlagBits::eVertexAttributeRead)
        TRY_SET_BIT(Access::DepthStencilAttachmentRead, vk::AccessFlagBits::eDepthStencilAttachmentRead)
        TRY_SET_BIT(Access::DepthStencilAttachmentWrite, vk::AccessFlagBits::eDepthStencilAttachmentWrite)

        return bits;
    }

    vk::DependencyFlags dependency2VK(Flags<Dependency> flags) {
        vk::DependencyFlags bits{};
        TRY_SET_BIT(Dependency::ByRegionBit, vk::DependencyFlagBits::eByRegion)
        TRY_SET_BIT(Dependency::DeviceGroupBit, vk::DependencyFlagBits::eDeviceGroup)
        TRY_SET_BIT(Dependency::ViewLocalBit, vk::DependencyFlagBits::eViewLocal)
        return bits;
    }

    vk::ImageLayout imageLayout2VK(ImageLayout layout) {
        switch(layout) {
            CASE(ImageLayout::Undefined, vk::ImageLayout::eUndefined)
            CASE(ImageLayout::General, vk::ImageLayout::eGeneral)
            CASE(ImageLayout::Preinitialized, vk::ImageLayout::ePreinitialized)
            CASE(ImageLayout::PresentSrcKHR, vk::ImageLayout::ePresentSrcKHR)
            CASE(ImageLayout::AttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal)
            CASE(ImageLayout::ReadOnly_optimal, vk::ImageLayout::eReadOnlyOptimal)
            CASE(ImageLayout::ColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal)
            CASE(ImageLayout::DepthAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal)
            CASE(ImageLayout::StencilAttachmentOptimal, vk::ImageLayout::eStencilAttachmentOptimal)
            CASE(ImageLayout::TransferDstOptimal, vk::ImageLayout::eTransferDstOptimal)
            CASE(ImageLayout::TransferSrcOptimal, vk::ImageLayout::eTransferSrcOptimal)
            CASE(ImageLayout::DepthReadOnlyOptimal, vk::ImageLayout::eDepthReadOnlyOptimal)
            CASE(ImageLayout::DepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal)
            CASE(ImageLayout::DepthStencilReadOnlyOptimal, vk::ImageLayout::eDepthStencilReadOnlyOptimal)
            CASE(ImageLayout::ShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal)
            CASE(ImageLayout::StencilReadOnlyOptimal, vk::ImageLayout::eStencilReadOnlyOptimal)
            CASE(ImageLayout::DepthAttachmentStencilReadOnlyOptimal, vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal)
            CASE(ImageLayout::DepthReadOnlyStencilAttachmentOptimal, vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal)
        }
        CANT_REACH();
        return {};
    }

    vk::PipelineBindPoint pipelineBindPoint2VK(PipelineBindPoint point) {
        switch(point) {
            CASE(PipelineBindPoint::Compute, vk::PipelineBindPoint::eCompute)
            CASE(PipelineBindPoint::Graphics, vk::PipelineBindPoint::eGraphics)
        }
        CANT_REACH();
        return {};
    }

    vk::ImageTiling imageTiling2VK(ImageTiling tiling) {
        switch(tiling) {
            CASE(ImageTiling::Linear, vk::ImageTiling::eLinear)
            CASE(ImageTiling::Optimal, vk::ImageTiling::eOptimal)
        }
        CANT_REACH();
        return {};
    }

    vk::SharingMode sharingMode2VK(SharingMode mode) {
        switch(mode) {
            CASE(SharingMode::Concurrent, vk::SharingMode::eConcurrent)
            CASE(SharingMode::Exclusive, vk::SharingMode::eExclusive)
        }
        CANT_REACH();
        return {};
    }

    vk::ComponentSwizzle componentMapping2VK(ComponentMapping swizzle) {
        switch(swizzle) {
            CASE(ComponentMapping::SwizzleZero, vk::ComponentSwizzle::eZero)
            CASE(ComponentMapping::SwizzleOne, vk::ComponentSwizzle::eOne)
            CASE(ComponentMapping::SwizzleIdentity, vk::ComponentSwizzle::eIdentity)
            CASE(ComponentMapping::SwizzleR, vk::ComponentSwizzle::eR)
            CASE(ComponentMapping::SwizzleG, vk::ComponentSwizzle::eG)
            CASE(ComponentMapping::SwizzleB, vk::ComponentSwizzle::eB)
            CASE(ComponentMapping::SwizzleA, vk::ComponentSwizzle::eA)
        }
        CANT_REACH();
        return {};
    }

    vk::ColorComponentFlags componentMask2VK(Flags<ColorComponent> flags) {
        vk::ColorComponentFlags bits{};
        TRY_SET_BIT(ColorComponent::R, vk::ColorComponentFlagBits::eR)
        TRY_SET_BIT(ColorComponent::G, vk::ColorComponentFlagBits::eG)
        TRY_SET_BIT(ColorComponent::B, vk::ColorComponentFlagBits::eB)
        TRY_SET_BIT(ColorComponent::A, vk::ColorComponentFlagBits::eA)
        return bits;
    }

    vk::ColorSpaceKHR imageColorSpace2VK(ImageColorSpace space) {
        switch(space) {
                // CASE(ImageColorSpace::DolbyvisionEXT,         vk::ColorSpaceKHR::eDolbyvisionEXT)
            CASE(ImageColorSpace::AdobergbLinearEXT, vk::ColorSpaceKHR::eAdobergbLinearEXT)
            CASE(ImageColorSpace::AdobergbNonlinearEXT, vk::ColorSpaceKHR::eAdobergbNonlinearEXT)
            CASE(ImageColorSpace::Bt709LinearEXT, vk::ColorSpaceKHR::eBt709LinearEXT)
            CASE(ImageColorSpace::Bt709NonlinearEXT, vk::ColorSpaceKHR::eBt709NonlinearEXT)
            CASE(ImageColorSpace::Bt2020LinearEXT, vk::ColorSpaceKHR::eBt2020LinearEXT)
            CASE(ImageColorSpace::DisplayNativeAMD, vk::ColorSpaceKHR::eDisplayNativeAMD)
            CASE(ImageColorSpace::Hdr10HlgEXT, vk::ColorSpaceKHR::eHdr10HlgEXT)
            CASE(ImageColorSpace::Hdr10St2084EXT, vk::ColorSpaceKHR::eHdr10St2084EXT)
            CASE(ImageColorSpace::PassThroughEXT, vk::ColorSpaceKHR::ePassThroughEXT)
            CASE(ImageColorSpace::SrgbNonlinearKHR, vk::ColorSpaceKHR::eSrgbNonlinear)
            CASE(ImageColorSpace::DciP3NonlinearEXT, vk::ColorSpaceKHR::eDciP3NonlinearEXT)
            CASE(ImageColorSpace::DisplayP3LinearEXT, vk::ColorSpaceKHR::eDisplayP3LinearEXT)
            CASE(ImageColorSpace::DisplayP3NonlinearEXT, vk::ColorSpaceKHR::eDisplayP3NonlinearEXT)
            CASE(ImageColorSpace::ExtendedSrgbLinearEXT, vk::ColorSpaceKHR::eExtendedSrgbLinearEXT)
            CASE(ImageColorSpace::ExtendedSrgbNonlinearEXT, vk::ColorSpaceKHR::eExtendedSrgbNonlinearEXT)
        }
        CANT_REACH();
        return {};
    }

    ImageColorSpace VkColorSpace2ImageColorSpace(vk::ColorSpaceKHR space) {
        switch(space) {
                // CASE(vk::ColorSpaceKHR::eDolbyvisionEXT, ImageColorSpace::DolbyvisionEXT)
            CASE(vk::ColorSpaceKHR::eAdobergbLinearEXT, ImageColorSpace::AdobergbLinearEXT)
            CASE(vk::ColorSpaceKHR::eAdobergbNonlinearEXT, ImageColorSpace::AdobergbNonlinearEXT)
            CASE(vk::ColorSpaceKHR::eBt709LinearEXT, ImageColorSpace::Bt709LinearEXT)
            CASE(vk::ColorSpaceKHR::eBt709NonlinearEXT, ImageColorSpace::Bt709NonlinearEXT)
            CASE(vk::ColorSpaceKHR::eBt2020LinearEXT, ImageColorSpace::Bt2020LinearEXT)
            CASE(vk::ColorSpaceKHR::eDisplayNativeAMD, ImageColorSpace::DisplayNativeAMD)
            CASE(vk::ColorSpaceKHR::eHdr10HlgEXT, ImageColorSpace::Hdr10HlgEXT)
            CASE(vk::ColorSpaceKHR::eHdr10St2084EXT, ImageColorSpace::Hdr10St2084EXT)
            CASE(vk::ColorSpaceKHR::ePassThroughEXT, ImageColorSpace::PassThroughEXT)
            CASE(vk::ColorSpaceKHR::eSrgbNonlinear, ImageColorSpace::SrgbNonlinearKHR)
            CASE(vk::ColorSpaceKHR::eDciP3NonlinearEXT, ImageColorSpace::DciP3NonlinearEXT)
            CASE(vk::ColorSpaceKHR::eDisplayP3LinearEXT, ImageColorSpace::DisplayP3LinearEXT)
            CASE(vk::ColorSpaceKHR::eDisplayP3NonlinearEXT, ImageColorSpace::DisplayP3NonlinearEXT)
            CASE(vk::ColorSpaceKHR::eExtendedSrgbLinearEXT, ImageColorSpace::ExtendedSrgbLinearEXT)
            CASE(vk::ColorSpaceKHR::eExtendedSrgbNonlinearEXT, ImageColorSpace::ExtendedSrgbNonlinearEXT)
            default:
                CANT_REACH();
        }
        CANT_REACH();
        return {};
    }

    vk::MemoryPropertyFlags memoryProperty2VK(MemoryType property) {
        switch(property) {
            CASE(MemoryType::CPULocal, vk::MemoryPropertyFlagBits::eHostVisible);
            CASE(MemoryType::Coherence, vk::MemoryPropertyFlagBits::eHostCoherent);
            CASE(MemoryType::GPULocal, vk::MemoryPropertyFlagBits::eDeviceLocal);
        }
        CANT_REACH();
        return {};
    }

    std::optional<uint32_t> findMemoryType(vk::PhysicalDevice physicalDevice, vk::MemoryRequirements requirements, vk::MemoryPropertyFlags properties) {
        vk::PhysicalDeviceMemoryProperties props;
        physicalDevice.getMemoryProperties(&props);
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if ((1 << i & requirements.memoryTypeBits) &&
                (props.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return {};
    }

#undef CASE
}
