#include "graphics/synchronization/barrier.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        ImageSubresourceRange color_range() {
            return {
                .aspects = Flags<ImageAspect>(ImageAspect::Color),
                .base_mip_level = 2,
                .level_count = 3,
                .base_array_layer = 1,
                .layer_count = 4
            };
        }

        ImageState require_state(
            const ResourceUsage usage,
            const ImageSubresourceRange& subresources,
            const Flags<PipelineStage> shader_stages = {}) {
            const auto state = resolve_image_state(
                usage,
                subresources,
                shader_stages);
            EXPECT_TRUE(state);
            return state.value_or(ImageState{});
        }
    }

    TEST(ImageBarrierTest, BuildsSynchronization2BarrierFromTypedStates) {
        const auto subresources = color_range();
        const auto before = require_state(
            ResourceUsage::Undefined,
            subresources);
        const auto after = require_state(
            ResourceUsage::TransferDestination,
            subresources);

        const auto barrier = Graphics::build_image_memory_barrier(
            {},
            before,
            after);

        ASSERT_TRUE(barrier);
        EXPECT_FALSE(barrier->srcStageMask);
        EXPECT_FALSE(barrier->srcAccessMask);
        EXPECT_EQ(
            barrier->dstStageMask,
            vk::PipelineStageFlagBits2::eTransfer);
        EXPECT_EQ(
            barrier->dstAccessMask,
            vk::AccessFlagBits2::eTransferWrite);
        EXPECT_EQ(barrier->oldLayout, vk::ImageLayout::eUndefined);
        EXPECT_EQ(
            barrier->newLayout,
            vk::ImageLayout::eTransferDstOptimal);
        EXPECT_EQ(
            barrier->srcQueueFamilyIndex,
            VK_QUEUE_FAMILY_IGNORED);
        EXPECT_EQ(
            barrier->dstQueueFamilyIndex,
            VK_QUEUE_FAMILY_IGNORED);
        EXPECT_EQ(
            barrier->subresourceRange.aspectMask,
            vk::ImageAspectFlagBits::eColor);
        EXPECT_EQ(barrier->subresourceRange.baseMipLevel, 2u);
        EXPECT_EQ(barrier->subresourceRange.levelCount, 3u);
        EXPECT_EQ(barrier->subresourceRange.baseArrayLayer, 1u);
        EXPECT_EQ(barrier->subresourceRange.layerCount, 4u);
    }

    TEST(ImageBarrierTest, MapsShaderAccess) {
        const auto subresources = color_range();
        auto before = require_state(
            ResourceUsage::TransferDestination,
            subresources);
        auto after = require_state(
            ResourceUsage::SampledRead,
            subresources,
            Flags<PipelineStage>(PipelineStage::FragmentShader));
        const auto barrier = Graphics::build_image_memory_barrier(
            {},
            before,
            after);

        ASSERT_TRUE(barrier);
        EXPECT_EQ(
            barrier->srcStageMask,
            vk::PipelineStageFlagBits2::eTransfer);
        EXPECT_EQ(
            barrier->srcAccessMask,
            vk::AccessFlagBits2::eTransferWrite);
        EXPECT_EQ(
            barrier->dstStageMask,
            vk::PipelineStageFlagBits2::eFragmentShader);
        EXPECT_EQ(
            barrier->dstAccessMask,
            vk::AccessFlagBits2::eShaderRead);
    }

    TEST(ImageBarrierTest, IgnoresMatchingQueueOwnership) {
        const auto subresources = color_range();
        auto before = require_state(
            ResourceUsage::TransferDestination,
            subresources);
        auto after = require_state(
            ResourceUsage::SampledRead,
            subresources,
            Flags<PipelineStage>(PipelineStage::FragmentShader));
        before.resource.queue_family = 3;
        after.resource.queue_family = 3;

        const auto barrier = Graphics::build_image_memory_barrier(
            {},
            before,
            after);

        ASSERT_TRUE(barrier);
        EXPECT_EQ(
            barrier->srcQueueFamilyIndex,
            VK_QUEUE_FAMILY_IGNORED);
        EXPECT_EQ(
            barrier->dstQueueFamilyIndex,
            VK_QUEUE_FAMILY_IGNORED);
    }

    TEST(ImageBarrierTest, RejectsIncompleteOrContradictoryStates) {
        const auto subresources = color_range();
        auto before = require_state(
            ResourceUsage::TransferDestination,
            subresources);
        auto after = require_state(
            ResourceUsage::SampledRead,
            subresources,
            Flags<PipelineStage>(PipelineStage::FragmentShader));

        after.resource.queue_family = 2;
        EXPECT_FALSE(Graphics::build_image_memory_barrier(
            {},
            before,
            after));

        before.resource.queue_family = 3;
        after.resource.queue_family = 2;
        EXPECT_FALSE(Graphics::build_image_memory_barrier(
            {},
            before,
            after));

        before.resource.queue_family = UNSPECIFIED_QUEUE_FAMILY;
        after.resource.queue_family = UNSPECIFIED_QUEUE_FAMILY;
        after.subresources.base_mip_level = 0;
        EXPECT_FALSE(Graphics::build_image_memory_barrier(
            {},
            before,
            after));

        after.subresources = subresources;
        after.resource.stages = {};
        EXPECT_FALSE(Graphics::build_image_memory_barrier(
            {},
            before,
            after));

        after = require_state(ResourceUsage::Undefined, subresources);
        EXPECT_FALSE(Graphics::build_image_memory_barrier(
            {},
            before,
            after));
    }
}
