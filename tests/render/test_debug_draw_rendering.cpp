#include "core/engine.h"
#include "diagnostics/logger.h"
#include "render/line_draw_list.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/convert.h"
#include "common/file_io.h"
#include "shader_reload.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <sstream>
#include <tuple>

namespace Comet::Tests {
    class DebugDrawRenderingTest
        : public ::testing::TestWithParam<std::tuple<bool, SampleCount>> {
    protected:
        void SetUp() override {
            log_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(errors);
            log_sink->set_level(spdlog::level::err);
            Logger::add_custom_sink(log_sink);

            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            config.vulkan.msaa_samples = std::get<1>(GetParam());
            config.render.max_frames_in_flight = 2;
            engine = std::make_unique<Engine>(config);
            if(std::get<0>(GetParam())) {
                auto& renderer = engine->get_renderer();
                renderer.enable_offscreen_rendering({160, 120});
                auto& context = renderer.get_render_context();
                auto& swapchain = context.get_swapchain();
                const auto format = swapchain.get_images().front()->get_info().format;
                presentation_pass = std::make_unique<RenderPass>(context.get_device(),
                    std::vector{
                        Attachment::get_color_attachment(format, SampleCount::Count1)},
                    std::vector{RenderSubPass{{}, {SubpassColorAttachment(0)}, {}}},
                    format);
                presentation_target = RenderTarget::create_swapchain_target(
                    context.get_device(), *presentation_pass, swapchain);
                renderer.set_overlay_callbacks({},
                    [this](CommandBuffer& command_buffer) { present(command_buffer); });
            }
            scene.cameras.push_back(RenderCamera{.primary = true});
        }

        void TearDown() override {
            if(engine) {
                engine->get_renderer().get_render_context().wait_idle();
            }
            presentation_target.reset();
            presentation_pass.reset();
            engine.reset();
            if(auto logger = Logger::get_console_logger()) {
                std::erase(logger->sinks(), log_sink);
            }
            EXPECT_TRUE(errors.str().empty()) << errors.str();
            std::error_code error;
            std::filesystem::remove_all(shader_root, error);
        }

        struct Allocations {
            uint64_t count = 0;
            uint64_t bytes = 0;
        };

        void present(CommandBuffer& command_buffer) {
            // 离屏场景仍需最终交换链 pass，对应编辑器的 ImGui 呈现阶段。
            if(presentation_target) {
                presentation_target->begin_render_target(command_buffer);
                presentation_target->end_render_target(command_buffer);
            }
        }

        Allocations allocations() const {
            Allocations result;
            for(const auto& heap : engine->get_renderer()
                    .get_render_context()
                    .get_device()
                    .query_memory_budget()
                    .heaps) {
                result.count += heap.allocation_count;
                result.bytes += heap.allocation_bytes;
            }
            return result;
        }

        LineDrawList lines(const int count) const {
            LineDrawList list;
            for(int index = 0; index < count; ++index) {
                EXPECT_TRUE(list.add_line({-0.5f, 0, -2}, {0.5f, 0, -2}, {1, 0, 0, 1}));
            }
            return list;
        }

        bool draw_frame(const LineDrawList& list = {}) {
            auto& renderer = engine->get_renderer();
            engine->get_window().poll_events();
            renderer.submit_lines(list);
            if(!renderer.prepare_frame()) {
                return false;
            }
            renderer.render_frame(scene);
            return true;
        }

        std::ostringstream errors;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> log_sink;
        std::unique_ptr<Engine> engine;
        std::unique_ptr<RenderPass> presentation_pass;
        std::unique_ptr<RenderTarget> presentation_target;
        RenderScene scene;
        std::filesystem::path shader_root =
            std::filesystem::temp_directory_path()
            / ("comet_debug_reload_" + std::to_string(AssetHandle::generate().value()));
    };

    TEST_P(
        DebugDrawRenderingTest, ReloadsBothShadersPreservesOldFramesAndSurvivesRebuild) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& resources = engine->get_resource_manager();
        auto& shaders = resources.get_shader_manager();
        const auto initial_fragment = shaders.get_shader("debug_line_frag")->get_code();
        const auto samples = std::get<1>(GetParam());
        auto color = Attachment::get_color_attachment(Format::R8G8B8A8_UNORM, samples);
        if(samples == SampleCount::Count1) {
            color.description.store_op = AttachmentStoreOp::Store;
            color.description.final_layout = ImageLayout::TransferSrcOptimal;
            color.usage |= ImageUsage::CopySrc;
        }
        RenderPass pass(device,
            {color, Attachment::get_depth_attachment(Format::D32_SFLOAT, samples)},
            {{{}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)},
                samples, ImageLayout::TransferSrcOptimal,
                Flags<ImageUsage>(ImageUsage::ColorAttachment) | ImageUsage::CopySrc}},
            Format::R8G8B8A8_UNORM);
        auto target = RenderTarget::create_multi_target(device, pass, {32, 32}, 2);
        target->set_clear_value(ClearValue(Math::Vec4(0, 0, 0, 1)));
        PipelineManager pipelines(device, pass);
        DebugRenderer debug(device, pipelines, resources, 2, samples);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        LineDrawList list;
        ASSERT_TRUE(list.add_line({-0.75f, 0, 0.5f}, {0.75f, 0, 0.5f}, {1, 0, 0, 1}));
        vk::UniqueDeviceMemory memory;
        auto readback =
            device.get().createBufferUnique(vk::BufferCreateInfo({}, 2 * 32 * 32 * 4,
                vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive));
        const auto requirements = device.get().getBufferMemoryRequirements(*readback);
        const auto properties =
            context.get_context().get_physical_device().getMemoryProperties();
        std::optional<uint32_t> memory_type;
        const auto required = vk::MemoryPropertyFlagBits::eHostVisible
                              | vk::MemoryPropertyFlagBits::eHostCoherent;
        for(uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
            if((requirements.memoryTypeBits & (1u << index))
                && (properties.memoryTypes[index].propertyFlags & required) == required) {
                memory_type = index;
                break;
            }
        }
        ASSERT_TRUE(memory_type);
        memory = device.get().allocateMemoryUnique({requirements.size, *memory_type});
        device.get().bindBufferMemory(*readback, *memory, 0);
        struct WaitForFrames {
            FrameScheduler& frames;
            ~WaitForFrames() { frames.wait_for_all_slots(); }
        } wait_for_frames{frames};
        ShaderManager::Bytecodes published;
        for(int iteration = 0; iteration < 2; ++iteration) {
            if(iteration == 1) {
                CometEditor::ShaderReload::Requests requests;
                for(const auto& [name, filename, stage] :
                    {std::tuple("debug_line_vert", "debug_line.vert",
                         ShaderCompiler::Stage::Vertex),
                        std::tuple("debug_line_frag", "debug_line.frag",
                            ShaderCompiler::Stage::Fragment)}) {
                    auto source = read_text_file(std::filesystem::path(PROJECT_ROOT_DIR)
                                                 / "engine/shaders/glsl" / filename);
                    if(stage == ShaderCompiler::Stage::Fragment)
                        source.insert(
                            source.rfind('}'), "    o_Color.rgb = o_Color.bgr;\n");
                    write_text_file_atomic(shader_root / filename, source);
                    requests.emplace(
                        name, ShaderCompiler::Request{
                                  .source = shader_root / filename, .stage = stage});
                }
                CometEditor::ShaderReload reload(engine->get_task_scheduler(), requests);
                EXPECT_FALSE(reload.update());
                engine->get_task_scheduler().wait_idle();
                auto candidate = reload.update();
                ASSERT_TRUE(candidate);
                published = std::move(*candidate);
                EXPECT_TRUE(debug.reload_shaders(pipelines, shaders, published, samples));
                EXPECT_FALSE(
                    debug.reload_shaders(pipelines, shaders, published, samples));
                const auto old_vertex = shaders.get_shader("debug_line_vert");
                const auto old_fragment = shaders.get_shader("debug_line_frag");
                auto incomplete = published;
                incomplete.erase("debug_line_vert");
                EXPECT_THROW(
                    debug.reload_shaders(pipelines, shaders, incomplete, samples),
                    std::invalid_argument);
                auto broken = published;
                // Bytecodes 按名称遍历：先创建 fragment 候选，再在 vertex 失败。
                broken.at("debug_line_frag").words = initial_fragment;
                broken.at("debug_line_vert").words.clear();
                EXPECT_THROW(debug.reload_shaders(pipelines, shaders, broken, samples),
                    std::invalid_argument);
                EXPECT_EQ(shaders.get_shader("debug_line_vert"), old_vertex);
                EXPECT_EQ(shaders.get_shader("debug_line_frag"), old_fragment);
                auto source = read_text_file(shader_root / "debug_line.vert");
                const auto position = source.find("uniform DebugDrawConstants");
                ASSERT_NE(position, std::string::npos);
                source.replace(position, std::string("uniform DebugDrawConstants").size(),
                    "layout(row_major) uniform DebugDrawConstants");
                write_text_file_atomic(shader_root / "debug_line.vert", source);
                const auto incompatible =
                    ShaderCompiler::compile(requests.at("debug_line_vert"));
                ASSERT_TRUE(incompatible.succeeded()) << incompatible.diagnostics;
                broken = published;
                broken.at("debug_line_vert").words = incompatible.words;
                EXPECT_THROW(debug.reload_shaders(pipelines, shaders, broken, samples),
                    std::invalid_argument);
                EXPECT_EQ(shaders.get_shader("debug_line_vert"), old_vertex);
                DebugRenderer rebuilt(device, pipelines, resources, 2, samples);
                EXPECT_EQ(shaders.get_shader("debug_line_frag"), old_fragment);
            }
            frames.wait_for_current_slot();
            const auto slot = frames.get_current_frame_slot_index();
            frames.begin_frame(slot);
            auto& command = frames.get_current_command_buffer();
            command.begin();
            target->begin_render_target(command, slot);
            command.set_viewport(Graphics::get_viewport(32, 32));
            command.set_scissor(Graphics::get_scissor(32, 32));
            debug.render(
                frames, {.view = Math::Mat4(1), .projection = Math::Mat4(1)}, list);
            target->end_render_target(command);
            vk::MemoryBarrier barrier(vk::AccessFlagBits::eColorAttachmentWrite,
                vk::AccessFlagBits::eTransferRead);
            command.get().pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eTransfer, {}, barrier, {}, {});
            vk::BufferImageCopy copy;
            copy.bufferOffset = iteration * 32 * 32 * 4;
            copy.imageSubresource =
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            copy.imageExtent = vk::Extent3D(32, 32, 1);
            command.get().copyImageToBuffer(
                target->get_color_view(slot)->get_image()->get(),
                vk::ImageLayout::eTransferSrcOptimal, *readback, copy);
            barrier = vk::MemoryBarrier(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead);
            command.get().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eHost, {}, barrier, {}, {});
            command.end();
            static_cast<void>(
                device.get_graphics_queue().submit2({}, std::span(&command, 1), {},
                    &frames.get_current_frame_slot().in_flight_fence));
            frames.record_submission();
            frames.end_frame();
        }
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 2u);
        frames.wait_for_all_slots();
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 1u);
        const auto* pixels = static_cast<const uint8_t*>(
            device.get().mapMemory(*memory, 0, VK_WHOLE_SIZE));
        std::array<std::array<uint32_t, 3>, 2> channels{};
        for(size_t frame = 0; frame < 2; ++frame)
            for(size_t pixel = 0; pixel < 32 * 32; ++pixel)
                for(size_t channel = 0; channel < 3; ++channel)
                    channels[frame][channel] +=
                        pixels[(frame * 32 * 32 + pixel) * 4 + channel];
        device.get().unmapMemory(*memory);
        EXPECT_GT(channels[0][0], 1000u);
        EXPECT_EQ(channels[0][1], 0u);
        EXPECT_EQ(channels[0][2], 0u);
        EXPECT_EQ(channels[1][0], 0u);
        EXPECT_EQ(channels[1][1], 0u);
        EXPECT_EQ(channels[1][2], channels[0][0]);

        auto& scene_renderer = engine->get_renderer().get_scene_renderer();
        auto& scene_frames = scene_renderer.get_frame_scheduler();
        scene_frames.wait_for_current_slot();
        scene_frames.begin_frame(0);
        EXPECT_THROW(
            scene_renderer.reload_debug_shaders(resources, published), std::logic_error);
        auto& command = scene_frames.get_current_command_buffer();
        command.begin();
        command.end();
        static_cast<void>(device.get_graphics_queue().submit2({}, std::span(&command, 1),
            {}, &scene_frames.get_current_frame_slot().in_flight_fence));
        scene_frames.record_submission();
        scene_frames.end_frame();
        EXPECT_TRUE(scene_renderer.reload_debug_shaders(resources, published));
        scene_frames.wait_for_all_slots();
        scene_renderer.setup_pipeline(resources);
        EXPECT_FALSE(scene_renderer.reload_debug_shaders(resources, published));
    }

    TEST_P(DebugDrawRenderingTest, AppendsProducersConsumesOnceAndReusesSlotBuffers) {
        auto& renderer = engine->get_renderer();
        const auto initial = allocations();
        const auto batch = lines(100);
        renderer.set_overlay_callbacks([&] { renderer.submit_lines(batch); },
            [this](CommandBuffer& command_buffer) { present(command_buffer); });
        ASSERT_TRUE(draw_frame(batch));
        renderer.set_overlay_callbacks(
            {}, [this](CommandBuffer& command_buffer) { present(command_buffer); });
        const auto first_slot = allocations();
        EXPECT_EQ(first_slot.count, initial.count + 1);
        EXPECT_GE(first_slot.bytes - initial.bytes, batch.vertices().size_bytes() * 2);

        // 下帧换到另一 slot，不再提交时不能残留旧线段或创建第二个 buffer。
        ASSERT_TRUE(draw_frame());
        EXPECT_EQ(allocations().count, first_slot.count);
        ASSERT_TRUE(draw_frame(lines(600)));
        const auto grown = allocations();
        EXPECT_EQ(grown.count, first_slot.count);
        EXPECT_GT(grown.bytes, first_slot.bytes);

        ASSERT_TRUE(draw_frame(lines(600)));
        const auto both_slots = allocations();
        EXPECT_EQ(both_slots.count, initial.count + 2);
        for(int frame = 0; frame < 4; ++frame) {
            ASSERT_TRUE(draw_frame(batch));
            EXPECT_EQ(allocations().count, both_slots.count);
            EXPECT_EQ(allocations().bytes, both_slots.bytes);
        }

        if(std::get<0>(GetParam())) {
            renderer.set_render_view({.render_size = {192, 128}});
            ASSERT_TRUE(draw_frame(batch));
            EXPECT_EQ(renderer.get_scene_renderer().get_render_target().get_size(),
                Math::Vec2u(192, 128));
        }
    }

    TEST_P(DebugDrawRenderingTest, MissingCameraAndHiddenViewDiscardRequests) {
        auto& renderer = engine->get_renderer();
        const auto initial = allocations();
        scene.cameras.clear();
        ASSERT_TRUE(draw_frame(lines(100)));
        scene.cameras.push_back(RenderCamera{.primary = true});
        ASSERT_TRUE(draw_frame());
        EXPECT_EQ(allocations().count, initial.count);

        renderer.set_render_view({.visible = false});
        ASSERT_TRUE(draw_frame(lines(100)));
        renderer.set_render_view({});
        ASSERT_TRUE(draw_frame());
        EXPECT_EQ(allocations().count, initial.count);
    }

    INSTANTIATE_TEST_SUITE_P(SwapchainAndOffscreen, DebugDrawRenderingTest,
        ::testing::Combine(::testing::Bool(),
            ::testing::Values(SampleCount::Count1, SampleCount::Count4)));
}
