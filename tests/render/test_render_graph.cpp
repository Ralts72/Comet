#include "render/render_graph.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class RenderGraphTest: public testing::Test {
    protected:
        RenderGraph graph;
        const Flags<PipelineStage> vertex{PipelineStage::VertexShader};
        const Flags<PipelineStage> fragment{PipelineStage::FragmentShader};
        static ImageState image_state(ResourceUsage usage = ResourceUsage::Undefined) {
            return *resolve_image_state(usage, {.aspects = Flags<ImageAspect>(ImageAspect::Color)});
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
        ASSERT_TRUE(plan) << plan.error();
        const auto passes = plan.value().get_passes();
        ASSERT_EQ(passes.size(), 7);
        EXPECT_TRUE(passes[0].barriers.empty());
        ASSERT_EQ(passes[1].barriers.size(), 1);
        EXPECT_TRUE(passes[2].barriers.empty());
        EXPECT_EQ(passes[3].barriers.size(), 1);
        EXPECT_EQ(passes[4].barriers.size(), 1);
        ASSERT_EQ(passes[5].barriers.size(), 1);
        EXPECT_EQ(passes[6].barriers.size(), 1);
        const auto& before = std::get<RenderGraph::BufferState>(passes[5].barriers[0].before);
        EXPECT_TRUE(static_cast<bool>(before.resource.stages & PipelineStage::VertexShader));
        EXPECT_TRUE(static_cast<bool>(before.resource.stages & PipelineStage::FragmentShader));
        EXPECT_EQ(before.offset, 16);
        EXPECT_EQ(before.size, 32);
    }
    TEST_F(RenderGraphTest, ReadOnlyImportsNeedNoReadReadBarrierButWriteWaitsForAllReads) {
        const auto id = buffer(*resolve_resource_state(ResourceUsage::UniformRead, vertex));
        pass("vertex", id, ResourceUsage::UniformRead, vertex);
        pass("fragment", id, ResourceUsage::StorageRead, fragment);
        pass("write", id, ResourceUsage::StorageReadWrite, fragment);
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        EXPECT_TRUE(plan.value().get_passes()[0].barriers.empty());
        EXPECT_TRUE(plan.value().get_passes()[1].barriers.empty());
        ASSERT_EQ(plan.value().get_passes()[2].barriers.size(), 1);
        const auto& state =
            std::get<RenderGraph::BufferState>(plan.value().get_passes()[2].barriers[0].before);
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
        ASSERT_TRUE(plan) << plan.error();
        ASSERT_EQ(plan.value().get_passes()[0].barriers.size(), 1);
        EXPECT_EQ(std::get<ImageState>(plan.value().get_passes()[0].barriers[0].before).layout,
            ImageLayout::Undefined);
        ASSERT_EQ(plan.value().get_passes()[1].barriers.size(), 1);
        EXPECT_TRUE(plan.value().get_passes()[2].barriers.empty());
        ASSERT_EQ(plan.value().get_exports().size(), 1);
        const auto final = std::get<ImageState>(plan.value().get_final_states()[0]);
        EXPECT_EQ(final.layout, ImageLayout::TransferSrcOptimal);
        EXPECT_EQ(final.subresources, initial.subresources);
        EXPECT_TRUE(static_cast<bool>(final.resource.access & Access::TransferWrite));
    }
    TEST_F(RenderGraphTest, DiscardPreservesPriorAccessWithoutMakingContentsReadable) {
        auto initial = image_state(ResourceUsage::ColorAttachmentWrite);
        initial.resource.stages |= PipelineStage::FragmentShader;
        initial.layout = ImageLayout::Undefined;
        const auto id = graph.import_image("reused target", initial);
        pass("clear", id, ResourceUsage::ColorAttachmentWrite);
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        ASSERT_EQ(plan.value().get_passes()[0].barriers.size(), 1u);
        EXPECT_EQ(std::get<ImageState>(plan.value().get_passes()[0].barriers[0].before), initial);

        graph = {};
        const auto discarded = graph.import_image("discarded target", initial);
        pass("invalid read", discarded, ResourceUsage::SampledRead, fragment);
        EXPECT_FALSE(graph.compile());
    }

    TEST_F(RenderGraphTest, RejectsUninitializedReadersAndReadModifyWrite) {
        const auto id = buffer();
        pass("invalid read", id, ResourceUsage::UniformRead, vertex);
        EXPECT_FALSE(graph.compile());
        RenderGraph storage;
        const auto image = storage.import_image("empty", image_state());
        storage.add_pass({"rmw", {{image, ResourceUsage::StorageReadWrite, fragment}}});
        EXPECT_FALSE(storage.compile());
    }
    TEST_F(RenderGraphTest, RejectsMissingStageWrongKindAndDuplicateUseBeforeRecording) {
        const auto id = buffer(*resolve_resource_state(ResourceUsage::TransferDestination));
        pass("missing stages", id, ResourceUsage::StorageRead);
        EXPECT_FALSE(graph.compile());
        graph = {};
        const auto second = buffer();
        pass("image use", second, ResourceUsage::ColorAttachmentWrite);
        EXPECT_FALSE(graph.compile());
        graph = {};
        const auto third = buffer();
        graph.add_pass({"duplicate", {{third, ResourceUsage::TransferDestination, {}},
                                         {third, ResourceUsage::TransferSource, {}}}});
        EXPECT_FALSE(graph.compile());
        graph = {};
        pass("foreign index", {55}, ResourceUsage::TransferDestination);
        EXPECT_FALSE(graph.compile());
    }
    TEST_F(RenderGraphTest, RequiresExplicitSameQueueOwnershipAndValidImportedRanges) {
        auto first = image_state();
        first.resource.queue_family = 0;
        static_cast<void>(graph.import_image("first", first));
        first.resource.queue_family = 1;
        static_cast<void>(graph.import_image("second", first));
        EXPECT_FALSE(graph.compile());
        graph = {};
        static_cast<void>(graph.import_buffer("overflow", {{}, UINT64_MAX, 2}));
        EXPECT_FALSE(graph.compile());
        graph = {};
        static_cast<void>(graph.import_buffer("zero", {{}, 0, 0}));
        EXPECT_FALSE(graph.compile());
        graph = {};
        first.subresources.base_mip_level = UINT32_MAX;
        static_cast<void>(graph.import_image("bad mip", first));
        EXPECT_FALSE(graph.compile());
        graph = {};
        static_cast<void>(graph.import_image("", image_state()));
        EXPECT_FALSE(graph.compile());
    }
    TEST_F(RenderGraphTest, RejectsAmbiguousNamesAndExcessiveDeclarations) {
        static_cast<void>(buffer());
        static_cast<void>(buffer());
        EXPECT_FALSE(graph.compile());
        graph = {};
        graph.add_pass({"", {}});
        EXPECT_FALSE(graph.compile());
        graph = {};
        graph.add_pass({"same", {}});
        graph.add_pass({"same", {}});
        EXPECT_FALSE(graph.compile());
        graph = {};
        for(unsigned index = 0; index < 257; ++index)
            static_cast<void>(graph.import_buffer(std::to_string(index), {{}, 0, 16}));
        EXPECT_FALSE(graph.compile());
        graph = {};
        for(unsigned index = 0; index < 513; ++index)
            graph.add_pass({std::to_string(index), {}});
        EXPECT_FALSE(graph.compile());
    }

    TEST_F(RenderGraphTest, HostReadsAreExportsAfterGpuCompletionNotPassCallbacks) {
        const auto id = buffer();
        pass("gpu fill", id, ResourceUsage::TransferDestination);
        graph.export_resource({id, ResourceUsage::HostRead, {}});
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        ASSERT_EQ(plan.value().get_exports().size(), 1);
        const auto after = std::get<RenderGraph::BufferState>(plan.value().get_exports()[0].after);
        EXPECT_EQ(after.resource.stages, PipelineStage::Host);
        EXPECT_EQ(after.resource.access, Access::HostRead);
        pass("unsafe immediate host read", id, ResourceUsage::HostRead);
        EXPECT_FALSE(graph.compile());
        EXPECT_FALSE(resolve_image_state(
            ResourceUsage::HostRead, {.aspects = Flags<ImageAspect>(ImageAspect::Color)}));
        EXPECT_EQ(resolve_resource_state(ResourceUsage::HostWrite)->access, Access::HostWrite);
    }
    TEST_F(RenderGraphTest, CompiledPlanIsIndependentFromSubsequentBuilderChanges) {
        const auto id = buffer();
        pass("fill", id, ResourceUsage::TransferDestination);
        const auto first = graph.compile();
        ASSERT_TRUE(first) << first.error();
        pass("copy", id, ResourceUsage::TransferSource);
        const auto second = graph.compile();
        ASSERT_TRUE(second) << second.error();
        EXPECT_EQ(first.value().get_passes().size(), 1);
        EXPECT_EQ(second.value().get_passes().size(), 2);
        EXPECT_EQ(
            std::get<RenderGraph::BufferState>(first.value().get_final_states()[0]).resource.access,
            Access::TransferWrite);
    }

    TEST_F(RenderGraphTest, ExportCannotInventContentsAndPreservesTheActualProducer) {
        const auto id = buffer();
        graph.export_resource({id, ResourceUsage::TransferDestination, {}});
        EXPECT_FALSE(graph.compile());
        pass("actual write", id, ResourceUsage::TransferDestination);
        graph.export_resource({id, ResourceUsage::StorageReadWrite, fragment});
        EXPECT_FALSE(graph.compile());
        graph = {};
        const auto produced = buffer();
        pass("actual write", produced, ResourceUsage::TransferDestination);
        graph.export_resource({produced, ResourceUsage::StorageReadWrite, fragment});
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        const auto state = std::get<RenderGraph::BufferState>(plan.value().get_final_states()[0]);
        EXPECT_TRUE(static_cast<bool>(state.resource.stages & PipelineStage::Transfer));
        EXPECT_TRUE(static_cast<bool>(state.resource.access & Access::TransferWrite));
    }

    TEST_F(RenderGraphTest, PlansByUseOrderAndKeepsOverlappingAndExportedContentsIndependent) {
        const RenderGraph::ImageDescription description{Format::R16G16B16A16_SFLOAT, 64, 32};
        const auto c = graph.create_image("later", description);
        const auto a = graph.create_image("first", description);
        const auto b = graph.create_image("overlap", description);
        const auto d = graph.create_image("last", description);
        pass("produce a", a, ResourceUsage::ColorAttachmentWrite);
        graph.add_pass({"a to b", {{a, ResourceUsage::SampledRead, fragment},
                                      {b, ResourceUsage::ColorAttachmentWrite, {}}}});
        graph.add_pass({"b to c", {{b, ResourceUsage::SampledRead, fragment},
                                      {c, ResourceUsage::ColorAttachmentWrite, {}}}});
        pass("produce d", d, ResourceUsage::ColorAttachmentWrite);
        graph.export_resource({c, ResourceUsage::SampledRead, fragment});
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        const auto lifetimes = plan.value().get_lifetimes();
        ASSERT_EQ(plan.value().get_allocations().size(), 2u);
        EXPECT_EQ(lifetimes[c.index].first, 2u);
        EXPECT_EQ(lifetimes[c.index].last, 4u);
        EXPECT_EQ(lifetimes[a.index].allocation, lifetimes[c.index].allocation);
        EXPECT_EQ(lifetimes[b.index].allocation, lifetimes[d.index].allocation);
        EXPECT_NE(lifetimes[a.index].allocation, lifetimes[b.index].allocation);
        const auto& barriers = plan.value().get_passes()[2].barriers;
        const auto transition = std::ranges::find(barriers, c, &RenderGraph::Barrier::resource);
        ASSERT_NE(transition, barriers.end());
        const auto before = std::get<ImageState>(transition->before);
        EXPECT_EQ(before.layout, ImageLayout::Undefined);
        EXPECT_TRUE(static_cast<bool>(before.resource.stages & PipelineStage::FragmentShader));
        EXPECT_TRUE(static_cast<bool>(before.resource.access & Access::ShaderRead));
        const auto independent = graph.compile(false);
        ASSERT_TRUE(independent);
        EXPECT_EQ(independent.value().get_allocations().size(), 4u);
    }

    TEST_F(RenderGraphTest, BufferPlanningUnionsUsageAndRequiresExactDescriptions) {
        const auto a = graph.create_buffer("a", {64});
        const auto b = graph.create_buffer("b", {64});
        const auto larger = graph.create_buffer("larger", {128});
        pass("fill a", a, ResourceUsage::TransferDestination);
        pass("read a", a, ResourceUsage::UniformRead, vertex);
        pass("fill b", b, ResourceUsage::TransferDestination);
        pass("read b", b, ResourceUsage::StorageRead, fragment);
        pass("fill larger", larger, ResourceUsage::TransferDestination);
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        ASSERT_EQ(plan.value().get_allocations().size(), 2u);
        const auto flags = plan.value().get_allocations()[0].buffer_usage;
        EXPECT_TRUE(static_cast<bool>(flags & BufferUsage::CopyDst));
        EXPECT_TRUE(static_cast<bool>(flags & BufferUsage::Uniform));
        EXPECT_TRUE(static_cast<bool>(flags & BufferUsage::Storage));
        const auto before = std::get<RenderGraph::BufferState>(
            plan.value().get_passes()[2].barriers.front().before);
        EXPECT_TRUE(static_cast<bool>(before.resource.stages & PipelineStage::VertexShader));
        EXPECT_TRUE(static_cast<bool>(before.resource.access & Access::UniformRead));
    }

    TEST_F(RenderGraphTest, TransientsRejectMissingProducersAndInvalidDescriptions) {
        auto id = graph.create_image("empty", {Format::R8G8B8A8_UNORM, 8, 8});
        pass("read", id, ResourceUsage::SampledRead, fragment);
        EXPECT_FALSE(graph.compile());
        graph = {};
        id = graph.create_buffer("rmw", {64});
        pass("rmw", id, ResourceUsage::StorageReadWrite, fragment);
        EXPECT_FALSE(graph.compile());
        graph = {};
        static_cast<void>(graph.create_image("unused", {Format::R8G8B8A8_UNORM, 8, 8}));
        EXPECT_FALSE(graph.compile());
        graph = {};
        id = graph.create_image("zero width", {Format::R8G8B8A8_UNORM, 0, 8});
        pass("write", id, ResourceUsage::ColorAttachmentWrite);
        EXPECT_FALSE(graph.compile());
        graph = {};
        id = graph.create_buffer("zero size", {0});
        pass("write", id, ResourceUsage::TransferDestination);
        EXPECT_FALSE(graph.compile());
    }
}
