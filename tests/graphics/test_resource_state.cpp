#include "graphics/synchronization/resource_state.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        ImageSubresourceRange color_range() {
            return {.aspects = Flags<ImageAspect>(ImageAspect::Color)};
        }

        ImageSubresourceRange depth_range() {
            return {.aspects = Flags<ImageAspect>(ImageAspect::Depth)};
        }
    }

    TEST(ResourceStateTest, ResolvesFixedPipelineBufferUsages) {
        EXPECT_EQ(resolve_resource_state(ResourceUsage::TransferSource),
            ResourceState({.stages = Flags<PipelineStage>(PipelineStage::Transfer),
                .access = Flags<Access>(Access::TransferRead)}));
        EXPECT_EQ(resolve_resource_state(ResourceUsage::VertexBuffer),
            ResourceState({.stages = Flags<PipelineStage>(PipelineStage::VertexInput),
                .access = Flags<Access>(Access::VertexAttributeRead)}));
        EXPECT_EQ(resolve_resource_state(ResourceUsage::IndexBuffer),
            ResourceState({.stages = Flags<PipelineStage>(PipelineStage::VertexInput),
                .access = Flags<Access>(Access::IndexRead)}));
        EXPECT_EQ(resolve_resource_state(ResourceUsage::IndirectBuffer),
            ResourceState({.stages = Flags<PipelineStage>(PipelineStage::DrawIndirect),
                .access = Flags<Access>(Access::IndirectCommandRead)}));
    }

    TEST(ResourceStateTest, RequiresExplicitShaderStages) {
        EXPECT_FALSE(resolve_resource_state(ResourceUsage::SampledRead));
        EXPECT_FALSE(resolve_resource_state(ResourceUsage::UniformRead));
        EXPECT_FALSE(resolve_image_state(ResourceUsage::SampledRead, color_range()));
        EXPECT_FALSE(resolve_resource_state(
            ResourceUsage::SampledRead, Flags<PipelineStage>(PipelineStage::Transfer)));

        const auto state = resolve_resource_state(
            ResourceUsage::SampledRead, Flags<PipelineStage>(PipelineStage::VertexShader)
                                            | PipelineStage::FragmentShader);

        ASSERT_TRUE(state.has_value());
        EXPECT_EQ(state->stages, Flags<PipelineStage>(PipelineStage::VertexShader)
                                     | PipelineStage::FragmentShader);
        EXPECT_EQ(state->access, Access::ShaderRead);
    }

    TEST(ResourceStateTest, RejectsShaderStagesForFixedPipelineUsages) {
        EXPECT_FALSE(resolve_resource_state(ResourceUsage::TransferDestination,
            Flags<PipelineStage>(PipelineStage::FragmentShader)));
        EXPECT_FALSE(resolve_image_state(ResourceUsage::ColorAttachmentWrite,
            color_range(), Flags<PipelineStage>(PipelineStage::FragmentShader)));
    }

    TEST(ResourceStateTest, ResolvesImageLayoutsAndSynchronization) {
        const auto undefined =
            resolve_image_state(ResourceUsage::Undefined, color_range());
        const auto transfer =
            resolve_image_state(ResourceUsage::TransferDestination, color_range());
        const auto sampled = resolve_image_state(ResourceUsage::SampledRead,
            color_range(), Flags<PipelineStage>(PipelineStage::FragmentShader));
        const auto storage = resolve_image_state(ResourceUsage::StorageReadWrite,
            color_range(), Flags<PipelineStage>(PipelineStage::ComputeShader));
        const auto color =
            resolve_image_state(ResourceUsage::ColorAttachmentWrite, color_range());
        const auto depth = resolve_image_state(
            ResourceUsage::DepthStencilAttachmentWrite, depth_range());
        const auto depth_read =
            resolve_image_state(ResourceUsage::DepthStencilAttachmentRead, depth_range());
        const auto present = resolve_image_state(ResourceUsage::Present, color_range());

        ASSERT_TRUE(undefined);
        EXPECT_EQ(undefined->layout, ImageLayout::Undefined);
        EXPECT_FALSE(undefined->resource.stages);
        EXPECT_FALSE(undefined->resource.access);
        ASSERT_TRUE(transfer);
        EXPECT_EQ(transfer->layout, ImageLayout::TransferDstOptimal);
        EXPECT_EQ(transfer->resource.access, Access::TransferWrite);
        ASSERT_TRUE(sampled);
        EXPECT_EQ(sampled->layout, ImageLayout::ShaderReadOnlyOptimal);
        EXPECT_EQ(sampled->resource.stages, PipelineStage::FragmentShader);
        EXPECT_EQ(sampled->resource.access, Access::ShaderRead);
        ASSERT_TRUE(storage);
        EXPECT_EQ(storage->layout, ImageLayout::General);
        EXPECT_EQ(storage->resource.stages, PipelineStage::ComputeShader);
        EXPECT_EQ(storage->resource.access,
            Flags<Access>(Access::ShaderRead) | Access::ShaderWrite);
        ASSERT_TRUE(color);
        EXPECT_EQ(color->layout, ImageLayout::ColorAttachmentOptimal);
        EXPECT_EQ(color->resource.access,
            Flags<Access>(Access::ColorAttachmentRead) | Access::ColorAttachmentWrite);
        ASSERT_TRUE(depth);
        EXPECT_EQ(depth->layout, ImageLayout::DepthStencilAttachmentOptimal);
        ASSERT_TRUE(depth_read);
        EXPECT_EQ(depth_read->layout, ImageLayout::DepthStencilReadOnlyOptimal);
        EXPECT_EQ(depth_read->resource.access, Access::DepthStencilAttachmentRead);
        ASSERT_TRUE(present);
        EXPECT_EQ(present->layout, ImageLayout::PresentSrcKHR);
        EXPECT_FALSE(present->resource.stages);
        EXPECT_FALSE(present->resource.access);
        EXPECT_EQ(present->subresources, color_range());
    }

    TEST(ResourceStateTest, RejectsBufferOnlyUsagesForImages) {
        EXPECT_FALSE(resolve_image_state(ResourceUsage::VertexBuffer, color_range()));
        EXPECT_FALSE(resolve_image_state(ResourceUsage::IndexBuffer, color_range()));
        EXPECT_FALSE(resolve_image_state(ResourceUsage::IndirectBuffer, color_range()));
        EXPECT_FALSE(resolve_image_state(ResourceUsage::UniformRead, color_range(),
            Flags<PipelineStage>(PipelineStage::VertexShader)));
    }

    TEST(ResourceStateTest, CarriesQueueOwnershipAndSubresourceRange) {
        const ImageSubresourceRange subresources{
            .aspects = Flags<ImageAspect>(ImageAspect::Color),
            .base_mip_level = 2,
            .level_count = 3,
            .base_array_layer = 1,
            .layer_count = 4};

        const auto state =
            resolve_image_state(ResourceUsage::TransferDestination, subresources, {}, 7);

        ASSERT_TRUE(state);
        EXPECT_EQ(state->resource.queue_family, 7u);
        EXPECT_EQ(state->subresources, subresources);
    }

    TEST(ResourceStateTest, RejectsInvalidOrIncompatibleSubresources) {
        EXPECT_FALSE(resolve_image_state(
            ResourceUsage::TransferDestination, ImageSubresourceRange{}));
        EXPECT_FALSE(resolve_image_state(ResourceUsage::TransferDestination,
            ImageSubresourceRange{
                .aspects = Flags<ImageAspect>(ImageAspect::Color), .level_count = 0}));
        EXPECT_FALSE(resolve_image_state(ResourceUsage::TransferDestination,
            ImageSubresourceRange{
                .aspects = Flags<ImageAspect>(ImageAspect::Color), .layer_count = 0}));
        EXPECT_FALSE(
            resolve_image_state(ResourceUsage::ColorAttachmentWrite, depth_range()));
        EXPECT_FALSE(resolve_image_state(
            ResourceUsage::DepthStencilAttachmentWrite, color_range()));
    }
}
