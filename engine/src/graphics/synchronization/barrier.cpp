#include "graphics/synchronization/barrier.h"

#include "graphics/convert.h"

namespace Comet::Graphics {
    namespace {
        bool has_valid_access_scope(const ResourceState& state) {
            return !static_cast<bool>(state.access) || static_cast<bool>(state.stages);
        }

        bool has_stable_queue_owner(
            const ResourceState& before, const ResourceState& after) {
            const bool before_has_owner = before.queue_family != UNSPECIFIED_QUEUE_FAMILY;
            const bool after_has_owner = after.queue_family != UNSPECIFIED_QUEUE_FAMILY;
            return before_has_owner == after_has_owner
                   && (!before_has_owner || before.queue_family == after.queue_family);
        }
    }

    std::optional<vk::BufferMemoryBarrier2> build_buffer_memory_barrier(
        const vk::Buffer buffer, const ResourceState& before, const ResourceState& after,
        const vk::DeviceSize offset, const vk::DeviceSize size) {
        if(size == 0 || !has_valid_access_scope(before) || !has_valid_access_scope(after)
            || !has_stable_queue_owner(before, after)) {
            return std::nullopt;
        }

        vk::BufferMemoryBarrier2 barrier{};
        barrier.srcStageMask = pipeline_stage_to_vk2(before.stages);
        barrier.srcAccessMask = access_to_vk2(before.access);
        barrier.dstStageMask = pipeline_stage_to_vk2(after.stages);
        barrier.dstAccessMask = access_to_vk2(after.access);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer;
        barrier.offset = offset;
        barrier.size = size;
        return barrier;
    }

    std::optional<vk::ImageMemoryBarrier2> build_image_memory_barrier(
        const vk::Image image, const ImageState& before, const ImageState& after) {
        if(!before.subresources.is_valid() || before.subresources != after.subresources
            || after.layout == ImageLayout::Undefined
            || after.layout == ImageLayout::Preinitialized
            || (before.layout == ImageLayout::Undefined
                && static_cast<bool>(before.resource.access))
            || !has_valid_access_scope(before.resource)
            || !has_valid_access_scope(after.resource)
            || !has_stable_queue_owner(before.resource, after.resource)) {
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
