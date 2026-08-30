#pragma once

#include "common/export.h"
#include "graphics/enums.h"

#include <cstdint>
#include <limits>
#include <optional>

namespace Comet {
    inline constexpr std::uint32_t UNSPECIFIED_QUEUE_FAMILY =
        std::numeric_limits<std::uint32_t>::max();

    enum class ResourceUsage {
        Undefined,
        TransferSource,
        TransferDestination,
        VertexBuffer,
        IndexBuffer,
        IndirectBuffer,
        UniformRead,
        SampledRead,
        StorageRead,
        StorageReadWrite,
        ColorAttachmentWrite,
        DepthStencilAttachmentWrite,
        DepthStencilAttachmentRead,
        Present
    };

    struct ResourceState {
        Flags<PipelineStage> stages;
        Flags<Access> access;
        std::uint32_t queue_family = UNSPECIFIED_QUEUE_FAMILY;

        bool operator==(const ResourceState&) const noexcept = default;
    };

    struct ImageSubresourceRange {
        Flags<ImageAspect> aspects;
        std::uint32_t base_mip_level = 0;
        std::uint32_t level_count = 1;
        std::uint32_t base_array_layer = 0;
        std::uint32_t layer_count = 1;

        [[nodiscard]] bool is_valid() const noexcept {
            return static_cast<bool>(aspects)
                && level_count > 0
                && layer_count > 0;
        }

        bool operator==(
            const ImageSubresourceRange&) const noexcept = default;
    };

    struct ImageState {
        ResourceState resource;
        ImageLayout layout = ImageLayout::Undefined;
        ImageSubresourceRange subresources;

        bool operator==(const ImageState&) const noexcept = default;
    };

    [[nodiscard]] COMET_API std::optional<ResourceState>
    resolve_resource_state(
        ResourceUsage usage,
        Flags<PipelineStage> shader_stages = {},
        std::uint32_t queue_family = UNSPECIFIED_QUEUE_FAMILY);

    [[nodiscard]] COMET_API std::optional<ImageState> resolve_image_state(
        ResourceUsage usage,
        ImageSubresourceRange subresources,
        Flags<PipelineStage> shader_stages = {},
        std::uint32_t queue_family = UNSPECIFIED_QUEUE_FAMILY);
}
