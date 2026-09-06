#include "render/render_graph.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class RenderGraphTest: public testing::Test {
    protected:
        RenderGraph graph;
        const Flags<PipelineStage> vertex{PipelineStage::VertexShader};
        const Flags<PipelineStage> fragment{PipelineStage::FragmentShader};
        static ImageState image_state(ResourceUsage usage = ResourceUsage::Undefined) {
            return *resolve_image_state(
                usage, {.aspects = Flags<ImageAspect>(ImageAspect::Color)});
        }
        RenderGraph::ResourceId buffer(ResourceState initial = {}) {
            return graph.import_buffer("buffer", {initial, 16, 32});
        }
        void pass(std::string name, RenderGraph::ResourceId id, ResourceUsage usage,
            Flags<PipelineStage> stages = {}) {
            graph.add_pass({std::move(name), {{id, usage, stages}}});
        }
    };
    TEST_F(RenderGraphTest, CompilesWriteReadReadWriteAndRetainsAllReaderScopes) {
        const auto id = buffer();
        pass("produce", id, ResourceUsage::TransferDestination);
        pass("vertex", id, ResourceUsage::UniformRead, vertex);
        pass("same reader", id, ResourceUsage::UniformRead, vertex);
        pass("fragment", id, ResourceUsage::StorageRead, fragment);
        pass("new access at previous stage", id, ResourceUsage::StorageRead, vertex);
        pass("overwrite", id, ResourceUsage::TransferDestination);
        pass("overwrite again", id, ResourceUsage::TransferDestination);
        const auto plan = graph.compile();
        const auto passes = plan.get_passes();
        ASSERT_EQ(passes.size(), 7);
        EXPECT_TRUE(passes[0].barriers.empty());
        ASSERT_EQ(passes[1].barriers.size(), 1);
        EXPECT_TRUE(passes[2].barriers.empty());
        EXPECT_EQ(passes[3].barriers.size(), 1);
        EXPECT_EQ(passes[4].barriers.size(), 1);
        ASSERT_EQ(passes[5].barriers.size(), 1);
        EXPECT_EQ(passes[6].barriers.size(), 1);
        const auto& before =
            std::get<RenderGraph::BufferState>(passes[5].barriers[0].before);
        EXPECT_TRUE(
            static_cast<bool>(before.resource.stages & PipelineStage::VertexShader));
        EXPECT_TRUE(
            static_cast<bool>(before.resource.stages & PipelineStage::FragmentShader));
        EXPECT_EQ(before.offset, 16);
        EXPECT_EQ(before.size, 32);
    }
    TEST_F(
        RenderGraphTest, ReadOnlyImportsNeedNoReadReadBarrierButWriteWaitsForAllReads) {
        const auto id =
            buffer(*resolve_resource_state(ResourceUsage::UniformRead, vertex));
        pass("vertex", id, ResourceUsage::UniformRead, vertex);
        pass("fragment", id, ResourceUsage::StorageRead, fragment);
        pass("write", id, ResourceUsage::StorageReadWrite, fragment);
        const auto plan = graph.compile();
        EXPECT_TRUE(plan.get_passes()[0].barriers.empty());
        EXPECT_TRUE(plan.get_passes()[1].barriers.empty());
        ASSERT_EQ(plan.get_passes()[2].barriers.size(), 1);
        const auto& state =
            std::get<RenderGraph::BufferState>(plan.get_passes()[2].barriers[0].before);
        EXPECT_EQ(state.resource.stages, vertex | fragment);
    }
    TEST_F(RenderGraphTest, ImageLayoutsAndExportPreserveExactSubresourceRange) {
        auto initial = image_state();
        initial.subresources = {.aspects = Flags<ImageAspect>(ImageAspect::Color),
            .base_mip_level = 2,
            .level_count = 1,
            .base_array_layer = 4,
            .layer_count = 2};
        const auto id = graph.import_image("hdr mip", initial);
        pass("producer", id, ResourceUsage::TransferDestination);
        pass("consumer", id, ResourceUsage::SampledRead, fragment);
        pass("same consumer", id, ResourceUsage::SampledRead, fragment);
        graph.export_resource({id, ResourceUsage::TransferSource, {}});
        const auto plan = graph.compile();
        ASSERT_EQ(plan.get_passes()[0].barriers.size(), 1);
        EXPECT_EQ(std::get<ImageState>(plan.get_passes()[0].barriers[0].before).layout,
            ImageLayout::Undefined);
        ASSERT_EQ(plan.get_passes()[1].barriers.size(), 1);
        EXPECT_TRUE(plan.get_passes()[2].barriers.empty());
        ASSERT_EQ(plan.get_exports().size(), 1);
        const auto final = std::get<ImageState>(plan.get_final_states()[0]);
        EXPECT_EQ(final.layout, ImageLayout::TransferSrcOptimal);
        EXPECT_EQ(final.subresources, initial.subresources);
        EXPECT_TRUE(static_cast<bool>(final.resource.access & Access::TransferWrite));
    }
    TEST_F(RenderGraphTest, RejectsUninitializedReadersAndReadModifyWrite) {
        const auto id = buffer();
        pass("invalid read", id, ResourceUsage::UniformRead, vertex);
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
        RenderGraph storage;
        const auto image = storage.import_image("empty", image_state());
        storage.add_pass({"rmw", {{image, ResourceUsage::StorageReadWrite, fragment}}});
        EXPECT_THROW(static_cast<void>(storage.compile()), std::invalid_argument);
    }
    TEST_F(RenderGraphTest, RejectsMissingStageWrongKindAndDuplicateUseBeforeRecording) {
        const auto id =
            buffer(*resolve_resource_state(ResourceUsage::TransferDestination));
        pass("missing stages", id, ResourceUsage::StorageRead);
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
        graph = {};
        const auto second = buffer();
        pass("image use", second, ResourceUsage::ColorAttachmentWrite);
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
        graph = {};
        const auto third = buffer();
        graph.add_pass({"duplicate", {{third, ResourceUsage::TransferDestination, {}},
                                         {third, ResourceUsage::TransferSource, {}}}});
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
        graph = {};
        pass("foreign index", {55}, ResourceUsage::TransferDestination);
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
    }
    TEST_F(RenderGraphTest, RequiresExplicitSameQueueOwnershipAndValidImportedRanges) {
        auto first = image_state();
        first.resource.queue_family = 0;
        static_cast<void>(graph.import_image("first", first));
        first.resource.queue_family = 1;
        static_cast<void>(graph.import_image("second", first));
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
        EXPECT_THROW(
            static_cast<void>(graph.import_buffer("overflow", {{}, UINT64_MAX, 2})),
            std::invalid_argument);
        EXPECT_THROW(static_cast<void>(graph.import_buffer("zero", {{}, 0, 0})),
            std::invalid_argument);
        first.subresources.base_mip_level = UINT32_MAX;
        EXPECT_THROW(static_cast<void>(graph.import_image("bad mip", first)),
            std::invalid_argument);
        EXPECT_THROW(static_cast<void>(graph.import_image("", image_state())),
            std::invalid_argument);
    }
    TEST_F(RenderGraphTest, HostReadsAreExportsAfterGpuCompletionNotPassCallbacks) {
        const auto id = buffer();
        pass("gpu fill", id, ResourceUsage::TransferDestination);
        graph.export_resource({id, ResourceUsage::HostRead, {}});
        const auto plan = graph.compile();
        ASSERT_EQ(plan.get_exports().size(), 1);
        const auto after =
            std::get<RenderGraph::BufferState>(plan.get_exports()[0].after);
        EXPECT_EQ(after.resource.stages, PipelineStage::Host);
        EXPECT_EQ(after.resource.access, Access::HostRead);
        pass("unsafe immediate host read", id, ResourceUsage::HostRead);
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
        EXPECT_FALSE(resolve_image_state(ResourceUsage::HostRead,
            {.aspects = Flags<ImageAspect>(ImageAspect::Color)}));
        EXPECT_EQ(
            resolve_resource_state(ResourceUsage::HostWrite)->access, Access::HostWrite);
    }
    TEST_F(RenderGraphTest, CompiledPlanIsIndependentFromSubsequentBuilderChanges) {
        const auto id = buffer();
        pass("fill", id, ResourceUsage::TransferDestination);
        const auto first = graph.compile();
        pass("copy", id, ResourceUsage::TransferSource);
        const auto second = graph.compile();
        EXPECT_EQ(first.get_passes().size(), 1);
        EXPECT_EQ(second.get_passes().size(), 2);
        EXPECT_EQ(std::get<RenderGraph::BufferState>(first.get_final_states()[0])
                      .resource.access,
            Access::TransferWrite);
    }

    TEST_F(RenderGraphTest, ExportCannotInventContentsAndPreservesTheActualProducer) {
        const auto id = buffer();
        graph.export_resource({id, ResourceUsage::TransferDestination, {}});
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
        pass("actual write", id, ResourceUsage::TransferDestination);
        graph.export_resource({id, ResourceUsage::StorageReadWrite, fragment});
        EXPECT_THROW(static_cast<void>(graph.compile()), std::invalid_argument);
        graph = {};
        const auto produced = buffer();
        pass("actual write", produced, ResourceUsage::TransferDestination);
        graph.export_resource({produced, ResourceUsage::StorageReadWrite, fragment});
        const auto plan = graph.compile();
        const auto state = std::get<RenderGraph::BufferState>(plan.get_final_states()[0]);
        EXPECT_TRUE(static_cast<bool>(state.resource.stages & PipelineStage::Transfer));
        EXPECT_TRUE(static_cast<bool>(state.resource.access & Access::TransferWrite));
    }
}
