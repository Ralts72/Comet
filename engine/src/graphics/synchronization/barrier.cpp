#include "graphics/synchronization/barrier.h"

#include "graphics/convert.h"

namespace Comet::Graphics {
    std::optional<vk::ImageMemoryBarrier2> build_image_memory_barrier(
        const vk::Image image,
        const ImageState& before,
        const ImageState& after) {
        if(!before.subresources.is_valid()
           || before.subresources != after.subresources
           || after.layout == ImageLayout::Undefined
           || after.layout == ImageLayout::Preinitialized
           || (before.layout == ImageLayout::Undefined
               && static_cast<bool>(before.resource.access))
           || (static_cast<bool>(before.resource.access)
               && !static_cast<bool>(before.resource.stages))
           || (static_cast<bool>(after.resource.access)
               && !static_cast<bool>(after.resource.stages))) {
            return std::nullopt;
        }

        const bool before_has_owner =
            before.resource.queue_family != UNSPECIFIED_QUEUE_FAMILY;
        const bool after_has_owner =
            after.resource.queue_family != UNSPECIFIED_QUEUE_FAMILY;
        if(before_has_owner != after_has_owner
           || (before_has_owner
               && before.resource.queue_family
                   != after.resource.queue_family)) {
            return std::nullopt;
        }

        const auto& range = after.subresources;
        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = pipeline_stage_to_vk2(before.resource.stages);
        barrier.srcAccessMask = access_to_vk2(before.resource.access);
        barrier.dstStageMask = pipeline_stage_to_vk2(after.resource.stages);
        barrier.dstAccessMask = access_to_vk2(after.resource.access);
        barrier.oldLayout = image_layout_to_vk(before.layout);
        barrier.newLayout = image_layout_to_vk(after.layout);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = image_aspect_to_vk(range.aspects);
        barrier.subresourceRange.baseMipLevel = range.base_mip_level;
        barrier.subresourceRange.levelCount = range.level_count;
        barrier.subresourceRange.baseArrayLayer = range.base_array_layer;
        barrier.subresourceRange.layerCount = range.layer_count;
        return barrier;
    }
}
