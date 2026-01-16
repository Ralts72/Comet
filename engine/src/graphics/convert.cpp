#include "convert.h"
#include "common/logger.h"

namespace Comet::Graphics {
    #define CASE(a, b) \
    case (a):      \
        return (b);

#define TRY_SET_BIT(src, dst) \
    if (flags & (src)) bits |= (dst);

    vk::Filter filter_to_vk(const Filter filter) {
        switch(filter) {
            CASE(Filter::Nearest, vk::Filter::eNearest)
            CASE(Filter::Linear, vk::Filter::eLinear)
        }
        LOG_FATAL("can't reach");
    }

    vk::SamplerAddressMode sampler_address_mode_to_vk(const SamplerAddressMode mode) {
        switch(mode) {
            CASE(SamplerAddressMode::ClampToEdge, vk::SamplerAddressMode::eClampToEdge)
            CASE(SamplerAddressMode::Repeat, vk::SamplerAddressMode::eRepeat)
            CASE(SamplerAddressMode::MirrorRepeat, vk::SamplerAddressMode::eMirroredRepeat)
            CASE(SamplerAddressMode::ClampToBorder, vk::SamplerAddressMode::eClampToBorder)
        }
        LOG_FATAL("can't reach");
    }

    vk::CompareOp compare_op_to_vk(const CompareOp op) {
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
        LOG_FATAL("can't reach");
    }

    vk::ImageType image_type_to_vk(const ImageType type) {
        switch(type) {
            CASE(ImageType::Dim1, vk::ImageType::e1D);
            CASE(ImageType::Dim2, vk::ImageType::e2D);
            CASE(ImageType::Dim3, vk::ImageType::e3D);
        }
        LOG_FATAL("can't reach");
    }

    vk::SampleCountFlagBits sample_count_to_vk(const SampleCount count) {
        switch(count) {
            CASE(SampleCount::Count1, vk::SampleCountFlagBits::e1);
            CASE(SampleCount::Count2, vk::SampleCountFlagBits::e2);
            CASE(SampleCount::Count4, vk::SampleCountFlagBits::e4);
            CASE(SampleCount::Count8, vk::SampleCountFlagBits::e8);
            CASE(SampleCount::Count16, vk::SampleCountFlagBits::e16);
            CASE(SampleCount::Count32, vk::SampleCountFlagBits::e32);
            CASE(SampleCount::Count64, vk::SampleCountFlagBits::e64);
        }
        LOG_FATAL("can't reach");
    }

    vk::Format format_to_vk(Format format) {
        return static_cast<vk::Format>(format);
    }

    Format vk_to_format(vk::Format format) {
        return static_cast<Format>(format);
    }

    vk::ImageViewType image_view_type_to_vk(const ImageViewType type) {
        switch(type) {
            CASE(ImageViewType::Dim1, vk::ImageViewType::e1D);
            CASE(ImageViewType::Dim2, vk::ImageViewType::e2D);
            CASE(ImageViewType::Dim3, vk::ImageViewType::e3D);
            CASE(ImageViewType::Dim2Array, vk::ImageViewType::e2DArray);
            CASE(ImageViewType::Cube, vk::ImageViewType::eCube);
            CASE(ImageViewType::CubeArray, vk::ImageViewType::eCubeArray);
        }
        LOG_FATAL("can't reach");
    }

    vk::PrimitiveTopology primitive_topology_to_vk(const Topology topology) {
        switch(topology) {
            CASE(Topology::LineList, vk::PrimitiveTopology::eLineList);
            CASE(Topology::LineStrip, vk::PrimitiveTopology::eLineStrip);
            CASE(Topology::PointList, vk::PrimitiveTopology::ePointList);
            CASE(Topology::TriangleList, vk::PrimitiveTopology::eTriangleList);
            CASE(Topology::TriangleStrip, vk::PrimitiveTopology::eTriangleStrip);
            CASE(Topology::TriangleFan, vk::PrimitiveTopology::eTriangleFan);
        }
        LOG_FATAL("can't reach");
    }

    vk::PolygonMode polygon_mode_to_vk(const PolygonMode mode) {
        switch(mode) {
            CASE(PolygonMode::Line, vk::PolygonMode::eLine);
            CASE(PolygonMode::Fill, vk::PolygonMode::eFill);
            CASE(PolygonMode::Point, vk::PolygonMode::ePoint);
        }
        LOG_FATAL("can't reach");
    }

    vk::FrontFace front_face_to_vk(const FrontFace face) {
        switch(face) {
            CASE(FrontFace::CCW, vk::FrontFace::eCounterClockwise);
            CASE(FrontFace::CW, vk::FrontFace::eClockwise);
        }
        LOG_FATAL("can't reach");
    }

    vk::BlendOp blend_op_to_vk(const BlendOp op) {
        switch(op) {
            CASE(BlendOp::Add, vk::BlendOp::eAdd);
            CASE(BlendOp::Subtract, vk::BlendOp::eSubtract);
            CASE(BlendOp::ReverseSubtract, vk::BlendOp::eReverseSubtract);
            CASE(BlendOp::Min, vk::BlendOp::eMin);
            CASE(BlendOp::Max, vk::BlendOp::eMax);
        }
        LOG_FATAL("can't reach");
    }

    vk::BlendFactor blend_factor_to_vk(const BlendFactor factor) {
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
        LOG_FATAL("can't reach");
    }

    vk::StencilOp stencil_op_to_vk(const StencilOp op) {
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
        LOG_FATAL("can't reach");
    }

    vk::AttachmentLoadOp attachment_load_op_to_vk(const AttachmentLoadOp op) {
        switch(op) {
            CASE(AttachmentLoadOp::Clear, vk::AttachmentLoadOp::eClear);
            CASE(AttachmentLoadOp::Load, vk::AttachmentLoadOp::eLoad);
            CASE(AttachmentLoadOp::DontCare, vk::AttachmentLoadOp::eDontCare);
        }
        LOG_FATAL("can't reach");
    }

    vk::AttachmentStoreOp attachment_store_op_to_vk(const AttachmentStoreOp op) {
        switch(op) {
            CASE(AttachmentStoreOp::Store, vk::AttachmentStoreOp::eStore);
            CASE(AttachmentStoreOp::DontCare, vk::AttachmentStoreOp::eDontCare);
        }
        LOG_FATAL("can't reach");
    }

    vk::IndexType index_type_to_vk(const IndexType type) {
        switch(type) {
            CASE(IndexType::Uint16, vk::IndexType::eUint16);
            CASE(IndexType::Uint32, vk::IndexType::eUint32);
        }
        LOG_FATAL("can't reach");
    }

    vk::Format vertex_format_to_vk(const VertexFormat format) {
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
        LOG_FATAL("can't reach");
    }

    vk::ShaderStageFlags shader_stage_to_vk(const Flags<ShaderStage> flags) {
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

    vk::BorderColor border_color_to_vk(const BorderColor color) {
        switch(color) {
            CASE(BorderColor::FloatTransparentBlack, vk::BorderColor::eFloatTransparentBlack);
            CASE(BorderColor::IntTransparentBlack, vk::BorderColor::eIntTransparentBlack);
            CASE(BorderColor::FloatOpaqueBlack, vk::BorderColor::eFloatOpaqueBlack);
            CASE(BorderColor::IntOpaqueBlack, vk::BorderColor::eIntOpaqueBlack);
            CASE(BorderColor::FloatOpaqueWhite, vk::BorderColor::eFloatOpaqueWhite);
            CASE(BorderColor::IntOpaqueWhite, vk::BorderColor::eIntOpaqueWhite);
        }
        LOG_FATAL("can't reach");
    }

    vk::SamplerMipmapMode sampler_mipmap_mode_to_vk(const SamplerMipmapMode mode) {
        switch(mode) {
            CASE(SamplerMipmapMode::Nearest, vk::SamplerMipmapMode::eNearest);
            CASE(SamplerMipmapMode::Linear, vk::SamplerMipmapMode::eLinear);
        }
        LOG_FATAL("can't reach");
    }

    vk::DescriptorType description_type_to_vk(const DescriptorType type) {
        switch(type) {
            CASE(DescriptorType::Sampler, vk::DescriptorType::eSampler)
            CASE(DescriptorType::CombinedImageSampler, vk::DescriptorType::eCombinedImageSampler)
            CASE(DescriptorType::SampledImage, vk::DescriptorType::eSampledImage)
            CASE(DescriptorType::StorageImage, vk::DescriptorType::eStorageImage)
            CASE(DescriptorType::UniformTexelBuffer, vk::DescriptorType::eUniformTexelBuffer)
            CASE(DescriptorType::StorageTexelBuffer, vk::DescriptorType::eStorageTexelBuffer)
            CASE(DescriptorType::UniformBuffer, vk::DescriptorType::eUniformBuffer)
            CASE(DescriptorType::StorageBuffer, vk::DescriptorType::eStorageBuffer)
            CASE(DescriptorType::UniformBufferDynamic, vk::DescriptorType::eUniformBufferDynamic)
            CASE(DescriptorType::StoragesBufferDynamic, vk::DescriptorType::eStorageBufferDynamic)
            CASE(DescriptorType::InputAttachment, vk::DescriptorType::eInputAttachment)
            CASE(DescriptorType::InlineUniformBlock, vk::DescriptorType::eUniformBufferDynamic)
        }
        LOG_FATAL("can't reach");
    }

    vk::DescriptorPoolCreateFlags descriptor_pool_create_flags_to_vk(const Flags<DescriptorPoolCreateFlag> flags) {
        vk::DescriptorPoolCreateFlags bits{};
        TRY_SET_BIT(DescriptorPoolCreateFlag::FreeDescriptorSet, vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        TRY_SET_BIT(DescriptorPoolCreateFlag::UpdateAfterBind, vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind)
        return bits;
    }

    vk::ImageAspectFlags image_aspect_to_vk(const Flags<ImageAspect> flags) {
        vk::ImageAspectFlags bits{};
        TRY_SET_BIT(ImageAspect::Color, vk::ImageAspectFlagBits::eColor)
        TRY_SET_BIT(ImageAspect::Depth, vk::ImageAspectFlagBits::eDepth)
        TRY_SET_BIT(ImageAspect::Stencil, vk::ImageAspectFlagBits::eStencil)
        return bits;
    }

    vk::BufferUsageFlags buffer_usage_to_vk(const Flags<BufferUsage> flags) {
        vk::BufferUsageFlags bits{};
        TRY_SET_BIT(BufferUsage::CopySrc, vk::BufferUsageFlagBits::eTransferSrc)
        TRY_SET_BIT(BufferUsage::CopyDst, vk::BufferUsageFlagBits::eTransferDst)
        TRY_SET_BIT(BufferUsage::Vertex, vk::BufferUsageFlagBits::eVertexBuffer)
        TRY_SET_BIT(BufferUsage::Index, vk::BufferUsageFlagBits::eIndexBuffer)
        TRY_SET_BIT(BufferUsage::Uniform, vk::BufferUsageFlagBits::eUniformBuffer)
        TRY_SET_BIT(BufferUsage::Storage, vk::BufferUsageFlagBits::eStorageBuffer)
        return bits;
    }

    vk::ImageUsageFlags image_usage_to_vk(const Flags<ImageUsage> flags) {
        vk::ImageUsageFlags bits{};
        TRY_SET_BIT(ImageUsage::Sampled, vk::ImageUsageFlagBits::eSampled)
        TRY_SET_BIT(ImageUsage::CopySrc, vk::ImageUsageFlagBits::eTransferSrc)
        TRY_SET_BIT(ImageUsage::CopyDst, vk::ImageUsageFlagBits::eTransferDst)
        TRY_SET_BIT(ImageUsage::StorageBinding, vk::ImageUsageFlagBits::eStorage)
        TRY_SET_BIT(ImageUsage::ColorAttachment, vk::ImageUsageFlagBits::eColorAttachment)
        TRY_SET_BIT(ImageUsage::DepthStencilAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment)
        return bits;
    }

    vk::ColorComponentFlags color_write_mask_to_vk(const Flags<ColorWriteMask> flags) {
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

    vk::CullModeFlags cull_mode_to_vk(const Flags<CullMode> mode) {
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
        LOG_FATAL("can't reach");
    }

    vk::PipelineStageFlags pipeline_stage_to_vk(const Flags<PipelineStage> flags) {
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

    vk::AccessFlags access_to_vk(const Flags<Access> flags) {
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

    vk::DependencyFlags dependency_to_vk(const Flags<Dependency> flags) {
        vk::DependencyFlags bits{};
        TRY_SET_BIT(Dependency::ByRegionBit, vk::DependencyFlagBits::eByRegion)
        TRY_SET_BIT(Dependency::DeviceGroupBit, vk::DependencyFlagBits::eDeviceGroup)
        TRY_SET_BIT(Dependency::ViewLocalBit, vk::DependencyFlagBits::eViewLocal)
        return bits;
    }

    vk::ImageLayout image_layout_to_vk(const ImageLayout layout) {
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
        LOG_FATAL("can't reach");
    }

    vk::PipelineBindPoint pipeline_bind_point_to_vk(const PipelineBindPoint point) {
        switch(point) {
            CASE(PipelineBindPoint::Compute, vk::PipelineBindPoint::eCompute)
            CASE(PipelineBindPoint::Graphics, vk::PipelineBindPoint::eGraphics)
        }
        LOG_FATAL("can't reach");
    }

    vk::ImageTiling image_tiling_to_vk(const ImageTiling tiling) {
        switch(tiling) {
            CASE(ImageTiling::Linear, vk::ImageTiling::eLinear)
            CASE(ImageTiling::Optimal, vk::ImageTiling::eOptimal)
        }
        LOG_FATAL("can't reach");
    }

    vk::SharingMode sharing_mode_to_vk(const SharingMode mode) {
        switch(mode) {
            CASE(SharingMode::Concurrent, vk::SharingMode::eConcurrent)
            CASE(SharingMode::Exclusive, vk::SharingMode::eExclusive)
        }
        LOG_FATAL("can't reach");
    }

    vk::ComponentSwizzle component_mapping_to_vk(const ComponentMapping swizzle) {
        switch(swizzle) {
            CASE(ComponentMapping::SwizzleZero, vk::ComponentSwizzle::eZero)
            CASE(ComponentMapping::SwizzleOne, vk::ComponentSwizzle::eOne)
            CASE(ComponentMapping::SwizzleIdentity, vk::ComponentSwizzle::eIdentity)
            CASE(ComponentMapping::SwizzleR, vk::ComponentSwizzle::eR)
            CASE(ComponentMapping::SwizzleG, vk::ComponentSwizzle::eG)
            CASE(ComponentMapping::SwizzleB, vk::ComponentSwizzle::eB)
            CASE(ComponentMapping::SwizzleA, vk::ComponentSwizzle::eA)
        }
        LOG_FATAL("can't reach");
    }

    vk::ColorComponentFlags component_mask_to_vk(const Flags<ColorComponent> flags) {
        vk::ColorComponentFlags bits{};
        TRY_SET_BIT(ColorComponent::R, vk::ColorComponentFlagBits::eR)
        TRY_SET_BIT(ColorComponent::G, vk::ColorComponentFlagBits::eG)
        TRY_SET_BIT(ColorComponent::B, vk::ColorComponentFlagBits::eB)
        TRY_SET_BIT(ColorComponent::A, vk::ColorComponentFlagBits::eA)
        return bits;
    }

    vk::ColorSpaceKHR image_color_space_to_vk(const ImageColorSpace space) {
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
        LOG_FATAL("can't reach");
    }

    ImageColorSpace vk_to_image_color_space(const vk::ColorSpaceKHR space) {
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
                LOG_FATAL("can't reach");
        }
        LOG_FATAL("can't reach");
    }

    vk::MemoryPropertyFlags memory_property_to_vk(const Flags<MemoryType> flags) {
        vk::MemoryPropertyFlags bits{};
        TRY_SET_BIT(MemoryType::Coherence,vk::MemoryPropertyFlagBits::eHostCoherent)
        TRY_SET_BIT(MemoryType::CPULocal, vk::MemoryPropertyFlagBits::eHostVisible)
        TRY_SET_BIT(MemoryType::GPULocal, vk::MemoryPropertyFlagBits::eDeviceLocal)
        return bits;
    }


    vk::VertexInputRate vertex_input_rate_to_vk(const VertexInputRate rate) {
        switch(rate) {
            CASE(VertexInputRate::Vertex, vk::VertexInputRate::eVertex);
            CASE(VertexInputRate::Instance, vk::VertexInputRate::eInstance);
        }
        LOG_FATAL("can't reach");
    }

    vk::DynamicState dynamic_state_to_vk(const DynamicState state) {
        switch(state) {
            CASE(DynamicState::Viewport, vk::DynamicState::eViewport);
            CASE(DynamicState::Scissor, vk::DynamicState::eScissor);
            CASE(DynamicState::LineWidth, vk::DynamicState::eLineWidth);
            CASE(DynamicState::DepthBias, vk::DynamicState::eDepthBias);
            CASE(DynamicState::BlendConstants, vk::DynamicState::eBlendConstants);
            CASE(DynamicState::DepthBounds, vk::DynamicState::eDepthBounds);
            CASE(DynamicState::StencilCompareMask, vk::DynamicState::eStencilCompareMask);
            CASE(DynamicState::StencilWriteMask, vk::DynamicState::eStencilWriteMask);
            CASE(DynamicState::StencilReference, vk::DynamicState::eStencilReference);
        }
        LOG_FATAL("can't reach");
    }

    vk::PresentModeKHR present_mode_to_vk(const PresentMode mode) {
        switch(mode) {
            CASE(PresentMode::Immediate, vk::PresentModeKHR::eImmediate);
            CASE(PresentMode::Mailbox, vk::PresentModeKHR::eMailbox);
            CASE(PresentMode::Fifo, vk::PresentModeKHR::eFifo);
            CASE(PresentMode::FifoRelaxed, vk::PresentModeKHR::eFifoRelaxed);
        }
        LOG_FATAL("can't reach");
    }

    PresentMode vk_to_present_mode(const vk::PresentModeKHR mode) {
        switch(mode) {
            CASE(vk::PresentModeKHR::eImmediate, PresentMode::Immediate);
            CASE(vk::PresentModeKHR::eMailbox, PresentMode::Mailbox);
            CASE(vk::PresentModeKHR::eFifo, PresentMode::Fifo);
            CASE(vk::PresentModeKHR::eFifoRelaxed, PresentMode::FifoRelaxed);
            // Shared present modes (rarely used, fallback to Fifo)
            case vk::PresentModeKHR::eSharedDemandRefresh:
            case vk::PresentModeKHR::eSharedContinuousRefresh:
                LOG_WARN("Shared present mode not fully supported, falling back to Fifo");
                return PresentMode::Fifo;
            default:
                LOG_FATAL("Unknown present mode");
        }
    }

#undef CASE
#undef TRY_SET_BIT
}