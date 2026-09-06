#include "render/gpu_test.h"
#include "render/render_graph.h"
#include "core/engine.h"
#include "graphics/resource/image.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "diagnostics/logger.h"
#include "render/resource/mesh.h"
#include "render/material.h"
#include "common/file_io.h"
#include "shader/compiler.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cmath>

namespace Comet::Tests {
    using RenderGraphGpuTest = RenderGpuTest;

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

    TEST_F(
        RenderGraphGpuTest, ToneMapsHdrWithoutClippingOrVerticalFlipInBothSdrEncodings) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        for(const auto format : {Format::R8G8B8A8_SRGB, Format::R8G8B8A8_UNORM}) {
            PostProcessRenderer post(device, format, true, 2);
            auto color = Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT);
            color.description.initial_layout = color.description.final_layout =
                ImageLayout::ColorAttachmentOptimal;
            color.description.store_op = AttachmentStoreOp::Store;
            color.usage |= ImageUsage::Sampled;
            RenderPass source_pass(device, {color},
                {{{}, {SubpassColorAttachment(0)}, {}}}, Format::R16G16B16A16_SFLOAT);
            auto source =
                RenderTarget::create_multi_target(device, source_pass, {4, 4}, 2);
            std::shared_ptr<RenderTarget> output = RenderTarget::create_multi_target(
                device, post.get_render_pass(), {4, 4}, 2);
            source->set_clear_value(ClearValue(Math::Vec4(4.0f, 0.5f, 0.001f, 1.0f)));
            auto readback = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 64);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            RenderGraph graph;
            const auto hdr = graph.import_image(
                "HDR", *resolve_image_state(ResourceUsage::Undefined,
                           {.aspects = Flags<ImageAspect>(ImageAspect::Color)}));
            graph.add_pass({"scene", {{hdr, ResourceUsage::ColorAttachmentWrite, {}}}});
            graph.add_pass(
                {"post", {{hdr, ResourceUsage::SampledRead,
                             Flags<PipelineStage>(PipelineStage::FragmentShader)}}});
            const auto plan = graph.compile();
            for(const auto exposure : {1.0f, 0.25f, 0.0f}) {
                frames.wait_for_current_slot();
                frames.begin_frame(0);
                frames.get_current_command_buffer().begin();
                const auto slot = frames.get_current_frame_slot_index();
                const std::vector<RenderGraph::Binding> bindings{
                    source->get_color_view(slot)->get_image()};
                plan.record(
                    frames, bindings, [&](size_t pass, const CommandBuffer& command) {
                        if(pass == 0) {
                            source->begin_render_target(command, slot);
                            const vk::ClearAttachment lower(
                                vk::ImageAspectFlagBits::eColor, 0,
                                vk::ClearColorValue(
                                    std::array<float, 4>{0.0f, 2.0f, 0.5f, 1.0f}));
                            command.get().clearAttachments(
                                lower, vk::ClearRect(vk::Rect2D({0, 2}, {4, 2}), 0, 1));
                            command.end_render_pass();
                        } else {
                            post.render(frames, output, slot,
                                source->get_color_view(slot), exposure);
                        }
                    });
                copy_output(
                    frames, output->get_color_view(slot)->get_image(), readback, {4, 4});
                submit(device, frames);
                frames.wait_for_all_slots();
                const auto bytes = readback->read();
                for(size_t y = 0; y < 4; ++y) {
                    const std::array<float, 3> expected =
                        y < 2 ? std::array<float, 3>{4.0f, 0.5f, 0.001f}
                              : std::array<float, 3>{0.0f, 2.0f, 0.5f};
                    for(size_t x = 0; x < 4; ++x) {
                        for(size_t channel = 0; channel < 3; ++channel)
                            EXPECT_NEAR(
                                std::to_integer<int>(bytes[(y * 4 + x) * 4 + channel]),
                                mapped_byte(expected[channel], exposure), 2)
                                << "format=" << static_cast<int>(format) << " pixel=" << x
                                << ',' << y;
                        EXPECT_EQ(bytes[(y * 4 + x) * 4 + 3], std::byte{255});
                    }
                }
            }
            EXPECT_THROW(
                PostProcessRenderer(device, Format::R16G16B16A16_SFLOAT, true, 2),
                std::invalid_argument);
        }
    }

    TEST_F(RenderGraphGpuTest, ProductionHdrClearSurvivesMsaaAndTargetGenerationChanges) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        for(const auto samples : {SampleCount::Count1, SampleCount::Count4}) {
            Config config;
            config.vulkan.msaa_samples = samples;
            config.render.clear_color = {4.0f, 0.5f, 0.02f, 1.0f};
            SceneRenderer scene(context, config.vulkan, config.render);
            scene.setup_offscreen_render_pass({4, 4});
            auto& frames = scene.get_frame_scheduler();
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            std::weak_ptr<ImageView> old;
            for(unsigned iteration = 0; iteration < 4; ++iteration) {
                const Math::Vec2u size =
                    iteration < 2 ? Math::Vec2u(4, 4) : Math::Vec2u(8, 6);
                scene.resize_offscreen_target(size);
                frames.wait_for_current_slot();
                frames.begin_frame(0);
                frames.get_current_command_buffer().begin();
                const auto slot = frames.get_current_frame_slot_index();
                auto readback = std::make_shared<Readback>(device,
                    context.get_context().get_physical_device(), size.x * size.y * 4);
                auto view = scene.get_offscreen_color_view(slot);
                const auto format = view->get_image()->get_info().format;
                EXPECT_NE(format, Format::R16G16B16A16_SFLOAT);
                EXPECT_TRUE(scene.render_scene_pass({}).empty());
                copy_output(frames, view->get_image(), readback, size);
                if(iteration == 0) {
                    old = view;
                    scene.resize_offscreen_target({8, 6});
                    view.reset();
                    EXPECT_FALSE(old.expired());
                }
                submit(device, frames);
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

    TEST_F(
        RenderGraphGpuTest, ForwardLightsProduceExpectedPixelsAndReuseMaterialBindings) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        Config config;
        config.vulkan.msaa_samples = SampleCount::Count1;
        config.render.clear_color = {0, 0, 0, 1};
        SceneRenderer scene(context, config.vulkan, config.render);
        scene.setup_offscreen_render_pass({17, 17});
        scene.setup_pipeline(engine->get_resource_manager());
        auto mesh = lit_quad();
        auto tilted = lit_quad({1, 0, 1});
        auto material = std::make_shared<Material>("lit", "lit_color");
        material->set_vector_property("albedo", {0.5f, 0.25f, 0.125f, 1});
        auto& frames = scene.get_frame_scheduler();
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        auto readback = std::make_shared<Readback>(
            device, context.get_context().get_physical_device(), 17 * 17 * 4);
        for(unsigned mode = 0; mode < 7; ++mode) {
            RenderLight light{.entity_id = 1,
                .position = {0, 0, 2.5f},
                .intensity = Math::PI,
                .range = 10,
                .inner_angle = 5,
                .outer_angle = 20};
            if(mode == 1 || mode == 2 || mode == 6) {
                light.type = mode == 1 ? LightType::Point : LightType::Spot;
                light.intensity = 4 * Math::PI;
            }
            if(mode == 6) {
                light.inner_angle = 0;
                light.outer_angle = 0.0001f;
            }
            if(mode == 3)
                light.direction = {0, 0, 1};
            auto model = Math::Mat4(1);
            if(mode == 4)
                model = Math::scale(model, {2, 1, 1});
            if(mode == 5)
                model = Math::scale(model, {1, 1, 0});
            RenderSubmission submission{
                .view_project_matrix = ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)},
                .render_items = {{.model_matrix = model,
                    .mesh = mode == 4 ? tilted : mesh,
                    .material = {AssetHandle(555), material}}},
                .lights = {light}};
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            EXPECT_TRUE(scene.render_scene_pass(submission).empty());
            const auto& statistics = scene.get_material_statistics();
            EXPECT_EQ(statistics.draw_calls, 1);
            EXPECT_EQ(statistics.light_count, 1);
            if(mode > 0)
                EXPECT_EQ(statistics.material_bindings_created, 0);
            auto view =
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            copy_output(frames, view->get_image(), readback, {17, 17});
            submit(device, frames);
            frames.wait_for_all_slots();
            const auto bytes = readback->read();
            const auto format = view->get_image()->get_info().format;
            const bool bgra =
                format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
            for(unsigned y = 0; y < 17; ++y) {
                for(unsigned x = 0; x < 17; ++x) {
                    float irradiance = 1;
                    if(mode == 1 || mode == 2 || mode == 6) {
                        const Math::Vec3 position{
                            (x + 0.5f) / 8.5f - 1, 1 - (y + 0.5f) / 8.5f, 0.5f};
                        const auto delta = light.position - position;
                        const float distance = Math::length(delta);
                        const float falloff =
                            std::max(1.0f - std::pow(distance / light.range, 4.0f), 0.0f);
                        irradiance = 4 * falloff * falloff / (distance * distance)
                                     * delta.z / distance;
                        if(mode == 2 || mode == 6) {
                            const float inner =
                                std::cos(Math::radians(light.inner_angle));
                            const float outer =
                                std::cos(Math::radians(light.outer_angle));
                            if(inner - outer > 1e-6f) {
                                const float t = std::clamp(
                                    (delta.z / distance - outer) / (inner - outer), 0.0f,
                                    1.0f);
                                irradiance *= t * t * (3 - 2 * t);
                            } else if(delta.z / distance < outer) {
                                irradiance = 0;
                            }
                        }
                    }
                    if(mode == 3 || mode == 5)
                        irradiance = 0;
                    if(mode == 4)
                        irradiance = 1 / std::sqrt(1.25f);
                    const std::array<float, 3> albedo{0.5f, 0.25f, 0.125f};
                    for(size_t channel = 0; channel < 3; ++channel) {
                        const auto component = bgra ? 2 - channel : channel;
                        EXPECT_NEAR(
                            std::to_integer<int>(bytes[(y * 17 + x) * 4 + component]),
                            mapped_byte(albedo[channel] * irradiance), 3)
                            << "mode " << mode << " at " << x << ',' << y;
                    }
                }
            }
        }
    }

    TEST_F(RenderGraphGpuTest,
        DirectionalShadowsMoveToggleAndKeepInFlightFramesIndependent) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        Config config;
        config.vulkan.msaa_samples = SampleCount::Count1;
        config.render.clear_color = {0, 0, 0, 1};
        SceneRenderer scene(context, config.vulkan, config.render);
        scene.setup_offscreen_render_pass({33, 33});
        scene.setup_pipeline(engine->get_resource_manager());
        auto mesh = lit_quad();
        auto material = std::make_shared<Material>("shadow receiver", "lit_color");
        material->set_vector_property("albedo", {1, 1, 1, 1});
        auto& frames = scene.get_frame_scheduler();
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 2> outputs;
        for(unsigned batch = 0; batch < 3; ++batch) {
            for(unsigned index = 0; index < 2; ++index) {
                const float x = index == 0 ? -0.5f : 0.5f;
                const auto occluder =
                    Math::scale(Math::translate(Math::Mat4(1), {x, 0, 0.625f}),
                        {0.25f, 0.25f, 0.25f});
                RenderSubmission submission{
                    .view_project_matrix =
                        ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)},
                    .render_items = {{.mesh = mesh,
                                         .material = {AssetHandle(559), material}},
                        {.model_matrix = occluder,
                            .mesh = mesh,
                            .material = {AssetHandle(559), material}}},
                    .lights = {{.entity_id = 1,
                        .intensity = Math::PI,
                        .casts_shadow = batch != 1}}};
                if(batch == 2 && index == 1)
                    submission.render_items.pop_back();
                const auto lighting = ShadowRenderer::prepare(submission);
                EXPECT_EQ(lighting.shadow_parameters.x, batch == 1 ? -1 : 0);
                frames.wait_for_current_slot();
                frames.begin_frame(0);
                frames.get_current_command_buffer().begin();
                EXPECT_TRUE(scene.render_scene_pass(submission).empty());
                outputs[index] = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), 33 * 33 * 4);
                copy_output(frames,
                    scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                        ->get_image(),
                    outputs[index], {33, 33});
                submit(device, frames);
            }
            // 两帧均提交后才等待，读回各自阴影，不能让后帧覆盖前帧的 depth/UBO。
            frames.wait_for_all_slots();
            for(unsigned index = 0; index < 2; ++index) {
                const auto bytes = outputs[index]->read();
                const unsigned shadow_x = index == 0 ? 8 : 24;
                const unsigned other_x = index == 0 ? 24 : 8;
                const bool shadow = batch != 1 && !(batch == 2 && index == 1);
                for(unsigned channel = 0; channel < 3; ++channel) {
                    const auto at = [&](unsigned x, unsigned y) {
                        return std::to_integer<int>(bytes[(y * 33 + x) * 4 + channel]);
                    };
                    EXPECT_NEAR(at(shadow_x, 16), shadow ? 0 : mapped_byte(1), 3)
                        << "batch " << batch << " frame " << index;
                    EXPECT_NEAR(at(other_x, 16), mapped_byte(1), 3);
                    EXPECT_NEAR(at(16, 3), mapped_byte(1), 3);
                }
            }
        }
    }

    TEST_F(RenderGraphGpuTest, ShadowAndMaterialUploadWaitsMergeStagesAndTimelineValues) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        Semaphore upload(device, Semaphore::Type::Timeline);
        Semaphore other(device, Semaphore::Type::Timeline);
        std::vector<QueueSemaphoreSubmit> waits;
        merge_semaphore_wait(
            waits, {upload, Flags<PipelineStage>(PipelineStage::VertexInput), 7});
        merge_semaphore_wait(
            waits, {upload, Flags<PipelineStage>(PipelineStage::FragmentShader), 3});
        merge_semaphore_wait(
            waits, {other, Flags<PipelineStage>(PipelineStage::Transfer), 5});
        ASSERT_EQ(waits.size(), 2);
        EXPECT_EQ(waits.front().value, 7);
        EXPECT_EQ(
            waits.front().stage_mask, Flags<PipelineStage>(PipelineStage::VertexInput)
                                          | PipelineStage::FragmentShader);
        EXPECT_EQ(waits.back().value, 5);
    }

    TEST_F(RenderGraphGpuTest, ShadowPassRetainsOwnersAfterRendererDestruction) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        auto shadow = std::make_unique<ShadowRenderer>(device, 2);
        std::weak_ptr<ImageView> view = shadow->get_depth_view(0);
        auto image = shadow->get_depth_view(0)->get_image();
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)},
            .render_items = {{.mesh = lit_quad()}},
            .lights = {{.casts_shadow = true}}};
        const auto lighting = ShadowRenderer::prepare(submission);
        ASSERT_EQ(lighting.shadow_parameters.x, 0);
        RenderGraph graph;
        const auto depth = graph.import_image(
            "shadow depth", *resolve_image_state(ResourceUsage::Undefined,
                                {.aspects = Flags<ImageAspect>(ImageAspect::Depth)}));
        graph.add_pass(
            {"write shadow", {{depth, ResourceUsage::DepthStencilAttachmentWrite, {}}}});
        graph.export_resource({depth, ResourceUsage::SampledRead,
            Flags<PipelineStage>(PipelineStage::FragmentShader)});
        const std::vector<RenderGraph::Binding> bindings{image};
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        graph.compile().record(frames, bindings, [&](size_t, const CommandBuffer&) {
            EXPECT_TRUE(
                shadow->render(frames, lighting, submission.render_items).empty());
        });
        shadow.reset();
        EXPECT_FALSE(view.expired());
        submit(device, frames);
        frames.wait_for_all_slots();
        EXPECT_TRUE(view.expired());
    }

    TEST_F(RenderGraphGpuTest, PbrParametersAndCameraProjectionMatchReferencePixels) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        Config config;
        config.vulkan.msaa_samples = SampleCount::Count1;
        config.render.clear_color = {0, 0, 0, 1};
        SceneRenderer scene(context, config.vulkan, config.render);
        scene.setup_offscreen_render_pass({33, 33});
        scene.setup_pipeline(engine->get_resource_manager());
        auto mesh = lit_quad();
        auto zero_normal = lit_quad({0, 0, 0});
        auto back_normal = lit_quad({0, 0, -1});
        auto material = std::make_shared<Material>("pbr", "pbr_color");
        material->set_vector_property("base_color", {0.8f, 0.2f, 0.1f, 1});
        auto& frames = scene.get_frame_scheduler();
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        const auto reference = [](glm::dvec3 n, glm::dvec3 v, glm::dvec3 l,
                                   double metallic, double roughness, double radiance) {
            n = glm::normalize(n);
            v = glm::normalize(v);
            l = glm::normalize(l);
            const double nl = std::clamp(glm::dot(n, l), 0.0, 1.0);
            const double nv = std::clamp(glm::dot(n, v), 0.0, 1.0);
            if(nl <= 0 || nv <= 0)
                return glm::dvec3(0);
            const auto h = glm::normalize(l + v);
            const double nh = std::clamp(glm::dot(n, h), 0.0, 1.0);
            const double vh = std::clamp(glm::dot(v, h), 0.0, 1.0);
            const double a2 = std::pow(std::clamp(roughness, 0.045, 1.0), 4);
            const double d =
                a2 / (glm::pi<double>() * std::pow(nh * nh * (a2 - 1) + 1, 2));
            const double g = 2 * nl * nv
                             / (nv * std::sqrt(a2 + (1 - a2) * nl * nl)
                                 + nl * std::sqrt(a2 + (1 - a2) * nv * nv));
            const double t = std::pow(1 - vh, 5);
            const double fd = 0.04 + 0.96 * t;
            const glm::dvec3 base(0.8, 0.2, 0.1);
            const auto dielectric = base * (1 - fd) / glm::pi<double>()
                                    + glm::dvec3(fd * d * g / (4 * nl * nv));
            const auto metal = (base + (1.0 - base) * t) * d * g / (4 * nl * nv);
            return glm::mix(dielectric, metal, std::clamp(metallic, 0.0, 1.0)) * radiance
                   * nl;
        };
        for(unsigned mode = 0; mode < 12; ++mode) {
            float metallic = 0;
            float roughness = 0.6f;
            Math::Vec3 camera{0, 0, 3};
            RenderLight light{.direction = {0.5f, 0, -1}, .intensity = 4};
            auto rendered_mesh = mesh;
            if(mode == 1)
                metallic = 1;
            if(mode == 2) {
                metallic = 0.5f;
                roughness = 0.2f;
            }
            if(mode == 3) {
                metallic = -1;
                roughness = -1;
            }
            if(mode == 4) {
                metallic = 2;
                roughness = 2;
            }
            if(mode == 5)
                camera = {1, 0, 3};
            if(mode == 7 || mode == 8) {
                light.type = mode == 7 ? LightType::Point : LightType::Spot;
                light.position = {0, 0, 2.5f};
                light.direction = {0, 0, -1};
                light.intensity = 4 * Math::PI;
            }
            if(mode == 9)
                rendered_mesh = zero_normal;
            if(mode == 10)
                rendered_mesh = back_normal;
            if(mode == 11) {
                metallic = 1;
                roughness = 0.045f;
                light.direction = {0, 0, -1};
                light.intensity = 10000;
            }
            material->set_scalar_property("metallic", metallic);
            material->set_scalar_property("roughness", roughness);
            const bool orthographic = mode == 6;
            const auto projection = orthographic ? Math::ortho(-1, 1, -1, 1, 0.1f, 10)
                                                 : Math::perspective(60, 1, 0.1f, 10);
            RenderSubmission submission{
                .view_project_matrix =
                    ViewProjectMatrix{
                        Math::look_at(camera, {0, 0, 0.5f}, {0, 1, 0}), projection},
                .render_items = {{.mesh = rendered_mesh,
                    .material = {AssetHandle(781), material}}},
                .lights = {light}};
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            EXPECT_TRUE(scene.render_scene_pass(submission).empty());
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            auto view =
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            copy_output(frames, view->get_image(), output, {33, 33});
            submit(device, frames);
            frames.wait_for_all_slots();
            auto direction = -glm::dvec3(light.direction);
            double radiance = light.intensity;
            if(mode == 7 || mode == 8) {
                direction = {0, 0, 1};
                radiance *= std::pow(1 - std::pow(2.0 / light.range, 4), 2) / 4;
            }
            auto expected =
                reference({0, 0, 1}, glm::dvec3(camera) - glm::dvec3(0, 0, 0.5),
                    direction, metallic, roughness, radiance);
            if(mode == 9 || mode == 10)
                expected = glm::dvec3(0);
            const auto bytes = output->read();
            const bool bgra =
                view->get_image()->get_info().format == Format::B8G8R8A8_SRGB
                || view->get_image()->get_info().format == Format::B8G8R8A8_UNORM;
            for(unsigned x : {4u, 16u, 28u}) {
                if(!orthographic && x != 16)
                    continue;
                for(unsigned channel = 0; channel < 3; ++channel) {
                    const auto component = bgra ? 2 - channel : channel;
                    EXPECT_NEAR(
                        std::to_integer<int>(bytes[(16 * 33 + x) * 4 + component]),
                        mapped_byte(static_cast<float>(expected[channel])), 3)
                        << "mode " << mode;
                }
            }
        }
    }

    TEST_F(RenderGraphGpuTest, PbrReceivesDirectionalShadowWithoutMaterialRebinding) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        Config config;
        config.vulkan.msaa_samples = SampleCount::Count1;
        SceneRenderer scene(context, config.vulkan, config.render);
        scene.setup_offscreen_render_pass({33, 33});
        scene.setup_pipeline(engine->get_resource_manager());
        const auto mesh = lit_quad();
        auto material = std::make_shared<Material>("pbr", "pbr_color");
        const auto occluder = Math::scale(
            Math::translate(Math::Mat4(1), {-0.5f, 0, 0.875f}), {0.25f, 0.25f, 0.25f});
        RenderSubmission submission{
            .view_project_matrix =
                ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                    Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = mesh, .material = {AssetHandle(780), material}},
                {.model_matrix = occluder,
                    .mesh = mesh,
                    .material = {AssetHandle(780), material}}},
            .lights = {{.direction = {1, 0, -1}, .intensity = 4}}};
        auto& frames = scene.get_frame_scheduler();
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::vector<std::byte>, 2> pixels;
        for(unsigned index = 0; index < 2; ++index) {
            submission.lights[0].casts_shadow = index == 1;
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            EXPECT_TRUE(scene.render_scene_pass(submission).empty());
            EXPECT_EQ(scene.get_material_statistics().material_bindings_created,
                index == 0 ? 1 : 0);
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                    ->get_image(),
                output, {33, 33});
            submit(device, frames);
            frames.wait_for_all_slots();
            pixels[index] = output->read();
        }
        for(unsigned channel = 0; channel < 3; ++channel) {
            const unsigned center = (16 * 33 + 16) * 4 + channel;
            const unsigned lit = (16 * 33 + 28) * 4 + channel;
            EXPECT_GT(std::to_integer<int>(pixels[0][center]), 50);
            EXPECT_LE(std::to_integer<int>(pixels[1][center]), 3);
            EXPECT_NEAR(std::to_integer<int>(pixels[0][lit]),
                std::to_integer<int>(pixels[1][lit]), 2);
        }
    }

    TEST_F(RenderGraphGpuTest,
        SharedLitVertexReloadUpdatesBothConsumersAndRetainsOldFrames) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& resources = engine->get_resource_manager();
        auto& shaders = resources.get_shader_manager();
        const auto directory =
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/glsl";
        const auto temporary =
            std::filesystem::temp_directory_path()
            / ("comet_pbr_reload_" + std::to_string(AssetHandle::generate().value()));
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{temporary};
        auto source = read_text_file(directory / "material_lit.vert");
        source.insert(source.rfind('}'), "    world_normal = -world_normal;\n");
        write_text_file_atomic(temporary / "lit.vert", source);
        const auto compiled = ShaderCompiler::compile({.source = temporary / "lit.vert",
            .stage = ShaderCompiler::Stage::Vertex,
            .include_directories = {directory}});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        ShaderManager::Bytecodes candidate{{"material_lit_vert", {compiled.words}},
            {"material_lit_frag", {shaders.get_shader("material_lit_frag")->get_code()}},
            {"material_pbr_frag", {shaders.get_shader("material_pbr_frag")->get_code()}}};
        Config config;
        config.vulkan.msaa_samples = SampleCount::Count1;
        SceneRenderer scene(context, config.vulkan, config.render);
        scene.setup_offscreen_render_pass({32, 16});
        scene.setup_pipeline(resources);
        const auto mesh = lit_quad();
        auto lambert = std::make_shared<Material>("lambert", "lit_color");
        lambert->set_vector_property("albedo", {0.5f, 0.5f, 0.5f, 1});
        auto pbr = std::make_shared<Material>("pbr", "pbr_color");
        pbr->set_vector_property("base_color", {0.5f, 0.5f, 0.5f, 1});
        pbr->set_scalar_property("roughness", 0.8f);
        RenderSubmission submission{
            .view_project_matrix =
                ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                    Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.model_matrix = Math::scale(
                                  Math::translate(Math::Mat4(1), {-0.5f, 0, 0}),
                                  {0.5f, 1, 1}),
                                 .mesh = mesh,
                                 .material = {AssetHandle(783), lambert}},
                {.model_matrix = Math::scale(
                     Math::translate(Math::Mat4(1), {0.5f, 0, 0}), {0.5f, 1, 1}),
                    .mesh = mesh,
                    .material = {AssetHandle(784), pbr}}},
            .lights = {{.intensity = Math::PI}}};
        const auto original = shaders.get_shader("material_lit_vert");
        auto incomplete = candidate;
        incomplete.erase("material_pbr_frag");
        EXPECT_THROW(
            scene.reload_material_shaders(resources, incomplete), std::invalid_argument);
        EXPECT_EQ(shaders.get_shader("material_lit_vert"), original);
        auto& frames = scene.get_frame_scheduler();
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 2> outputs;
        for(unsigned index = 0; index < 2; ++index) {
            if(index == 1) {
                const auto report = scene.reload_material_shaders(resources, candidate);
                EXPECT_EQ(report.pipelines, 2);
                EXPECT_EQ(report.material_versions, 2);
                EXPECT_EQ(report.material_bindings, 0);
            }
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            EXPECT_TRUE(scene.render_scene_pass(submission).empty());
            outputs[index] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 32 * 16 * 4);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                    ->get_image(),
                outputs[index], {32, 16});
            submit(device, frames);
        }
        frames.wait_for_all_slots();
        const auto before = outputs[0]->read();
        const auto after = outputs[1]->read();
        for(unsigned channel = 0; channel < 3; ++channel) {
            EXPECT_NEAR(std::to_integer<int>(before[(8 * 32 + 8) * 4 + channel]),
                mapped_byte(0.5f), 2);
            EXPECT_NEAR(std::to_integer<int>(before[(8 * 32 + 24) * 4 + channel]),
                mapped_byte(0.48f + 0.04f / (4 * std::pow(0.8f, 4))), 2);
            EXPECT_LE(std::to_integer<int>(after[(8 * 32 + 8) * 4 + channel]), 2);
            EXPECT_LE(std::to_integer<int>(after[(8 * 32 + 24) * 4 + channel]), 2);
        }
    }

    TEST_F(RenderGraphGpuTest,
        LitShaderPublishesAtFrameBoundaryWithoutReplacingOldFramePixels) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& resources = engine->get_resource_manager();
        const auto shader_directory =
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/glsl";
        const auto temporary =
            std::filesystem::temp_directory_path()
            / ("comet_lit_reload_" + std::to_string(AssetHandle::generate().value()));
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{temporary};
        auto source = read_text_file(shader_directory / "material_lit.frag");
        source.insert(source.rfind('}'), "    color.rgb *= 0.5;\n");
        write_text_file_atomic(temporary / "lit.frag", source);
        const auto compiled = ShaderCompiler::compile({.source = temporary / "lit.frag",
            .stage = ShaderCompiler::Stage::Fragment,
            .include_directories = {shader_directory}});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        auto& shaders = resources.get_shader_manager();
        ShaderManager::Bytecodes candidate{
            {"material_lit_vert", {shaders.get_shader("material_lit_vert")->get_code()}},
            {"material_pbr_frag", {shaders.get_shader("material_pbr_frag")->get_code()}},
            {"material_lit_frag", {compiled.words}}};
        Config config;
        config.vulkan.msaa_samples = SampleCount::Count1;
        SceneRenderer scene(context, config.vulkan, config.render);
        scene.setup_offscreen_render_pass({4, 4});
        scene.setup_pipeline(resources);
        auto mesh = lit_quad();
        auto material = std::make_shared<Material>("lit", "lit_color");
        material->set_vector_property("albedo", {0.5f, 0.5f, 0.5f, 1});
        auto& frames = scene.get_frame_scheduler();
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 2> outputs;
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)},
            .render_items = {{.mesh = mesh, .material = {AssetHandle(556), material}}},
            .lights = {{.intensity = Math::PI}}};
        const auto legacy = shaders.get_shader("material_mesh");
        for(unsigned index = 0; index < 2; ++index) {
            if(index == 1) {
                const auto report = scene.reload_material_shaders(resources, candidate);
                EXPECT_EQ(report.pipelines, 1);
                EXPECT_EQ(report.material_versions, 1);
                EXPECT_EQ(report.material_bindings, 0);
                EXPECT_EQ(shaders.get_shader("material_mesh"), legacy);
                auto broken = candidate;
                broken.at("material_lit_frag").words.clear();
                const auto current = shaders.get_shader("material_lit_frag");
                EXPECT_THROW(scene.reload_material_shaders(resources, broken),
                    std::invalid_argument);
                EXPECT_EQ(shaders.get_shader("material_lit_frag"), current);
                auto header = read_text_file(shader_directory / "lighting.glsl");
                const auto binding = header.find("binding = 1");
                ASSERT_NE(binding, std::string::npos);
                header.replace(binding, std::string("binding = 1").size(), "binding = 7");
                write_text_file_atomic(temporary / "lighting.glsl", header);
                const auto incompatible =
                    ShaderCompiler::compile({.source = temporary / "lit.frag",
                        .stage = ShaderCompiler::Stage::Fragment});
                ASSERT_TRUE(incompatible.succeeded()) << incompatible.diagnostics;
                broken.at("material_lit_frag").words = incompatible.words;
                EXPECT_THROW(scene.reload_material_shaders(resources, broken),
                    std::invalid_argument);
                EXPECT_EQ(shaders.get_shader("material_lit_frag"), current);
            }
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            EXPECT_TRUE(scene.render_scene_pass(submission).empty());
            outputs[index] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 64);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                    ->get_image(),
                outputs[index], {4, 4});
            submit(device, frames);
        }
        frames.wait_for_all_slots();
        for(unsigned index = 0; index < 2; ++index) {
            const auto bytes = outputs[index]->read();
            for(unsigned pixel = 0; pixel < 16; ++pixel)
                for(unsigned channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + channel]),
                        mapped_byte(index == 0 ? 0.5f : 0.25f), 2);
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
