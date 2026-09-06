#include "render/gpu_test.h"
#include "graphics/pipeline/shader_interface.h"
#include "bloom_frag.h"
#include "tone_map_frag.h"

#include <limits>

namespace Comet::Tests {
    using PostProcessGpuTest = RenderGpuTest;

    TEST(PostProcessTest, ValidatesSettingsAndShaderPushContract) {
        PostProcessRenderer::Settings settings;
        EXPECT_TRUE(settings.is_valid());
        EXPECT_FALSE(settings.uses_bloom());
        for(auto member : {&PostProcessRenderer::Settings::exposure,
                &PostProcessRenderer::Settings::bloom_strength,
                &PostProcessRenderer::Settings::bloom_threshold}) {
            for(float value : {-1.0f, std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()}) {
                settings = {};
                settings.*member = value;
                EXPECT_FALSE(settings.is_valid());
            }
        }
        settings = {100, 10, 65504};
        EXPECT_TRUE(settings.is_valid());
        EXPECT_TRUE(settings.uses_bloom());
        for(const auto code : {std::span<const uint32_t>(BLOOM_FRAG),
                std::span<const uint32_t>(TONE_MAP_FRAG)}) {
            const ShaderInterface shader(code);
            ASSERT_EQ(shader.get_push_constants().size(), 1);
            EXPECT_EQ(shader.get_push_constants()[0].size, 20);
        }
    }

    TEST(PostProcessTest, GraphSkipsDisabledBloomAndTracksPingPongOverwrite) {
        for(const bool enabled : {false, true}) {
            RenderGraph graph;
            const auto hdr = graph.import_image(
                "HDR", *resolve_image_state(ResourceUsage::Undefined,
                           {.aspects = Flags<ImageAspect>(ImageAspect::Color)}));
            graph.add_pass({"scene", {{hdr, ResourceUsage::ColorAttachmentWrite, {}}}});
            PostProcessRenderer::append_passes(graph, hdr, enabled);
            const auto plan = graph.compile();
            const auto passes = plan.get_passes();
            ASSERT_EQ(passes.size(), enabled ? 5u : 2u);
            EXPECT_EQ(passes.back().name, "tone map");
            EXPECT_EQ(plan.get_final_states().size(), enabled ? 3u : 1u);
            if(enabled) {
                EXPECT_EQ(passes[1].name, "bloom extract");
                EXPECT_EQ(passes[2].name, "bloom horizontal");
                EXPECT_EQ(passes[3].name, "bloom vertical");
                EXPECT_EQ(passes[3].barriers.size(), 2);
                ASSERT_EQ(passes[4].barriers.size(), 1);
                EXPECT_EQ(passes[4].barriers[0].resource.index, 1);
                EXPECT_EQ(std::get<ImageState>(passes[4].barriers[0].after).layout,
                    ImageLayout::ShaderReadOnlyOptimal);
            }
        }
    }

    namespace {
        std::vector<Math::Vec3> reference_bloom(const std::vector<Math::Vec3>& source,
            Math::Vec2u size, const PostProcessRenderer::Settings& settings) {
            const Math::Vec2u half{(size.x + 1) / 2, (size.y + 1) / 2};
            std::vector<Math::Vec3> low(half.x * half.y);
            for(uint32_t y = 0; y < half.y; ++y)
                for(uint32_t x = 0; x < half.x; ++x) {
                    unsigned count = 0;
                    for(uint32_t oy = 0; oy < 2; ++oy)
                        for(uint32_t ox = 0; ox < 2; ++ox) {
                            const auto sx = 2 * x + ox;
                            const auto sy = 2 * y + oy;
                            if(sx >= size.x || sy >= size.y)
                                continue;
                            const auto value = source[sy * size.x + sx];
                            const auto bright = std::max({value.x, value.y, value.z});
                            low[y * half.x + x] +=
                                value
                                * (std::max(bright - settings.bloom_threshold, 0.0f)
                                    / std::max(bright, 1e-5f));
                            ++count;
                        }
                    low[y * half.x + x] /= float(count);
                }
            constexpr std::array kernel{1, 8, 28, 56, 70, 56, 28, 8, 1};
            for(unsigned axis = 0; axis < 2; ++axis) {
                auto next = low;
                for(int y = 0; y < int(half.y); ++y)
                    for(int x = 0; x < int(half.x); ++x) {
                        Math::Vec3 value{};
                        for(int offset = -4; offset <= 4; ++offset) {
                            const int sx = std::clamp(
                                x + (axis == 0 ? offset : 0), 0, int(half.x) - 1);
                            const int sy = std::clamp(
                                y + (axis == 1 ? offset : 0), 0, int(half.y) - 1);
                            value +=
                                low[sy * half.x + sx] * (float(kernel[offset + 4]) / 256);
                        }
                        next[y * half.x + x] = value;
                    }
                low = std::move(next);
            }
            auto result = source;
            for(uint32_t y = 0; y < size.y; ++y)
                for(uint32_t x = 0; x < size.x; ++x) {
                    const float px = (float(x) + 0.5f) * half.x / size.x - 0.5f;
                    const float py = (float(y) + 0.5f) * half.y / size.y - 0.5f;
                    const int ix = int(std::floor(px)), iy = int(std::floor(py));
                    Math::Vec3 glow{};
                    for(int oy = 0; oy < 2; ++oy)
                        for(int ox = 0; ox < 2; ++ox) {
                            const auto sx = std::clamp(ix + ox, 0, int(half.x) - 1);
                            const auto sy = std::clamp(iy + oy, 0, int(half.y) - 1);
                            const float fx = px - std::floor(px),
                                        fy = py - std::floor(py);
                            glow += low[sy * half.x + sx] * (ox == 0 ? 1 - fx : fx)
                                    * (oy == 0 ? 1 - fy : fy);
                        }
                    result[y * size.x + x] += glow * settings.bloom_strength;
                }
            return result;
        }
    }

    TEST_F(PostProcessGpuTest, BloomMatchesReferenceAcrossSizesThresholdsAndEncodings) {
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
            for(const Math::Vec2u size :
                {Math::Vec2u{32, 24}, {33, 25}, {1, 1}, {17, 9}}) {
                SCOPED_TRACE(std::to_string(size.x) + "x" + std::to_string(size.y));
                ASSERT_TRUE(post.try_resize_bloom(size));
                auto source =
                    RenderTarget::create_multi_target(device, source_pass, size, 2);
                source->set_clear_value(ClearValue(Math::Vec4(0.25f, 0.1f, 0.05f, 1)));
                std::shared_ptr<RenderTarget> output = RenderTarget::create_multi_target(
                    device, post.get_render_pass(), size, 2);
                FrameScheduler frames(device, 2);
                frames.initialize_swapchain_images(2);
                FrameWait wait{device, frames};
                const vk::Rect2D rectangle({int(size.x / 4), int(size.y / 4)},
                    {std::max(size.x / 4, 1u), std::max(size.y / 4, 1u)});
                std::vector<Math::Vec3> input(size.x * size.y, {0.25f, 0.1f, 0.05f});
                for(uint32_t y = 0; y < rectangle.extent.height; ++y)
                    for(uint32_t x = 0; x < rectangle.extent.width; ++x)
                        input[(rectangle.offset.y + y) * size.x + rectangle.offset.x
                              + x] = {4, 2, 0.5f};
                for(const PostProcessRenderer::Settings settings :
                    {PostProcessRenderer::Settings{1, 0, 1}, {1, 0.5f, 1},
                        {0.5f, 0.25f, 0}, {1, 0.5f, 8}}) {
                    RenderGraph graph;
                    const auto hdr = graph.import_image(
                        "HDR", *resolve_image_state(ResourceUsage::Undefined,
                                   {.aspects = Flags<ImageAspect>(ImageAspect::Color)}));
                    graph.add_pass(
                        {"source", {{hdr, ResourceUsage::ColorAttachmentWrite, {}}}});
                    PostProcessRenderer::append_passes(graph, hdr, settings.uses_bloom());
                    frames.wait_for_current_slot();
                    frames.begin_frame(0);
                    frames.get_current_command_buffer().begin();
                    const auto slot = frames.get_current_frame_slot_index();
                    std::vector<RenderGraph::Binding> bindings{
                        source->get_color_view(slot)->get_image()};
                    post.append_bindings(bindings, slot, settings.uses_bloom());
                    graph.compile().record(
                        frames, bindings, [&](size_t pass, const CommandBuffer& command) {
                            if(pass == 0) {
                                source->begin_render_target(command, slot);
                                command.get().clearAttachments(
                                    vk::ClearAttachment(vk::ImageAspectFlagBits::eColor,
                                        0,
                                        vk::ClearColorValue(
                                            std::array<float, 4>{4, 2, 0.5f, 1})),
                                    vk::ClearRect(rectangle, 0, 1));
                                command.end_render_pass();
                            } else {
                                post.render_pass(pass - 1, frames, output, slot,
                                    source->get_color_view(slot), settings);
                            }
                        });
                    auto readback = std::make_shared<Readback>(device,
                        context.get_context().get_physical_device(), size.x * size.y * 4);
                    copy_output(frames, output->get_color_view(slot)->get_image(),
                        readback, size);
                    submit(device, frames);
                    frames.wait_for_all_slots();
                    const auto expected = reference_bloom(input, size, settings);
                    const auto bytes = readback->read();
                    for(size_t pixel = 0; pixel < expected.size(); ++pixel) {
                        for(unsigned channel = 0; channel < 3; ++channel)
                            ASSERT_NEAR(std::to_integer<int>(bytes[pixel * 4 + channel]),
                                mapped_byte(expected[pixel][channel], settings.exposure),
                                3)
                                << "pixel " << pixel << " channel " << channel;
                        EXPECT_EQ(bytes[pixel * 4 + 3], std::byte{255});
                    }
                }
            }
        }
    }

    TEST_F(
        PostProcessGpuTest, BloomTargetsSurviveOwnerDestructionUntilSubmissionCompletes) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto post = std::make_unique<PostProcessRenderer>(
            device, Format::R8G8B8A8_UNORM, true, 1);
        ASSERT_TRUE(post->try_resize_bloom({8, 8}));
        std::vector<RenderGraph::Binding> before;
        post->append_bindings(before, 0, true);
        ASSERT_EQ(before.size(), 2);
        EXPECT_FALSE(post->try_resize_bloom({0, 8}));
        EXPECT_TRUE(post->try_resize_bloom({8, 8}));
        std::vector<RenderGraph::Binding> after;
        post->append_bindings(after, 0, true);
        EXPECT_EQ(before, after);
        std::weak_ptr<Image> old_ping = std::get<std::shared_ptr<Image>>(before[0]);
        before.clear();
        after.clear();

        auto source = Image::create(
            device, {Format::R16G16B16A16_SFLOAT, {8, 8, 1},
                        Flags<ImageUsage>(ImageUsage::CopyDst) | ImageUsage::Sampled});
        auto source_view =
            ImageView::create(device, source, Flags<ImageAspect>(ImageAspect::Color));
        std::shared_ptr<RenderTarget> output =
            RenderTarget::create_multi_target(device, post->get_render_pass(), {8, 8}, 1);
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RenderGraph graph;
        const auto hdr = graph.import_image(
            "HDR", *resolve_image_state(ResourceUsage::Undefined,
                       {.aspects = Flags<ImageAspect>(ImageAspect::Color)}));
        graph.add_pass({"clear", {{hdr, ResourceUsage::TransferDestination, {}}}});
        PostProcessRenderer::append_passes(graph, hdr, true);
        std::vector<RenderGraph::Binding> bindings{source};
        post->append_bindings(bindings, 0, true);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        graph.compile().record(
            frames, bindings, [&](size_t pass, const CommandBuffer& command) {
                if(pass == 0) {
                    command.get().clearColorImage(source->get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ClearColorValue(std::array<float, 4>{4, 4, 4, 1}),
                        vk::ImageSubresourceRange(
                            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                } else {
                    post->render_pass(
                        pass - 1, frames, output, 0, source_view, {0.1f, 0.5f, 1});
                }
            });
        auto readback = std::make_shared<Readback>(
            device, context.get_context().get_physical_device(), 8 * 8 * 4);
        copy_output(frames, output->get_color_view(0)->get_image(), readback, {8, 8});
        bindings.clear();
        ASSERT_TRUE(post->try_resize_bloom({12, 12}));
        EXPECT_THROW(post->render_pass(0, frames, output, 0, source_view, {1, 0.5f, 1}),
            std::invalid_argument);
        post.reset();
        output.reset();
        EXPECT_FALSE(old_ping.expired());
        submit(device, frames);
        frames.wait_for_all_slots();
        EXPECT_TRUE(old_ping.expired());
        EXPECT_NEAR(
            std::to_integer<int>(readback->read()[0]), mapped_byte(5.5f, 0.1f), 2);
    }

    TEST_F(PostProcessGpuTest, SceneBloomToggleAndResizeKeepInFlightPixelsIndependent) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        Config config;
        config.render.clear_color = {4, 4, 4, 1};
        config.vulkan.msaa_samples = SampleCount::Count4;
        SceneRenderer scene(context, config.vulkan, config.render);
        scene.setup_offscreen_render_pass({16, 16});
        auto& frames = scene.get_frame_scheduler();
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::vector<std::shared_ptr<Readback>> readbacks;
        std::vector<int> expected;
        for(unsigned index = 0; index < 4; ++index) {
            const Math::Vec2u size = index < 2 ? Math::Vec2u(16, 16) : Math::Vec2u(7, 3);
            if(index == 2)
                scene.resize_offscreen_target(size);
            const float strength = index % 2 == 0 ? 0.5f : 0;
            scene.set_post_process_settings({0.1f, strength, 1});
            EXPECT_THROW(
                scene.set_post_process_settings({1, -1, 1}), std::invalid_argument);
            EXPECT_FLOAT_EQ(scene.get_post_process_settings().bloom_strength, strength);
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            EXPECT_THROW(scene.set_post_process_settings({}), std::logic_error);
            EXPECT_TRUE(scene.render_scene_pass({}).empty());
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), size.x * size.y * 4);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                    ->get_image(),
                output, size);
            submit(device, frames);
            readbacks.push_back(output);
            expected.push_back(mapped_byte(4 + 3 * strength, 0.1f));
        }
        frames.wait_for_all_slots();
        for(size_t index = 0; index < readbacks.size(); ++index) {
            const auto bytes = readbacks[index]->read();
            for(size_t offset = 0; offset < bytes.size(); offset += 4)
                for(size_t channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(bytes[offset + channel]),
                        expected[index], 2);
        }
    }
}
