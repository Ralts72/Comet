#include "support/render_gpu_test.h"
#include "render/passes/bloom_pass.h"
#include "render/passes/output_pass.h"
#include "render/resource/render_resources.h"
#include "render/resource/texture.h"
#include "render/render_target.h"
#include "render/scene/scene_renderer.h"
#include "scene/scene.h"
#include "core/window.h"
#include "graphics/resource/image_view.h"
#include "graphics/pipeline/shader_interface.h"
#include "bloom_frag.h"
#include "display_frag.h"

#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>

namespace Comet::Tests {
    TEST(BloomPassTest, GraphTracksPingPongReuseAndShaderAbi) {
        RenderGraph graph;
        const auto hdr =
            graph.import_image("hdr", *resolve_image_state(ResourceUsage::SampledRead,
                                          {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
                                          Flags<PipelineStage>(PipelineStage::FragmentShader)));
        const auto passes = BloomPass::append_passes(graph, hdr);
        graph.add_pass({"display", {{passes.output, ResourceUsage::SampledRead,
                                       Flags<PipelineStage>(PipelineStage::FragmentShader)}}});
        const auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        ASSERT_EQ(plan.value().get_passes().size(), 4u);
        const auto& vertical = plan.value().get_passes()[passes.ids[2]];
        const auto rewrite =
            std::ranges::find(vertical.barriers, passes.output, &RenderGraph::Barrier::resource);
        ASSERT_NE(rewrite, vertical.barriers.end());
        EXPECT_EQ(std::get<ImageState>(rewrite->before).layout, ImageLayout::ShaderReadOnlyOptimal);
        EXPECT_EQ(std::get<ImageState>(rewrite->after).layout, ImageLayout::ColorAttachmentOptimal);
        const auto bloom = ShaderInterface::reflect(BLOOM_FRAG);
        const auto display = ShaderInterface::reflect(DISPLAY_FRAG);
        ASSERT_TRUE(bloom);
        ASSERT_TRUE(display);
        ASSERT_EQ(bloom.value().get_push_constants().size(), 1u);
        ASSERT_EQ(display.value().get_push_constants().size(), 1u);
        EXPECT_EQ(bloom.value().get_push_constants()[0].size, 8u);
        EXPECT_EQ(display.value().get_push_constants()[0].size, 16u);
        EXPECT_EQ(bloom.value().get_bindings().size(), 1u);
        EXPECT_EQ(display.value().get_bindings().size(), 2u);
    }

    class BloomGpuTest: public RenderGpuTest {
    protected:
        using Pixels = std::vector<glm::dvec3>;
        static double half(double value) {
            return glm::unpackHalf1x16(glm::packHalf1x16(static_cast<float>(value)));
        }
        static glm::dvec3 half(glm::dvec3 value) {
            return {half(value.x), half(value.y), half(value.z)};
        }
        static Pixels reference(const Pixels& source, Math::Vec2u size, float threshold) {
            const int width = (size.x + 1) / 2, height = (size.y + 1) / 2;
            Pixels reduced(width * height);
            for(int y = 0; y < height; ++y)
                for(int x = 0; x < width; ++x) {
                    int count = 0;
                    glm::dvec3 energy{};
                    for(uint32_t dy = 0; dy < 2; ++dy)
                        for(uint32_t dx = 0; dx < 2; ++dx) {
                            const uint32_t sx = 2 * x + dx, sy = 2 * y + dy;
                            if(sx >= size.x || sy >= size.y)
                                continue;
                            const auto value = source[sy * size.x + sx];
                            const auto brightness = std::max({value.x, value.y, value.z});
                            energy += value
                                      * (std::max(brightness - threshold, 0.0)
                                          / std::max(brightness, 1e-5));
                            ++count;
                        }
                    reduced[y * width + x] = half(energy / double(count));
                }
            // 独立计算 CPU 卷积参考值，每轮按 RGBA16F 精度舍入。
            constexpr std::array weights{1, 8, 28, 56, 70, 56, 28, 8, 1};
            for(int axis = 0; axis < 2; ++axis) {
                Pixels blurred(reduced.size());
                for(int y = 0; y < height; ++y)
                    for(int x = 0; x < width; ++x) {
                        glm::dvec3 energy{};
                        for(int tap = -4; tap <= 4; ++tap) {
                            int sx = x, sy = y;
                            if(axis == 0)
                                sx = std::clamp(x + tap, 0, width - 1);
                            else
                                sy = std::clamp(y + tap, 0, height - 1);
                            energy += reduced[sy * width + sx] * (weights[tap + 4] / 256.0);
                        }
                        blurred[y * width + x] = half(energy);
                    }
                reduced = std::move(blurred);
            }
            Pixels upsampled(source.size());
            for(uint32_t y = 0; y < size.y; ++y)
                for(uint32_t x = 0; x < size.x; ++x) {
                    const double px = (x + 0.5) * width / size.x - 0.5;
                    const double py = (y + 0.5) * height / size.y - 0.5;
                    const int left = static_cast<int>(std::floor(px)),
                              top = static_cast<int>(std::floor(py));
                    glm::dvec3 energy{};
                    for(int dy = 0; dy < 2; ++dy)
                        for(int dx = 0; dx < 2; ++dx) {
                            const double wx = dx == 0 ? 1 - (px - left) : px - left;
                            const double wy = dy == 0 ? 1 - (py - top) : py - top;
                            energy += reduced[std::clamp(top + dy, 0, height - 1) * width
                                              + std::clamp(left + dx, 0, width - 1)]
                                      * wx * wy;
                        }
                    upsampled[y * size.x + x] = energy;
                }
            return upsampled;
        }
        static double encode(double linear) {
            if(linear <= 0.0031308)
                return 12.92 * linear;
            return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
        }
    };

    TEST_F(BloomGpuTest, MatchesCpuPixelsForOddTinySdrHdrDisabledAndExtremeSettings) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        auto bloom = BloomPass::create(device, 2);
        ASSERT_TRUE(bloom) << bloom.error().message;
        for(const auto format :
            {Format::R8G8B8A8_SRGB, Format::R8G8B8A8_UNORM, Format::R16G16B16A16_SFLOAT}) {
            const bool hdr_output = format == Format::R16G16B16A16_SFLOAT;
            auto color_space = ImageColorSpace::SrgbNonlinearKHR;
            float headroom = 1;
            if(hdr_output) {
                color_space = ImageColorSpace::ExtendedSrgbLinearEXT;
                headroom = 4;
            }
            auto display = OutputPass::create(device, format, true, 2, color_space, headroom);
            ASSERT_TRUE(display) << display.error().message;
            for(const auto size :
                {Math::Vec2u(32, 24), Math::Vec2u(33, 25), Math::Vec2u(1, 1), Math::Vec2u(17, 9)}) {
                SCOPED_TRACE(std::to_string(size.x) + "x" + std::to_string(size.y));
                Pixels source(size.x * size.y);
                TextureData data{.width = int(size.x),
                    .height = int(size.y),
                    .format = Format::R16G16B16A16_SFLOAT};
                data.pixels.resize(source.size() * 8);
                for(uint32_t y = 0; y < size.y; ++y)
                    for(uint32_t x = 0; x < size.x; ++x) {
                        glm::dvec3 value(0.03, 0.06, 0.1);
                        if((x == size.x / 2 && y == size.y / 2) || (x == 0 && y == 0))
                            value = {8, 2, 0.5};
                        if(x == size.x - 1 && y == size.y - 1)
                            value = {0.25, 4, 1};
                        if(size == Math::Vec2u(1, 1))
                            value = {65504, 128, 0.25};
                        source[y * size.x + x] = half(value);
                        for(size_t channel = 0; channel < 4; ++channel) {
                            float v = 1;
                            if(channel < 3)
                                v = static_cast<float>(value[channel]);
                            const auto packed = glm::packHalf1x16(v);
                            std::memcpy(data.pixels.data() + ((y * size.x + x) * 4 + channel) * 2,
                                &packed, 2);
                        }
                    }
                auto texture = engine->get_render_resources().try_create_texture(data);
                ASSERT_TRUE(texture);
                texture.value()->get_ready_completion().wait();
                ASSERT_TRUE(bloom.value()->resize(size));
                auto target = RenderTarget::try_create_multi_target(
                    device, display.value()->get_render_pass(), size, 2);
                ASSERT_TRUE(target);
                std::shared_ptr<RenderTarget> output = std::move(target).value();
                for(const auto settings :
                    {PostProcessSettings{}, PostProcessSettings{1, true, 0.5f, 1},
                        PostProcessSettings{0.25f, true, 1, 0},
                        PostProcessSettings{1, true, 1, 65504}, PostProcessSettings{0, true, 1, 1},
                        PostProcessSettings{100, true, 10, 0}, PostProcessSettings{1, true, 0, 0},
                        PostProcessSettings{1, false, 10, 0}}) {
                    SCOPED_TRACE(std::to_string(settings.exposure) + "/"
                                 + std::to_string(settings.bloom_strength) + "/"
                                 + std::to_string(settings.bloom_threshold));
                    RenderGraph graph;
                    const auto input = graph.import_image(
                        "hdr", *resolve_image_state(ResourceUsage::SampledRead,
                                   {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
                                   Flags<PipelineStage>(PipelineStage::FragmentShader)));
                    std::optional<BloomPass::Passes> passes;
                    RenderGraph::Pass output_pass{
                        "display", {{input, ResourceUsage::SampledRead,
                                       Flags<PipelineStage>(PipelineStage::FragmentShader)}}};
                    if(settings.uses_bloom()) {
                        passes = BloomPass::append_passes(graph, input);
                        output_pass.uses.push_back({passes->output, ResourceUsage::SampledRead,
                            Flags<PipelineStage>(PipelineStage::FragmentShader)});
                    }
                    const auto display_id = graph.add_pass(std::move(output_pass));
                    const auto plan = graph.compile();
                    ASSERT_TRUE(plan) << plan.error();
                    EXPECT_EQ(plan.value().get_passes().size(), settings.uses_bloom() ? 4u : 1u);
                    frames.wait_for_current_slot();
                    frames.begin_frame(0);
                    frames.get_current_command_buffer().begin();
                    const auto slot = frames.get_current_frame_slot_index();
                    const auto source_view = texture.value()->get_image_view();
                    std::vector<RenderGraph::Binding> bindings{source_view->get_image()};
                    std::shared_ptr<ImageView> glow;
                    if(passes) {
                        bloom.value()->append_bindings(bindings, slot);
                        glow = bloom.value()->get_output(slot);
                    }
                    const auto recorded =
                        plan.value().record(frames, bindings, [&](auto pass, CommandBuffer&) {
                            if(pass == display_id)
                                return display.value()->render(
                                    frames, output, source_view, settings, glow);
                            return bloom.value()->render(
                                frames, pass, *passes, source_view, settings.bloom_threshold);
                        });
                    ASSERT_TRUE(recorded) << recorded.error().message;
                    const size_t stride = hdr_output ? 8 : 4;
                    auto readback = std::make_shared<Readback>(device,
                        context.get_context().get_physical_device(), source.size() * stride);
                    ASSERT_TRUE(readback->get());
                    copy_output(frames, output->get_color_view(slot)->get_image(), readback, size);
                    submit(device, frames);
                    frames.wait_for_all_slots();
                    const auto bytes = readback->read();
                    const auto blurred = reference(source, size, settings.bloom_threshold);
                    const double strength = settings.uses_bloom() ? settings.bloom_strength : 0;
                    for(size_t pixel = 0; pixel < source.size(); ++pixel) {
                        for(size_t channel = 0; channel < 3; ++channel) {
                            const double value = std::clamp(
                                source[pixel][channel] + blurred[pixel][channel] * strength, 0.0,
                                65504.0);
                            const double mapped =
                                headroom * (1 - std::exp(-value * settings.exposure / headroom));
                            if(hdr_output) {
                                uint16_t packed;
                                std::memcpy(
                                    &packed, bytes.data() + pixel * stride + channel * 2, 2);
                                EXPECT_NEAR(glm::unpackHalf1x16(packed), mapped, 0.008);
                            } else {
                                EXPECT_NEAR(std::to_integer<int>(bytes[pixel * stride + channel]),
                                    std::lround(encode(mapped) * 255), 2);
                            }
                        }
                        if(hdr_output) {
                            uint16_t alpha;
                            std::memcpy(&alpha, bytes.data() + pixel * stride + 6, 2);
                            EXPECT_EQ(glm::unpackHalf1x16(alpha), 1.0f);
                        } else {
                            EXPECT_EQ(bytes[pixel * stride + 3], std::byte{255});
                        }
                    }
                }
            }
        }
    }

    TEST_F(BloomGpuTest, TargetsResizeAtomicallyAndBindingsOutliveThePass) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        EXPECT_FALSE(BloomPass::create(device, 0));
        auto bloom = BloomPass::create(device, 2);
        ASSERT_TRUE(bloom);
        EXPECT_FALSE(bloom.value()->get_output(0));
        ASSERT_TRUE(bloom.value()->resize({17, 9}));
        auto first = bloom.value()->get_output(0);
        EXPECT_EQ(first->get_image()->get_info().extent, Math::Vec3u(9, 5, 1));
        EXPECT_FALSE(bloom.value()->get_output(2));
        EXPECT_FALSE(bloom.value()->resize({0, 9}));
        EXPECT_EQ(bloom.value()->get_output(0), first);
        ASSERT_TRUE(bloom.value()->resize({17, 9}));
        EXPECT_EQ(bloom.value()->get_output(0), first);

        TextureData data{.width = 17, .height = 9, .format = Format::R16G16B16A16_SFLOAT};
        data.pixels.resize(17 * 9 * 8);
        auto source = engine->get_render_resources().try_create_texture(data);
        ASSERT_TRUE(source);
        source.value()->get_ready_completion().wait();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        RenderGraph graph;
        const auto hdr =
            graph.import_image("hdr", *resolve_image_state(ResourceUsage::SampledRead,
                                          {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
                                          Flags<PipelineStage>(PipelineStage::FragmentShader)));
        const auto passes = BloomPass::append_passes(graph, hdr);
        const auto plan = graph.compile();
        ASSERT_TRUE(plan);
        EXPECT_FALSE(bloom.value()->render(
            frames, passes.ids[0], passes, source.value()->get_image_view(), 1));
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        EXPECT_FALSE(
            bloom.value()->render(frames, 9999, passes, source.value()->get_image_view(), 1));
        EXPECT_FALSE(bloom.value()->render(frames, passes.ids[0], passes, {}, 1));
        for(const auto invalid : {-1.0f, std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()})
            EXPECT_FALSE(bloom.value()->render(
                frames, passes.ids[0], passes, source.value()->get_image_view(), invalid));
        std::vector<RenderGraph::Binding> bindings{source.value()->get_image_view()->get_image()};
        bloom.value()->append_bindings(bindings, 0);
        ASSERT_TRUE(plan.value().record(frames, bindings, [&](auto pass, CommandBuffer&) {
            return bloom.value()->render(frames, pass, passes, source.value()->get_image_view(), 1);
        }));
        const std::weak_ptr<ImageView> retained = first;
        first.reset();
        bindings.clear();
        ASSERT_TRUE(bloom.value()->resize({33, 25}));
        EXPECT_FALSE(retained.expired());
        bloom.value().reset();
        EXPECT_FALSE(retained.expired());
        submit(device, frames);
        frames.wait_for_all_slots();
        EXPECT_TRUE(retained.expired());
    }

    TEST_F(BloomGpuTest, ProductionSettingsToggleResizeAndKeepInFlightFramesIndependent) {
        engine.reset();
        Config config;
        config.window.width = 160;
        config.window.height = 120;
        config.vulkan.enable_validation = true;
        config.vulkan.msaa_samples = SampleCount::Count4;
        auto created = Engine::create(config);
        ASSERT_TRUE(created) << created.error().message;
        engine = std::move(created).value();
        auto& renderer = engine->get_renderer();
        const auto prepared = renderer.prepare_frame();
        ASSERT_TRUE(prepared);
        ASSERT_TRUE(prepared.value());
        RenderScene initial_scene;
        initial_scene.environment.background_color = {4, 2, 0.5f};
        initial_scene.post_process = {.bloom_enabled = true, .bloom_strength = 0.5f};
        initial_scene.cameras.push_back(RenderCamera{.primary = true});
        ASSERT_TRUE(renderer.render_frame(initial_scene));
        EXPECT_EQ(
            renderer.get_scene_renderer().get_post_process_settings(), initial_scene.post_process);
        renderer.wait_idle();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({17, 9}));
        auto& scene = renderer.get_scene_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::vector<std::shared_ptr<Readback>> outputs;
        const std::array settings{PostProcessSettings{1, true, 0.5f, 1},
            PostProcessSettings{1, false, 0.5f, 1}, PostProcessSettings{0.25f, true, 1, 0},
            PostProcessSettings{1, true, 1, 65504}};
        const std::array sizes{
            Math::Vec2u(17, 9), Math::Vec2u(33, 25), Math::Vec2u(33, 25), Math::Vec2u(1, 1)};
        const std::array colors{Math::Vec3(4, 2, 0.5f), Math::Vec3(0.1f, 0.2f, 0.3f),
            Math::Vec3(8, 2, 1), Math::Vec3(0)};
        for(size_t i = 0; i < settings.size(); ++i) {
            ASSERT_TRUE(scene.resize_offscreen_target(sizes[i]));
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            const auto before = scene.get_post_process_settings();
            EXPECT_FALSE(scene.render(frames, {.post_process = {.bloom_strength = -1}}));
            EXPECT_EQ(scene.get_post_process_settings(), before);
            auto rendered = scene.render(frames,
                {.environment = {.background_color = colors[i]}, .post_process = settings[i]});
            ASSERT_TRUE(rendered) << rendered.error().message;
            EXPECT_EQ(scene.get_post_process_settings(), settings[i]);
            auto readback = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), sizes[i].x * sizes[i].y * 4);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())->get_image(),
                readback, sizes[i]);
            outputs.push_back(readback);
            submit(device, frames, rendered.value());
        }
        frames.wait_for_all_slots();
        const auto format = scene.get_offscreen_color_view(0)->get_image()->get_info().format;
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(size_t i = 0; i < outputs.size(); ++i) {
            const auto bytes = outputs[i]->read();
            for(size_t channel = 0; channel < 3; ++channel) {
                const double value = colors[i][channel];
                const double peak = std::max({colors[i].x, colors[i].y, colors[i].z});
                double glow = 0;
                if(peak > 0)
                    glow = value * std::max(peak - settings[i].bloom_threshold, 0.0) / peak;
                const double strength = settings[i].uses_bloom() ? settings[i].bloom_strength : 0;
                const double mapped =
                    1 - std::exp(-(value + glow * strength) * settings[i].exposure);
                EXPECT_NEAR(std::to_integer<int>(bytes[bgra ? 2 - channel : channel]),
                    std::lround(encode(mapped) * 255), 2);
            }
        }
        renderer.wait_idle();
    }

    TEST_F(BloomGpuTest, EngineConsumesUiEditsAndSceneReplacementWithoutGlobalSettings) {
        auto& renderer = engine->get_renderer();
        engine->set_scene(std::make_unique<Scene>());
        const PostProcessSettings enabled{.exposure = 0.75f, .bloom_enabled = true};
        unsigned rendered = 0;
        unsigned updates = 0;
        const auto result = engine->run(
            [&](UpdateContext) {
                if(rendered == 1)
                    EXPECT_EQ(renderer.get_scene_renderer().get_post_process_settings(), enabled);
                if(rendered >= 2) {
                    EXPECT_EQ(renderer.get_scene_renderer().get_post_process_settings(),
                        PostProcessSettings{});
                    engine->get_window().request_close();
                }
                if(++updates > 30)
                    engine->get_window().request_close();
                return Result<void, Error>::success();
            },
            [&] {
                if(rendered == 0) {
                    if(!engine->get_scene()->set_post_process(enabled))
                        return Result<void, Error>::failure({"Cannot edit scene post processing"});
                } else {
                    engine->set_scene(std::make_unique<Scene>());
                }
                ++rendered;
                return Result<void, Error>::success();
            });
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(rendered, 2u);
    }
}
