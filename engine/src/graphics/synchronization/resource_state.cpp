#include "graphics/synchronization/resource_state.h"

#include <type_traits>

namespace Comet {
    namespace {
        bool is_shader_usage(const ResourceUsage usage) noexcept {
            return usage == ResourceUsage::UniformRead
                || usage == ResourceUsage::SampledRead
                || usage == ResourceUsage::StorageRead
                || usage == ResourceUsage::StorageReadWrite;
        }

        bool has_valid_stage_context(
            const ResourceUsage usage,
            const Flags<PipelineStage> shader_stages) noexcept {
            using StageBits = std::underlying_type_t<PipelineStage>;
            const StageBits stages = static_cast<StageBits>(shader_stages);
            constexpr StageBits SHADER_STAGES =
                static_cast<StageBits>(PipelineStage::VertexShader)
                | static_cast<StageBits>(
                    PipelineStage::TessellationControlShader)
                | static_cast<StageBits>(
                    PipelineStage::TessellationEvaluationShader)
                | static_cast<StageBits>(PipelineStage::GeometryShader)
                | static_cast<StageBits>(PipelineStage::FragmentShader)
                | static_cast<StageBits>(PipelineStage::ComputeShader);
            if(is_shader_usage(usage)) {
                return stages != 0 && (stages & ~SHADER_STAGES) == 0;
            }
            return stages == 0;
        }

        bool has_compatible_image_aspects(
            const ResourceUsage usage,
            const Flags<ImageAspect> aspects) noexcept {
            const auto raw = static_cast<Flags<ImageAspect>::underlying_type>(
                aspects);
            const auto color = static_cast<Flags<ImageAspect>::underlying_type>(
                ImageAspect::Color);
            const auto depth_stencil =
                static_cast<Flags<ImageAspect>::underlying_type>(
                    ImageAspect::Depth)
                | static_cast<Flags<ImageAspect>::underlying_type>(
                    ImageAspect::Stencil);
            if(usage == ResourceUsage::ColorAttachmentWrite
               || usage == ResourceUsage::Present) {
                return raw == color;
            }
            if(usage == ResourceUsage::DepthStencilAttachmentWrite
               || usage == ResourceUsage::DepthStencilAttachmentRead) {
                return (raw & depth_stencil) != 0 && (raw & color) == 0;
            }
            return true;
        }
    }

    std::optional<ResourceState> resolve_resource_state(
        const ResourceUsage usage,
        const Flags<PipelineStage> shader_stages,
        const std::uint32_t queue_family) {
        if(!has_valid_stage_context(usage, shader_stages)) {
            return std::nullopt;
        }

        switch(usage) {
            case ResourceUsage::Undefined:
            case ResourceUsage::Present:
                return ResourceState{.queue_family = queue_family};
            case ResourceUsage::TransferSource:
                return ResourceState{
                    .stages = Flags<PipelineStage>(PipelineStage::Transfer),
                    .access = Flags<Access>(Access::TransferRead),
                    .queue_family = queue_family
                };
            case ResourceUsage::TransferDestination:
                return ResourceState{
                    .stages = Flags<PipelineStage>(PipelineStage::Transfer),
                    .access = Flags<Access>(Access::TransferWrite),
                    .queue_family = queue_family
                };
            case ResourceUsage::VertexBuffer:
                return ResourceState{
                    .stages = Flags<PipelineStage>(PipelineStage::VertexInput),
                    .access = Flags<Access>(Access::VertexAttributeRead),
                    .queue_family = queue_family
                };
            case ResourceUsage::IndexBuffer:
                return ResourceState{
                    .stages = Flags<PipelineStage>(PipelineStage::VertexInput),
                    .access = Flags<Access>(Access::IndexRead),
                    .queue_family = queue_family
                };
            case ResourceUsage::IndirectBuffer:
                return ResourceState{
                    .stages = Flags<PipelineStage>(PipelineStage::DrawIndirect),
                    .access = Flags<Access>(Access::IndirectCommandRead),
                    .queue_family = queue_family
                };
            case ResourceUsage::UniformRead:
                return ResourceState{
                    .stages = shader_stages,
                    .access = Flags<Access>(Access::UniformRead),
                    .queue_family = queue_family
                };
            case ResourceUsage::SampledRead:
            case ResourceUsage::StorageRead:
                return ResourceState{
                    .stages = shader_stages,
                    .access = Flags<Access>(Access::ShaderRead),
                    .queue_family = queue_family
                };
            case ResourceUsage::StorageReadWrite:
                return ResourceState{
                    .stages = shader_stages,
                    .access = Flags<Access>(Access::ShaderRead)
                        | Access::ShaderWrite,
                    .queue_family = queue_family
                };
            case ResourceUsage::ColorAttachmentWrite:
                return ResourceState{
                    .stages = Flags<PipelineStage>(
                        PipelineStage::ColorAttachmentOutput),
                    .access = Flags<Access>(Access::ColorAttachmentRead)
                        | Access::ColorAttachmentWrite,
                    .queue_family = queue_family
                };
            case ResourceUsage::DepthStencilAttachmentWrite:
                return ResourceState{
                    .stages = Flags<PipelineStage>(
                        PipelineStage::EarlyFragmentTests)
                        | PipelineStage::LateFragmentTests,
                    .access = Flags<Access>(
                        Access::DepthStencilAttachmentRead)
                        | Access::DepthStencilAttachmentWrite,
                    .queue_family = queue_family
                };
            case ResourceUsage::DepthStencilAttachmentRead:
                return ResourceState{
                    .stages = Flags<PipelineStage>(
                        PipelineStage::EarlyFragmentTests)
                        | PipelineStage::LateFragmentTests,
                    .access = Flags<Access>(
                        Access::DepthStencilAttachmentRead),
                    .queue_family = queue_family
                };
        }
        return std::nullopt;
    }

    std::optional<ImageState> resolve_image_state(
        const ResourceUsage usage,
        const ImageSubresourceRange subresources,
        const Flags<PipelineStage> shader_stages,
        const std::uint32_t queue_family) {
        if(!subresources.is_valid()
           || !has_compatible_image_aspects(usage, subresources.aspects)) {
            return std::nullopt;
        }
        switch(usage) {
            case ResourceUsage::VertexBuffer:
            case ResourceUsage::IndexBuffer:
            case ResourceUsage::IndirectBuffer:
            case ResourceUsage::UniformRead:
                return std::nullopt;
            default:
                break;
        }

        const auto resource = resolve_resource_state(
            usage,
            shader_stages,
            queue_family);
        if(!resource) {
            return std::nullopt;
        }

        ImageLayout layout = ImageLayout::Undefined;
        switch(usage) {
            case ResourceUsage::Undefined:
                layout = ImageLayout::Undefined;
                break;
            case ResourceUsage::TransferSource:
                layout = ImageLayout::TransferSrcOptimal;
                break;
            case ResourceUsage::TransferDestination:
                layout = ImageLayout::TransferDstOptimal;
                break;
            case ResourceUsage::SampledRead:
                layout = ImageLayout::ShaderReadOnlyOptimal;
                break;
            case ResourceUsage::StorageRead:
            case ResourceUsage::StorageReadWrite:
                layout = ImageLayout::General;
                break;
            case ResourceUsage::ColorAttachmentWrite:
                layout = ImageLayout::ColorAttachmentOptimal;
                break;
            case ResourceUsage::DepthStencilAttachmentWrite:
                layout = ImageLayout::DepthStencilAttachmentOptimal;
                break;
            case ResourceUsage::DepthStencilAttachmentRead:
                layout = ImageLayout::DepthStencilReadOnlyOptimal;
                break;
            case ResourceUsage::Present:
                layout = ImageLayout::PresentSrcKHR;
                break;
            case ResourceUsage::VertexBuffer:
            case ResourceUsage::IndexBuffer:
            case ResourceUsage::IndirectBuffer:
            case ResourceUsage::UniformRead:
                return std::nullopt;
        }
        return ImageState{
            .resource = *resource,
            .layout = layout,
            .subresources = subresources
        };
    }
}
