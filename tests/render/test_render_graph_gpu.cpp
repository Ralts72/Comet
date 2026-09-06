#include "render/render_graph.h"
#include "core/engine.h"
#include "graphics/resource/image.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "diagnostics/logger.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <cstring>
#include <cstdlib>
#include <sstream>

namespace Comet::Tests {
    class RenderGraphGpuTest: public testing::Test {
    protected:
        // 测试专用 host-coherent 读回 owner，不改变生产 Buffer 的映射策略。
        class Readback final: public Buffer {
        public:
            Readback(Device& device, vk::PhysicalDevice physical, uint64_t size)
                : Buffer(device, size) {
                native = device.get().createBufferUnique({{}, size,
                    vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive});
                const auto requirements =
                    device.get().getBufferMemoryRequirements(*native);
                const auto properties = physical.getMemoryProperties();
                const auto required = vk::MemoryPropertyFlagBits::eHostVisible
                                      | vk::MemoryPropertyFlagBits::eHostCoherent;
                std::optional<uint32_t> type;
                for(uint32_t index = 0; index < properties.memoryTypeCount; ++index)
                    if((requirements.memoryTypeBits & (1u << index))
                        && (properties.memoryTypes[index].propertyFlags & required)
                               == required) {
                        type = index;
                        break;
                    }
                if(!type)
                    throw std::runtime_error("No host-coherent readback memory");
                memory = device.get().allocateMemoryUnique({requirements.size, *type});
                device.get().bindBufferMemory(*native, *memory, 0);
                m_buffer = *native;
            }
            std::vector<std::byte> read() const {
                std::vector<std::byte> result(m_size);
                const auto* data = m_device.get().mapMemory(*memory, 0, VK_WHOLE_SIZE);
                std::memcpy(result.data(), data, result.size());
                m_device.get().unmapMemory(*memory);
                return result;
            }

        private:
            vk::UniqueDeviceMemory memory;
            vk::UniqueBuffer native;
        };
        std::unique_ptr<Engine> engine;
        std::ostringstream messages;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink;
        void SetUp() override {
            sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            sink->set_level(spdlog::level::err);
            Logger::add_custom_sink(sink);
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            config.vulkan.msaa_samples = SampleCount::Count1;
            engine = std::make_unique<Engine>(config);
        }
        void TearDown() override {
            engine.reset();
            if(auto logger = Logger::get_console_logger())
                std::erase(logger->sinks(), sink);
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos)
                << messages.str();
            EXPECT_TRUE(messages.str().empty()) << messages.str();
        }
        static void submit(Device& device, FrameScheduler& frames) {
            auto& commands = frames.get_current_command_buffer();
            commands.end();
            static_cast<void>(
                device.get_graphics_queue().submit2({}, std::span(&commands, 1), {},
                    &frames.get_current_frame_slot().in_flight_fence));
            frames.record_submission();
            frames.end_frame();
        }
        struct FrameWait {
            Device& device;
            FrameScheduler& frames;
            ~FrameWait() {
                if(frames.is_recording_frame())
                    RenderGraphGpuTest::submit(device, frames);
                frames.wait_for_all_slots();
            }
        };
    };

    TEST_F(RenderGraphGpuTest, ExecutesFourPassesOnDisjointMipsLayersAndBufferRanges) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto image = Image::create(device,
            {.format = Format::R8G8B8A8_UNORM,
                .extent = {4, 4, 1},
                .usage = Flags<ImageUsage>(ImageUsage::CopySrc) | ImageUsage::CopyDst,
                .mip_levels = 2,
                .array_layers = 2});
        auto data = Buffer::create_gpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::CopySrc), 32);
        auto output = std::make_shared<Readback>(
            device, context.get_context().get_physical_device(), 192);
        RenderGraph graph;
        const auto a = graph.import_image(
            "mip0 layer0", *resolve_image_state(ResourceUsage::Undefined,
                               {.aspects = Flags<ImageAspect>(ImageAspect::Color)}));
        const auto b = graph.import_image(
            "mip1 layer1", *resolve_image_state(ResourceUsage::Undefined,
                               {.aspects = Flags<ImageAspect>(ImageAspect::Color),
                                   .base_mip_level = 1,
                                   .level_count = 1,
                                   .base_array_layer = 1,
                                   .layer_count = 1}));
        const auto left = graph.import_buffer("left", {{}, 0, 16});
        const auto right = graph.import_buffer("right", {{}, 16, 16});
        const auto host = graph.import_buffer("readback", {{}, 0, 192});
        graph.add_pass({"produce", {{a, ResourceUsage::TransferDestination, {}},
                                       {b, ResourceUsage::TransferDestination, {}},
                                       {left, ResourceUsage::TransferDestination, {}},
                                       {right, ResourceUsage::TransferDestination, {}}}});
        graph.add_pass({"consume", {{a, ResourceUsage::TransferSource, {}},
                                       {b, ResourceUsage::TransferSource, {}},
                                       {left, ResourceUsage::TransferSource, {}},
                                       {right, ResourceUsage::TransferSource, {}},
                                       {host, ResourceUsage::TransferDestination, {}}}});
        graph.add_pass(
            {"overwrite", {{a, ResourceUsage::TransferDestination, {}},
                              {left, ResourceUsage::TransferDestination, {}}}});
        graph.add_pass(
            {"consume new", {{a, ResourceUsage::TransferSource, {}},
                                {left, ResourceUsage::TransferSource, {}},
                                {host, ResourceUsage::TransferDestination, {}}}});
        graph.export_resource({host, ResourceUsage::HostRead, {}});
        const auto plan = graph.compile();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        FrameWait wait{device, frames};
        std::vector<RenderGraph::Binding> bindings{
            image, image, data, data, std::static_pointer_cast<Buffer>(output)};
        unsigned recorded = 0;
        plan.record(frames, bindings, [&](size_t pass, const CommandBuffer& commands) {
            ++recorded;
            auto cmd = commands.get();
            const vk::ImageSubresourceRange range_a(
                vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            const vk::ImageSubresourceRange range_b(
                vk::ImageAspectFlagBits::eColor, 1, 1, 1, 1);
            auto copy_image = [&](uint32_t mip, uint32_t layer, uint32_t side,
                                  uint64_t offset) {
                vk::BufferImageCopy region;
                region.bufferOffset = offset;
                region.imageSubresource = {
                    vk::ImageAspectFlagBits::eColor, mip, layer, 1};
                region.imageExtent = vk::Extent3D(side, side, 1);
                cmd.copyImageToBuffer(image->get(), vk::ImageLayout::eTransferSrcOptimal,
                    output->get(), region);
            };
            if(pass == 0) {
                cmd.clearColorImage(image->get(), vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue(std::array<float, 4>{1, 0, 0, 1}), range_a);
                cmd.clearColorImage(image->get(), vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue(std::array<float, 4>{0, 1, 0, 1}), range_b);
                cmd.fillBuffer(data->get(), 0, 16, 0x11223344);
                cmd.fillBuffer(data->get(), 16, 16, 0x55667788);
            } else if(pass == 1) {
                copy_image(0, 0, 4, 0);
                copy_image(1, 1, 2, 64);
                cmd.copyBuffer(data->get(), output->get(), vk::BufferCopy(0, 80, 32));
            } else if(pass == 2) {
                cmd.clearColorImage(image->get(), vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue(std::array<float, 4>{0, 0, 1, 1}), range_a);
                cmd.fillBuffer(data->get(), 0, 16, 0xaabbccdd);
            } else {
                copy_image(0, 0, 4, 112);
                cmd.copyBuffer(data->get(), output->get(), vk::BufferCopy(0, 176, 16));
            }
        });
        EXPECT_EQ(recorded, 4u);
        submit(device, frames);
        auto old_image = std::weak_ptr(image);
        auto old_buffer = std::weak_ptr(data);
        bindings.clear();
        image.reset();
        data.reset();
        EXPECT_FALSE(old_image.expired());
        EXPECT_FALSE(old_buffer.expired());
        frames.wait_for_all_slots();
        EXPECT_TRUE(old_image.expired());
        EXPECT_TRUE(old_buffer.expired());
        const auto bytes = output->read();
        auto color = [&](size_t offset, size_t count, std::array<std::byte, 4> expected) {
            for(size_t pixel = 0; pixel < count; ++pixel)
                for(size_t channel = 0; channel < 4; ++channel)
                    EXPECT_EQ(bytes[offset + pixel * 4 + channel], expected[channel]);
        };
        color(0, 16, {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}});
        color(64, 4, {std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}});
        color(112, 16, {std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}});
        for(const auto [offset, value] : {std::pair(80u, 0x11223344u),
                std::pair(96u, 0x55667788u), std::pair(176u, 0xaabbccddu)})
            for(size_t index = 0; index < 4; ++index) {
                uint32_t actual;
                std::memcpy(&actual, bytes.data() + offset + index * 4, 4);
                EXPECT_EQ(actual, value);
            }
    }

    TEST_F(RenderGraphGpuTest, RejectsBindingsAndOverlappingAliasesBeforeCallingPass) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto buffer = Buffer::create_gpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::CopySrc), 32);
        RenderGraph graph;
        const auto a = graph.import_buffer("a", {{}, 0, 24});
        const auto b = graph.import_buffer("b", {{}, 16, 16});
        graph.add_pass({"writes", {{a, ResourceUsage::TransferDestination, {}},
                                      {b, ResourceUsage::TransferDestination, {}}}});
        const auto plan = graph.compile();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        std::vector<RenderGraph::Binding> bindings{buffer, buffer};
        unsigned calls = 0;
        auto record = [&](size_t, const CommandBuffer&) { ++calls; };
        EXPECT_THROW(plan.record(frames, bindings, record), std::invalid_argument);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        EXPECT_THROW(plan.record(frames, bindings, record), std::invalid_argument);
        bindings.pop_back();
        EXPECT_THROW(plan.record(frames, bindings, record), std::invalid_argument);
        bindings.push_back(std::shared_ptr<Buffer>{});
        EXPECT_THROW(plan.record(frames, bindings, record), std::invalid_argument);
        EXPECT_EQ(calls, 0u);
        submit(device, frames);
        frames.wait_for_all_slots();
    }

    TEST_F(RenderGraphGpuTest, HandsStateToNextSubmissionWithoutGlobalImageState) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto data = Buffer::create_gpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::CopySrc), 16);
        auto output = std::make_shared<Readback>(
            device, context.get_context().get_physical_device(), 16);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        RenderGraph producer;
        const auto source = producer.import_buffer("source", {{}, 0, 16});
        producer.add_pass({"fill", {{source, ResourceUsage::TransferDestination, {}}}});
        producer.export_resource({source, ResourceUsage::TransferSource, {}});
        const auto first = producer.compile();
        FrameWait wait{device, frames};
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        const std::vector<RenderGraph::Binding> first_bindings{data};
        first.record(frames, first_bindings, [&](size_t, const CommandBuffer& commands) {
            commands.get().fillBuffer(data->get(), 0, 16, 0x10203040);
        });
        submit(device, frames);
        RenderGraph consumer;
        const auto imported = consumer.import_buffer(
            "source", std::get<RenderGraph::BufferState>(first.get_final_states()[0]));
        const auto host = consumer.import_buffer("output", {{}, 0, 16});
        consumer.add_pass({"copy", {{imported, ResourceUsage::TransferSource, {}},
                                       {host, ResourceUsage::TransferDestination, {}}}});
        consumer.export_resource({host, ResourceUsage::HostRead, {}});
        frames.wait_for_current_slot();
        frames.begin_frame(1);
        frames.get_current_command_buffer().begin();
        const std::vector<RenderGraph::Binding> bindings{
            data, std::static_pointer_cast<Buffer>(output)};
        consumer.compile().record(
            frames, bindings, [&](size_t, const CommandBuffer& commands) {
                commands.get().copyBuffer(
                    data->get(), output->get(), vk::BufferCopy(0, 0, 16));
            });
        submit(device, frames);
        frames.wait_for_all_slots();
        const auto bytes = output->read();
        for(size_t index = 0; index < 4; ++index) {
            uint32_t value;
            std::memcpy(&value, bytes.data() + index * 4, 4);
            EXPECT_EQ(value, 0x10203040u);
        }
    }

    TEST_F(RenderGraphGpuTest, OffscreenSceneExportsSampledLayoutAcrossMsaaAndResize) {
        for(const auto samples : {SampleCount::Count1, SampleCount::Count4}) {
            engine.reset();
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            config.vulkan.msaa_samples = samples;
            engine = std::make_unique<Engine>(config);
            auto& renderer = engine->get_renderer();
            renderer.enable_offscreen_rendering({32, 32});
            auto& context = renderer.get_render_context();
            auto& scene_renderer = renderer.get_scene_renderer();
            const auto format =
                context.get_swapchain().get_images().front()->get_info().format;
            RenderPass presentation(context.get_device(),
                {Attachment::get_color_attachment(format, SampleCount::Count1)},
                {{{}, {SubpassColorAttachment(0)}, {}}}, format);
            auto target = RenderTarget::create_swapchain_target(
                context.get_device(), presentation, context.get_swapchain());
            struct Wait {
                RenderContext& context;
                ~Wait() { context.wait_idle(); }
            } wait{context};
            renderer.set_overlay_callbacks({}, [&](CommandBuffer& commands) {
                auto view = scene_renderer.get_offscreen_color_view(
                    scene_renderer.get_frame_scheduler().get_current_frame_slot_index());
                // 作为外部消费者声明已导出的 layout，让 validation 核对实际状态。
                vk::ImageMemoryBarrier2 barrier;
                barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
                barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
                barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
                barrier.oldLayout = barrier.newLayout =
                    vk::ImageLayout::eShaderReadOnlyOptimal;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
                    VK_QUEUE_FAMILY_IGNORED;
                barrier.image = view->get_image()->get();
                barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
                commands.get().pipelineBarrier2(
                    vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
                target->begin_render_target(commands);
                target->end_render_target(commands);
            });
            RenderScene scene;
            scene.cameras.push_back(RenderCamera{.primary = true});
            for(unsigned frame = 0; frame < 4; ++frame) {
                if(frame == 2)
                    renderer.set_render_view({.render_size = {40, 24}});
                engine->get_window().poll_events();
                LineDrawList lines;
                ASSERT_TRUE(lines.add_line({-0.5f, 0, -2}, {0.5f, 0, -2}, {1, 0, 0, 1}));
                renderer.submit_lines(lines);
                ASSERT_TRUE(renderer.prepare_frame());
                renderer.render_frame(scene);
            }
            context.wait_idle();
            renderer.set_overlay_callbacks({}, {});
        }
    }

    TEST_F(RenderGraphGpuTest, RejectsImageBoundsUsageKindAliasesAndForeignQueue) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto image =
            Image::create(device, {.format = Format::R8G8B8A8_UNORM,
                                      .extent = {4, 4, 1},
                                      .usage = Flags<ImageUsage>(ImageUsage::CopyDst)});
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        unsigned calls = 0;
        auto record = [&](size_t, const CommandBuffer&) { ++calls; };
        for(unsigned mode = 0; mode < 5; ++mode) {
            RenderGraph graph;
            auto state = *resolve_image_state(ResourceUsage::Undefined,
                {.aspects = Flags<ImageAspect>(ImageAspect::Color)});
            if(mode == 0)
                state.subresources.base_mip_level = 1;
            if(mode == 4)
                state.resource.queue_family = frames.get_queue_family_index() + 1;
            const auto id = graph.import_image("image", state);
            graph.add_pass({"write", {{id, ResourceUsage::TransferDestination, {}}}});
            std::vector<RenderGraph::Binding> bindings{image};
            if(mode == 1)
                graph.export_resource({id, ResourceUsage::SampledRead,
                    Flags<PipelineStage>(PipelineStage::FragmentShader)});
            if(mode == 2)
                bindings[0] = std::shared_ptr<Buffer>{};
            if(mode == 3) {
                static_cast<void>(graph.import_image("alias", state));
                bindings.push_back(image);
            }
            EXPECT_THROW(
                graph.compile().record(frames, bindings, record), std::invalid_argument)
                << mode;
        }
        EXPECT_EQ(calls, 0u);
        submit(device, frames);
        frames.wait_for_all_slots();
    }

    TEST_F(
        RenderGraphGpuTest, ConfirmsSyncValidationWithUnsubmittedMissingBarrierControl) {
        const auto* enabled = std::getenv("VK_LAYER_ENABLES");
        if(!enabled
            || std::string_view(enabled).find("SYNCHRONIZATION_VALIDATION")
                   == std::string_view::npos)
            GTEST_SKIP() << "Runs under render_graph_sync_validation CTest environment";
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto source = Buffer::create_gpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::CopySrc), 16);
        auto destination = Buffer::create_gpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::CopyDst), 16);
        EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos);
        auto& pool = device.get_default_command_pool();
        auto commands =
            device.get()
                .allocateCommandBuffers({pool.get(), vk::CommandBufferLevel::ePrimary, 1})
                .front();
        commands.begin(vk::CommandBufferBeginInfo{});
        commands.fillBuffer(source->get(), 0, 16, 0x11223344);
        // 故意缺少 transfer write → read，只录制对照，不提交这组无效命令。
        commands.copyBuffer(source->get(), destination->get(), vk::BufferCopy(0, 0, 16));
        commands.end();
        device.get().freeCommandBuffers(pool.get(), commands);
        EXPECT_TRUE(messages.str().find("READ_AFTER_WRITE") != std::string::npos
                    || messages.str().find("READ-AFTER-WRITE") != std::string::npos)
            << "Synchronization validation was not active: " << messages.str();
        EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
        messages.str({});
        messages.clear();
    }
}
