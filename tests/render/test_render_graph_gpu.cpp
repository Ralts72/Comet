#include "support/render_graph_gpu_fixture.h"

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, ExecutesFourPassesOnDisjointMipsLayersAndBufferRanges) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto image = Image::create(
            device, {.format = Format::R8G8B8A8_UNORM,
                        .extent = {4, 4, 1},
                        .usage = Flags<ImageUsage>(ImageUsage::CopySrc) | ImageUsage::CopyDst,
                        .mip_levels = 2,
                        .array_layers = 2});
        auto data = Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::CopySrc), 32);
        auto output =
            std::make_shared<Readback>(device, context.get_context().get_physical_device(), 192);
        ASSERT_TRUE(output->get()) << "Host-coherent readback memory unavailable";
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
        const auto produce =
            graph.add_pass({"produce", {{a, ResourceUsage::TransferDestination, {}},
                                           {b, ResourceUsage::TransferDestination, {}},
                                           {left, ResourceUsage::TransferDestination, {}},
                                           {right, ResourceUsage::TransferDestination, {}}}});
        const auto consume = graph.add_pass({"consume",
            {{a, ResourceUsage::TransferSource, {}}, {b, ResourceUsage::TransferSource, {}},
                {left, ResourceUsage::TransferSource, {}},
                {right, ResourceUsage::TransferSource, {}},
                {host, ResourceUsage::TransferDestination, {}}}});
        const auto overwrite =
            graph.add_pass({"overwrite", {{a, ResourceUsage::TransferDestination, {}},
                                             {left, ResourceUsage::TransferDestination, {}}}});
        const auto consume_new = graph.add_pass({"consume new",
            {{a, ResourceUsage::TransferSource, {}}, {left, ResourceUsage::TransferSource, {}},
                {host, ResourceUsage::TransferDestination, {}}}});
        graph.export_resource({host, ResourceUsage::HostRead, {}});
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        FrameWait wait{device, frames};
        std::vector<RenderGraph::Binding> bindings{
            image, image, data, data, std::static_pointer_cast<Buffer>(output)};
        std::vector<RenderGraph::PassId> recorded;
        ASSERT_TRUE(plan.value().record(
            frames, bindings, [&](RenderGraph::PassId pass, const CommandBuffer& commands) {
                recorded.push_back(pass);
                auto cmd = commands.get();
                const vk::ImageSubresourceRange range_a(
                    vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                const vk::ImageSubresourceRange range_b(
                    vk::ImageAspectFlagBits::eColor, 1, 1, 1, 1);
                auto copy_image = [&](uint32_t mip, uint32_t layer, uint32_t side,
                                      uint64_t offset) {
                    vk::BufferImageCopy region;
                    region.bufferOffset = offset;
                    region.imageSubresource = {vk::ImageAspectFlagBits::eColor, mip, layer, 1};
                    region.imageExtent = vk::Extent3D(side, side, 1);
                    cmd.copyImageToBuffer(
                        image->get(), vk::ImageLayout::eTransferSrcOptimal, output->get(), region);
                };
                if(pass == produce) {
                    cmd.clearColorImage(image->get(), vk::ImageLayout::eTransferDstOptimal,
                        vk::ClearColorValue(std::array<float, 4>{1, 0, 0, 1}), range_a);
                    cmd.clearColorImage(image->get(), vk::ImageLayout::eTransferDstOptimal,
                        vk::ClearColorValue(std::array<float, 4>{0, 1, 0, 1}), range_b);
                    cmd.fillBuffer(data->get(), 0, 16, 0x11223344);
                    cmd.fillBuffer(data->get(), 16, 16, 0x55667788);
                } else if(pass == consume) {
                    copy_image(0, 0, 4, 0);
                    copy_image(1, 1, 2, 64);
                    cmd.copyBuffer(data->get(), output->get(), vk::BufferCopy(0, 80, 32));
                } else if(pass == overwrite) {
                    cmd.clearColorImage(image->get(), vk::ImageLayout::eTransferDstOptimal,
                        vk::ClearColorValue(std::array<float, 4>{0, 0, 1, 1}), range_a);
                    cmd.fillBuffer(data->get(), 0, 16, 0xaabbccdd);
                } else if(pass == consume_new) {
                    copy_image(0, 0, 4, 112);
                    cmd.copyBuffer(data->get(), output->get(), vk::BufferCopy(0, 176, 16));
                } else {
                    ADD_FAILURE() << "Unexpected pass ID: " << pass;
                }
                return Result<void, GraphicsError>::success();
            }));
        EXPECT_EQ(recorded, (std::vector{produce, consume, overwrite, consume_new}));
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
        for(const auto [offset, value] : {std::pair(80u, 0x11223344u), std::pair(96u, 0x55667788u),
                std::pair(176u, 0xaabbccddu)})
            for(size_t index = 0; index < 4; ++index) {
                uint32_t actual;
                std::memcpy(&actual, bytes.data() + offset + index * 4, 4);
                EXPECT_EQ(actual, value);
            }
    }

    TEST_F(RenderGraphGpuTest, StopsAtFailedRecorderAndPreservesNativeError) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        RenderGraph graph;
        graph.add_pass({"first", {}});
        graph.add_pass({"must not run", {}});
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        unsigned calls = 0;
        const auto recorded =
            plan.value().record(frames, {}, [&](size_t pass, CommandBuffer& commands) {
                EXPECT_EQ(pass, 0u);
                EXPECT_EQ(&commands, &frames.get_current_command_buffer());
                ++calls;
                return Result<void, GraphicsError>::failure(
                    {"recorder failed", vk::Result::eErrorDeviceLost});
            });
        ASSERT_FALSE(recorded);
        EXPECT_EQ(calls, 1u);
        EXPECT_TRUE(recorded.error().is_device_lost());
        EXPECT_EQ(recorded.error().message, "recorder failed");
        EXPECT_EQ(frames.get_current_frame_slot().last_submission_serial, 0u);
        // 模拟录制失败后退出：丢弃命令，不提交不完整帧。
        frames.get_current_command_buffer().end();
        frames.get_current_command_buffer().get().reset();
    }

    TEST_F(RenderGraphGpuTest, RejectsInvalidMipAndLayerDeclarationsBeforeAllocation) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const ImageInfo valid{.format = Format::R8G8B8A8_UNORM,
            .extent = {4, 4, 1},
            .usage = Flags<ImageUsage>(ImageUsage::CopyDst)};
        struct InvalidImage {
            const char* name;
            uint32_t mip_levels;
            uint32_t array_layers;
            uint32_t depth;
        };
        for(const auto& invalid :
            {InvalidImage{"zero mip levels", 0, 1, 1}, InvalidImage{"zero array layers", 1, 0, 1},
                InvalidImage{"too many mip levels", 4, 1, 1},
                InvalidImage{"depth on a 2D image", 1, 1, 2}}) {
            SCOPED_TRACE(invalid.name);
            auto info = valid;
            info.mip_levels = invalid.mip_levels;
            info.array_layers = invalid.array_layers;
            info.extent.z = invalid.depth;
            const auto created = Image::try_create(device, info, false);
            ASSERT_FALSE(created);
            EXPECT_EQ(created.result(), vk::Result::eErrorInitializationFailed);
        }
        auto multisampled = valid;
        multisampled.mip_levels = 2;
        EXPECT_FALSE(Image::try_create(device, multisampled, false, SampleCount::Count4));
    }

    TEST_F(RenderGraphGpuTest, RejectsBindingsAndOverlappingAliasesBeforeCallingPass) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto buffer =
            Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::CopySrc), 32);
        RenderGraph graph;
        const auto a = graph.import_buffer("a", {{}, 0, 24});
        const auto b = graph.import_buffer("b", {{}, 16, 16});
        graph.add_pass({"writes", {{a, ResourceUsage::TransferDestination, {}},
                                      {b, ResourceUsage::TransferDestination, {}}}});
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        std::vector<RenderGraph::Binding> bindings{buffer, buffer};
        unsigned calls = 0;
        auto record = [&](size_t, const CommandBuffer&) {
            ++calls;
            return Result<void, GraphicsError>::success();
        };
        EXPECT_FALSE(plan.value().record(frames, bindings, record));
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        EXPECT_FALSE(plan.value().record(frames, bindings, record));
        bindings.pop_back();
        EXPECT_FALSE(plan.value().record(frames, bindings, record));
        bindings.push_back(std::shared_ptr<Buffer>{});
        EXPECT_FALSE(plan.value().record(frames, bindings, record));
        EXPECT_EQ(calls, 0u);
        submit(device, frames);
        frames.wait_for_all_slots();
    }

    TEST_F(RenderGraphGpuTest, HandsStateToNextSubmissionWithoutGlobalImageState) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto data = Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::CopySrc), 16);
        auto output =
            std::make_shared<Readback>(device, context.get_context().get_physical_device(), 16);
        ASSERT_TRUE(output->get()) << "Host-coherent readback memory unavailable";
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        RenderGraph producer;
        const auto source = producer.import_buffer("source", {{}, 0, 16});
        producer.add_pass({"fill", {{source, ResourceUsage::TransferDestination, {}}}});
        producer.export_resource({source, ResourceUsage::TransferSource, {}});
        const auto first = producer.compile();
        ASSERT_TRUE(first) << first.error();
        FrameWait wait{device, frames};
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        const std::vector<RenderGraph::Binding> first_bindings{data};
        ASSERT_TRUE(first.value().record(
            frames, first_bindings, [&](size_t, const CommandBuffer& commands) {
                commands.get().fillBuffer(data->get(), 0, 16, 0x10203040);
                return Result<void, GraphicsError>::success();
            }));
        submit(device, frames);
        RenderGraph consumer;
        const auto imported = consumer.import_buffer(
            "source", std::get<RenderGraph::BufferState>(first.value().get_final_states()[0]));
        const auto host = consumer.import_buffer("output", {{}, 0, 16});
        consumer.add_pass({"copy", {{imported, ResourceUsage::TransferSource, {}},
                                       {host, ResourceUsage::TransferDestination, {}}}});
        consumer.export_resource({host, ResourceUsage::HostRead, {}});
        frames.wait_for_current_slot();
        frames.begin_frame(1);
        frames.get_current_command_buffer().begin();
        const std::vector<RenderGraph::Binding> bindings{
            data, std::static_pointer_cast<Buffer>(output)};
        const auto second = consumer.compile();
        ASSERT_TRUE(second) << second.error();
        ASSERT_TRUE(
            second.value().record(frames, bindings, [&](size_t, const CommandBuffer& commands) {
                commands.get().copyBuffer(data->get(), output->get(), vk::BufferCopy(0, 0, 16));
                return Result<void, GraphicsError>::success();
            }));
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
            auto created = Engine::create(config);
            ASSERT_TRUE(created) << created.error().message;
            engine = std::move(created).value();
            auto& renderer = engine->get_renderer();
            ASSERT_TRUE(renderer.enable_offscreen_rendering({32, 32}));
            auto& context = renderer.get_render_context();
            auto& scene_renderer = renderer.get_scene_renderer();
            const auto format = context.get_swapchain().get_images().front()->get_info().format;
            auto presentation = RenderPass::create(context.get_device(),
                {Attachment::get_color_attachment(format, SampleCount::Count1)},
                {{.color_attachments = {SubpassColorAttachment(0)}}}, format);
            ASSERT_TRUE(presentation) << presentation.error().message;
            auto created_target = RenderTarget::create_swapchain_target(
                context.get_device(), *presentation.value(), context.get_swapchain());
            ASSERT_TRUE(created_target) << created_target.error().message;
            auto target = std::move(created_target).value();
            struct Wait {
                RenderContext& context;
                ~Wait() { context.wait_idle(); }
            } wait{context};
            renderer.set_overlay_renderer([&](CommandBuffer& commands) {
                auto view = scene_renderer.get_offscreen_color_view(
                    renderer.get_frame_scheduler().get_current_frame_slot_index());
                // 作为外部消费者声明已导出的 layout，让 validation 核对实际状态。
                vk::ImageMemoryBarrier2 barrier;
                barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
                barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
                barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
                barrier.oldLayout = barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
                    ASSERT_TRUE(renderer.set_render_view({.render_size = {40, 24}}));
                engine->get_window().poll_events();
                LineDrawList lines;
                ASSERT_TRUE(lines.add_line({-0.5f, 0, -2}, {0.5f, 0, -2}, {1, 0, 0, 1}));
                renderer.submit_lines(lines);
                const auto prepared = renderer.prepare_frame();
                ASSERT_TRUE(prepared);
                ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
                ASSERT_TRUE(renderer.render_frame(scene));
            }
            context.wait_idle();
            renderer.set_overlay_renderer({});
        }
    }

    TEST_F(RenderGraphGpuTest, ToneMapsHdrWithoutClippingOrVerticalFlipInSdrAndLinearHdr) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        struct Output {
            Format format;
            float headroom;
        };
        for(const auto [format, headroom] : {Output{Format::R8G8B8A8_SRGB, 1},
                Output{Format::R8G8B8A8_UNORM, 1}, Output{Format::B8G8R8A8_SRGB, 1},
                Output{Format::B8G8R8A8_UNORM, 1}, Output{Format::R16G16B16A16_SFLOAT, 1},
                Output{Format::R16G16B16A16_SFLOAT, 4}, Output{Format::R16G16B16A16_SFLOAT, 16}}) {
            const bool linear_hdr = format == Format::R16G16B16A16_SFLOAT;
            auto color_space = ImageColorSpace::SrgbNonlinearKHR;
            if(linear_hdr)
                color_space = ImageColorSpace::ExtendedSrgbLinearEXT;
            auto output_pass = OutputPass::create(device, format, true, 2, color_space, headroom);
            ASSERT_TRUE(output_pass) << output_pass.error().message;
            auto color = Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT);
            color.description.initial_layout = color.description.final_layout =
                ImageLayout::ColorAttachmentOptimal;
            color.description.store_op = AttachmentStoreOp::Store;
            color.usage |= ImageUsage::Sampled;
            auto source_pass = RenderPass::create(device, {color},
                {{.color_attachments = {SubpassColorAttachment(0)}}}, Format::R16G16B16A16_SFLOAT);
            ASSERT_TRUE(source_pass) << source_pass.error().message;
            auto created_source =
                RenderTarget::try_create_multi_target(device, *source_pass.value(), {4, 4}, 2);
            ASSERT_TRUE(created_source);
            auto source = std::move(created_source).value();
            auto created_output = RenderTarget::try_create_multi_target(
                device, output_pass.value()->get_render_pass(), {4, 4}, 2);
            ASSERT_TRUE(created_output);
            std::shared_ptr<RenderTarget> output = std::move(created_output).value();
            source->set_clear_value(ClearValue(Math::Vec4(4.0f, 0.5f, 0.001f, 1.0f)));
            auto readback = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), linear_hdr ? 128 : 64);
            ASSERT_TRUE(readback->get());
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            EXPECT_FALSE(output_pass.value()->render(frames, output, source->get_color_view(0)));
            EXPECT_FALSE(OutputPass::create(device, format, true, 0));
            RenderGraph graph;
            const auto hdr =
                graph.import_image("HDR", *resolve_image_state(ResourceUsage::Undefined,
                                              {.aspects = Flags<ImageAspect>(ImageAspect::Color)}));
            graph.add_pass({"scene", {{hdr, ResourceUsage::ColorAttachmentWrite, {}}}});
            graph.add_pass(
                {"output_pass", {{hdr, ResourceUsage::SampledRead,
                                    Flags<PipelineStage>(PipelineStage::FragmentShader)}}});
            const auto plan = graph.compile();
            ASSERT_TRUE(plan) << plan.error();
            for(const auto exposure : {1.0f, 0.25f, 0.0f}) {
                frames.wait_for_current_slot();
                frames.begin_frame(0);
                frames.get_current_command_buffer().begin();
                const auto slot = frames.get_current_frame_slot_index();
                for(const float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()})
                    EXPECT_FALSE(output_pass.value()->render(
                        frames, output, source->get_color_view(slot), {.exposure = invalid}));
                EXPECT_FALSE(output_pass.value()->render(frames, {}, source->get_color_view(slot)));
                EXPECT_FALSE(output_pass.value()->render(frames, output, {}));
                const std::vector<RenderGraph::Binding> bindings{
                    source->get_color_view(slot)->get_image()};
                ASSERT_TRUE(
                    plan.value().record(frames, bindings, [&](size_t pass, CommandBuffer& command) {
                        if(pass == 0) {
                            source->begin_render_target(command, slot);
                            const vk::ClearAttachment lower(vk::ImageAspectFlagBits::eColor, 0,
                                vk::ClearColorValue(std::array<float, 4>{0.0f, 2.0f, 0.5f, 1.0f}));
                            command.get().clearAttachments(
                                lower, vk::ClearRect(vk::Rect2D({0, 2}, {4, 2}), 0, 1));
                            command.end_render_pass();
                        } else {
                            return output_pass.value()->render(frames, output,
                                source->get_color_view(slot), {.exposure = exposure});
                        }
                        return Result<void, GraphicsError>::success();
                    }));
                copy_output(frames, output->get_color_view(slot)->get_image(), readback, {4, 4});
                const auto retained = std::weak_ptr(output);
                if(exposure == 0.0f) {
                    output_pass.value().reset();
                    output.reset();
                    EXPECT_FALSE(retained.expired());
                }
                submit(device, frames);
                frames.wait_for_all_slots();
                if(exposure == 0.0f)
                    EXPECT_TRUE(retained.expired());
                const auto bytes = readback->read();
                const bool bgra =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
                for(size_t y = 0; y < 4; ++y) {
                    std::array<float, 3> expected{0.0f, 2.0f, 0.5f};
                    if(y < 2)
                        expected = {4.0f, 0.5f, 0.001f};
                    for(size_t x = 0; x < 4; ++x) {
                        for(size_t channel = 0; channel < 3; ++channel) {
                            if(linear_hdr) {
                                uint16_t half;
                                std::memcpy(
                                    &half, bytes.data() + ((y * 4 + x) * 4 + channel) * 2, 2);
                                const float actual = glm::unpackHalf1x16(half);
                                const float mapped =
                                    headroom
                                    * (1.0f - std::exp(-expected[channel] * exposure / headroom));
                                EXPECT_NEAR(actual, mapped, 0.005f);
                                if(y < 2 && channel == 0 && headroom > 1 && exposure == 1)
                                    EXPECT_GT(actual, 1.0f);
                                continue;
                            }
                            EXPECT_NEAR(
                                std::to_integer<int>(
                                    bytes[(y * 4 + x) * 4 + (bgra ? 2 - channel : channel)]),
                                mapped_byte(expected[channel], exposure), 2)
                                << "format=" << static_cast<int>(format) << " pixel=" << x << ','
                                << y;
                        }
                        if(linear_hdr) {
                            uint16_t alpha;
                            std::memcpy(&alpha, bytes.data() + ((y * 4 + x) * 4 + 3) * 2, 2);
                            EXPECT_EQ(glm::unpackHalf1x16(alpha), 1.0f);
                        } else {
                            EXPECT_EQ(bytes[(y * 4 + x) * 4 + 3], std::byte{255});
                        }
                    }
                }
            }
            EXPECT_FALSE(OutputPass::create(device, Format::R16G16B16A16_SFLOAT, true, 2));
        }
        EXPECT_FALSE(OutputPass::create(
            device, Format::R8G8B8A8_SRGB, true, 2, ImageColorSpace::ExtendedSrgbLinearEXT));
        EXPECT_FALSE(OutputPass::create(
            device, Format::R16G16B16A16_SFLOAT, true, 2, ImageColorSpace::Hdr10St2084EXT));
        for(const auto invalid : {0.0f, 17.0f, std::numeric_limits<float>::quiet_NaN()})
            EXPECT_FALSE(OutputPass::create(device, Format::R16G16B16A16_SFLOAT, true, 2,
                ImageColorSpace::ExtendedSrgbLinearEXT, invalid));
    }

    TEST_F(RenderGraphGpuTest, DirectionalShadowsMoveToggleAndKeepInFlightFramesIndependent) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({33, 33}));
        auto& scene = renderer.get_scene_renderer();
        auto mesh = lit_quad();
        auto material = std::make_shared<Material>("shadow receiver", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 2> outputs;
        enum class Scenario {
            MovingOccluder,
            ShadowsDisabled,
            RebuildAndRemoveOccluder,
            NoLights,
            TiltedLight
        };
        for(const auto& [scenario, name] : {std::pair{Scenario::MovingOccluder, "moving occluder"},
                std::pair{Scenario::ShadowsDisabled, "shadows disabled"},
                std::pair{Scenario::RebuildAndRemoveOccluder, "rebuild target and remove occluder"},
                std::pair{Scenario::NoLights, "no lights"},
                std::pair{Scenario::TiltedLight, "tilted light"}}) {
            SCOPED_TRACE(name);
            for(unsigned index = 0; index < 2; ++index) {
                SCOPED_TRACE(index);
                const float x = index == 0 ? -0.5f : 0.5f;
                const auto occluder = Math::scale(
                    Math::translate(Math::Mat4(1), {x, 0, 0.625f}), {0.25f, 0.25f, 0.25f});
                RenderSubmission submission{
                    .view_project_matrix = ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)},
                    .render_items = {{.mesh = mesh, .material = {AssetHandle(559), material}},
                        {.model_matrix = occluder,
                            .mesh = mesh,
                            .material = {AssetHandle(559), material}}},
                    .lights = {{.entity_id = 1,
                        .intensity = Math::PI,
                        .casts_shadow = scenario != Scenario::ShadowsDisabled}}};
                if(scenario == Scenario::RebuildAndRemoveOccluder && index == 1)
                    submission.render_items.pop_back();
                if(scenario == Scenario::NoLights)
                    submission.lights.clear();
                if(scenario == Scenario::TiltedLight)
                    submission.lights.front().direction = {1, 0, -1};
                const auto lighting = ShadowPass::prepare(submission);
                const bool shadow_enabled =
                    scenario != Scenario::ShadowsDisabled && scenario != Scenario::NoLights;
                EXPECT_EQ(lighting.shadow_light_index, shadow_enabled ? 0 : -1);
                frames.wait_for_current_slot();
                frames.begin_frame(0);
                frames.get_current_command_buffer().begin();
                auto drawn = scene.render(frames, submission);
                ASSERT_TRUE(drawn) << drawn.error();
                outputs[index] = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), 33 * 33 * 4);
                copy_output(frames,
                    scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                        ->get_image(),
                    outputs[index], {33, 33});
                submit(device, frames, drawn.value());
                if(scenario == Scenario::RebuildAndRemoveOccluder && index == 0)
                    ASSERT_TRUE(renderer.enable_offscreen_rendering({33, 33}));
            }
            // 两帧均提交后才等待，读回各自阴影，不能让后帧覆盖前帧的 depth/UBO。
            frames.wait_for_all_slots();
            for(unsigned index = 0; index < 2; ++index) {
                SCOPED_TRACE(index);
                const auto bytes = outputs[index]->read();
                const unsigned shadow_offset = scenario == Scenario::TiltedLight ? 4 : 0;
                const unsigned shadow_x = (index == 0 ? 8 : 24) + shadow_offset;
                const unsigned other_x = index == 0 ? 24 : 8;
                const bool removed_occluder =
                    scenario == Scenario::RebuildAndRemoveOccluder && index == 1;
                const bool shadow = scenario != Scenario::ShadowsDisabled
                                    && scenario != Scenario::NoLights && !removed_occluder;
                glm::dvec3 direction{0, 0, 1};
                if(scenario == Scenario::TiltedLight)
                    direction = {-1, 0, 1};
                double radiance = Math::PI;
                if(scenario == Scenario::NoLights)
                    radiance = 0;
                const auto reference =
                    pbr_reference({0, 0, 1}, {0, 0, 1}, direction, {1, 1, 1}, 0, 1, radiance);
                const auto lit = mapped_byte(static_cast<float>(reference.x));
                for(unsigned channel = 0; channel < 3; ++channel) {
                    const auto at = [&](unsigned x, unsigned y) {
                        return std::to_integer<int>(bytes[(y * 33 + x) * 4 + channel]);
                    };
                    EXPECT_NEAR(at(shadow_x, 16), shadow ? 0 : lit, 3);
                    EXPECT_NEAR(at(other_x, 16), lit, 3);
                    EXPECT_NEAR(at(16, 3), lit, 3);
                }
            }
        }
    }

    TEST_F(RenderGraphGpuTest, ShadowAndMaterialUploadWaitsMergeStagesAndTimelineValues) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        Semaphore upload(device, Semaphore::Type::Timeline);
        Semaphore other(device, Semaphore::Type::Timeline);
        std::vector<QueueSemaphoreSubmit> waits;
        merge_semaphore_wait(waits, {upload, Flags<PipelineStage>(PipelineStage::VertexInput), 7});
        merge_semaphore_wait(
            waits, {upload, Flags<PipelineStage>(PipelineStage::FragmentShader), 3});
        merge_semaphore_wait(waits, {other, Flags<PipelineStage>(PipelineStage::Transfer), 5});
        ASSERT_EQ(waits.size(), 2);
        EXPECT_EQ(waits.front().value, 7);
        EXPECT_EQ(waits.front().stage_mask,
            Flags<PipelineStage>(PipelineStage::VertexInput) | PipelineStage::FragmentShader);
        EXPECT_EQ(waits.back().value, 5);
    }

    TEST_F(RenderGraphGpuTest, ShadowPassRetainsOwnersAfterRendererDestruction) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        auto created = ShadowPass::create(device, 2);
        ASSERT_TRUE(created) << created.error();
        auto shadow = std::move(created).value();
        std::weak_ptr<ImageView> view = shadow->get_depth_view(0);
        auto image = shadow->get_depth_view(0)->get_image();
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)},
            .render_items = {{.mesh = lit_quad()}},
            .lights = {{.casts_shadow = true}}};
        const auto lighting = ShadowPass::prepare(submission);
        ASSERT_EQ(lighting.shadow_light_index, 0);
        RenderGraph graph;
        const auto depth = graph.import_image(
            "shadow depth", *resolve_image_state(ResourceUsage::Undefined,
                                {.aspects = Flags<ImageAspect>(ImageAspect::Depth)}));
        graph.add_pass({"write shadow", {{depth, ResourceUsage::DepthStencilAttachmentWrite, {}}}});
        graph.export_resource({depth, ResourceUsage::SampledRead,
            Flags<PipelineStage>(PipelineStage::FragmentShader)});
        const std::vector<RenderGraph::Binding> bindings{image};
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        ASSERT_TRUE(plan.value().record(frames, bindings, [&](size_t, CommandBuffer&) {
            auto drawn = shadow->render(frames, lighting, submission.render_items);
            if(!drawn)
                return Result<void, GraphicsError>::failure(drawn.error());
            EXPECT_TRUE(drawn.value().empty());
            return Result<void, GraphicsError>::success();
        }));
        shadow.reset();
        EXPECT_FALSE(view.expired());
        submit(device, frames);
        frames.wait_for_all_slots();
        EXPECT_TRUE(view.expired());
    }

    TEST_F(RenderGraphGpuTest, ColorTargetValidationUsesSceneFormatAndSampleRequirements) {
        const auto physical = engine->get_renderer()
                                  .get_render_context()
                                  .get_device()
                                  .get_capability()
                                  .physical_device;
        EXPECT_TRUE(validate_color_target(
            physical, Config::Render::SCENE_COLOR_FORMAT, SampleCount::Count1));
        const auto depth = validate_color_target(physical, Format::D32_SFLOAT, SampleCount::Count1);
        ASSERT_FALSE(depth);
        EXPECT_EQ(depth.error().result, vk::Result::eErrorFormatNotSupported);
        EXPECT_FALSE(validate_color_target(
            physical, Config::Render::SCENE_COLOR_FORMAT, static_cast<SampleCount>(0)));
        EXPECT_FALSE(validate_color_target(
            physical, Config::Render::SCENE_COLOR_FORMAT, static_cast<SampleCount>(3)));
    }

    TEST_F(RenderGraphGpuTest, StartupOutputModesPresentAndKeepTheirFormatOnRebuild) {
        for(const auto mode : {OutputMode::Sdr, OutputMode::Hdr, OutputMode::Auto}) {
            engine.reset();
            Config config;
            config.render.output_mode = mode;
            config.vulkan.enable_validation = true;
            config.window.width = 160;
            config.window.height = 120;
            auto created = Engine::create(config);
            ASSERT_TRUE(created) << created.error().message;
            engine = std::move(created).value();
            auto& renderer = engine->get_renderer();
            auto& swapchain = renderer.get_render_context().get_swapchain();
            const auto selected = swapchain.get_active_generation()->get_config().surface_format;
            if(mode == OutputMode::Sdr)
                EXPECT_EQ(selected.colorSpace, vk::ColorSpaceKHR::eSrgbNonlinear);
            else
                EXPECT_TRUE(selected.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear
                            || (selected.colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT
                                && selected.format == vk::Format::eR16G16B16A16Sfloat));
            for(unsigned frame = 0; frame < 3; ++frame) {
                if(frame == 1)
                    renderer.request_swapchain_recreation();
                const auto ready = renderer.prepare_frame();
                ASSERT_TRUE(ready) << ready.error().message;
                ASSERT_EQ(ready.value(), Renderer::FramePreparation::Ready);
                ASSERT_TRUE(renderer.render_frame({}));
                EXPECT_EQ(swapchain.get_active_generation()->get_config().surface_format, selected);
            }
            renderer.wait_idle();
            ASSERT_TRUE(renderer.enable_offscreen_rendering({4, 4}));
            EXPECT_EQ(renderer.get_scene_renderer()
                          .get_offscreen_color_view(0)
                          ->get_image()
                          ->get_info()
                          .format,
                config.vulkan.surface_format);
        }
    }

    TEST_F(RenderGraphGpuTest, ProductionHdrClearSurvivesMsaaAndTargetGenerationChanges) {
        for(const auto samples : {SampleCount::Count1, SampleCount::Count4}) {
            Config config;
            config.vulkan.msaa_samples = samples;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            config.render.max_frames_in_flight = 2;
            engine.reset();
            auto created = Engine::create(config);
            ASSERT_TRUE(created) << created.error().message;
            engine = std::move(created).value();
            auto& renderer = engine->get_renderer();
            ASSERT_TRUE(renderer.enable_offscreen_rendering({4, 4}));
            auto& scene = renderer.get_scene_renderer();
            auto& context = renderer.get_render_context();
            auto& device = context.get_device();
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            std::weak_ptr<ImageView> old;
            for(unsigned iteration = 0; iteration < 4; ++iteration) {
                const Math::Vec2u size = iteration < 2 ? Math::Vec2u(4, 4) : Math::Vec2u(8, 6);
                ASSERT_TRUE(scene.resize_offscreen_target(size));
                frames.wait_for_current_slot();
                frames.begin_frame(0);
                frames.get_current_command_buffer().begin();
                const auto slot = frames.get_current_frame_slot_index();
                auto readback = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), size.x * size.y * 4);
                auto view = scene.get_offscreen_color_view(slot);
                const auto format = view->get_image()->get_info().format;
                EXPECT_NE(format, Format::R16G16B16A16_SFLOAT);
                auto drawn = scene.render(
                    frames, {.environment = {.background_color = {4.0f, 0.5f, 0.02f}}});
                ASSERT_TRUE(drawn) << drawn.error().message;
                EXPECT_TRUE(drawn.value().empty());
                copy_output(frames, view->get_image(), readback, size);
                if(iteration == 0) {
                    old = view;
                    ASSERT_TRUE(scene.resize_offscreen_target({8, 6}));
                    view.reset();
                    EXPECT_FALSE(old.expired());
                }
                submit(device, frames, drawn.value());
                frames.wait_for_all_slots();
                if(iteration == 0)
                    EXPECT_TRUE(old.expired());
                const bool bgra =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
                const auto bytes = readback->read();
                const std::array<float, 3> hdr{4.0f, 0.5f, 0.02f};
                for(size_t pixel = 0; pixel < size.x * size.y; ++pixel)
                    for(size_t channel = 0; channel < 3; ++channel) {
                        const auto component = bgra ? 2 - channel : channel;
                        EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + component]),
                            mapped_byte(hdr[channel]), 2);
                    }
            }
        }
    }

    TEST_F(RenderGraphGpuTest, RejectsImageBoundsUsageKindAliasesAndForeignQueue) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto image = Image::create(device, {.format = Format::R8G8B8A8_UNORM,
                                               .extent = {4, 4, 1},
                                               .usage = Flags<ImageUsage>(ImageUsage::CopyDst)});
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        unsigned calls = 0;
        auto record = [&](size_t, const CommandBuffer&) {
            ++calls;
            return Result<void, GraphicsError>::success();
        };
        enum class InvalidBinding {
            MipOutOfBounds,
            MissingSampledUsage,
            WrongKind,
            OverlappingAlias,
            ForeignQueue
        };
        for(const auto& [invalid, name] :
            {std::pair{InvalidBinding::MipOutOfBounds, "mip out of bounds"},
                std::pair{InvalidBinding::MissingSampledUsage, "missing sampled usage"},
                std::pair{InvalidBinding::WrongKind, "buffer bound to image"},
                std::pair{InvalidBinding::OverlappingAlias, "overlapping image alias"},
                std::pair{InvalidBinding::ForeignQueue, "foreign queue family"}}) {
            SCOPED_TRACE(name);
            RenderGraph graph;
            auto state = *resolve_image_state(
                ResourceUsage::Undefined, {.aspects = Flags<ImageAspect>(ImageAspect::Color)});
            if(invalid == InvalidBinding::MipOutOfBounds)
                state.subresources.base_mip_level = 1;
            if(invalid == InvalidBinding::ForeignQueue)
                state.resource.queue_family = frames.get_queue_family_index() + 1;
            const auto id = graph.import_image("image", state);
            graph.add_pass({"write", {{id, ResourceUsage::TransferDestination, {}}}});
            std::vector<RenderGraph::Binding> bindings;
            bindings.reserve(2);
            bindings.emplace_back(image);
            if(invalid == InvalidBinding::MissingSampledUsage)
                graph.export_resource({id, ResourceUsage::SampledRead,
                    Flags<PipelineStage>(PipelineStage::FragmentShader)});
            if(invalid == InvalidBinding::WrongKind)
                bindings[0] = std::shared_ptr<Buffer>{};
            if(invalid == InvalidBinding::OverlappingAlias) {
                static_cast<void>(graph.import_image("alias", state));
                bindings.push_back(image);
            }
            const auto plan = graph.compile();
            ASSERT_TRUE(plan) << plan.error();
            EXPECT_FALSE(plan.value().record(frames, bindings, record));
        }
        EXPECT_EQ(calls, 0u);
        submit(device, frames);
        frames.wait_for_all_slots();
    }

    TEST_F(RenderGraphGpuTest, ConfirmsSyncValidationWithUnsubmittedMissingBarrierControl) {
        const auto* enabled = std::getenv("VK_LAYER_ENABLES");
        if(!enabled
            || std::string_view(enabled).find("SYNCHRONIZATION_VALIDATION")
                   == std::string_view::npos)
            GTEST_SKIP() << "Runs under render_graph_sync_validation CTest environment";
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto source =
            Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::CopySrc), 16);
        auto destination =
            Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::CopyDst), 16);
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
