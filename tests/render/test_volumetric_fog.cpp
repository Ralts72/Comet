#include "support/render_graph_gpu_fixture.h"
#include "render/material/material_programs.h"

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, VolumetricFogIntegratesSurfacesTransparencyParticlesAndBackground) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const auto mesh = lit_quad();
        auto white = std::make_shared<Material>("white", "unlit_color");
        auto black = std::make_shared<Material>("black", "unlit_color");
        auto glass = std::make_shared<Material>("glass", "unlit_color");
        ASSERT_TRUE(white->set_vector_property("color", {1, 1, 1, 1}));
        ASSERT_TRUE(black->set_vector_property("color", {0, 0, 0, 1}));
        ASSERT_TRUE(glass->set_vector_property("color", {1, 1, 1, .5f}));
        ASSERT_TRUE(glass->set_surface({.alpha_mode = AlphaMode::Blend}));
        const auto view = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
        const Math::Vec3 tint{.1f, .3f, .8f};
        for(const auto& [samples, temporal] : {std::pair{SampleCount::Count1, false},
                std::pair{SampleCount::Count4, false}, std::pair{SampleCount::Count1, true}}) {
            SCOPED_TRACE(static_cast<unsigned>(samples));
            MaterialPrograms programs;
            Math::Vec2u size{65, 65};
            auto scene = create_scene(programs, size, samples, temporal);
            ASSERT_TRUE(scene);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            for(unsigned frame = 0; frame < 10; ++frame) {
                SCOPED_TRACE(frame);
                if(frame == 7) {
                    size = {41, 29};
                    ASSERT_TRUE(scene.value()->resize_offscreen_target(size));
                }
                const bool enabled = frame != 0 && frame != 6;
                const auto camera = ViewProjectMatrix{
                    view, frame == 7 ? Math::perspective(60, float(size.x) / size.y, .1f, 10)
                                     : Math::ortho(-1, 1, -1, 1, .1f, 10)};
                RenderWorld world;
                world.environment.background_color = {1, 1, 1};
                world.environment.volumetric_fog = {.enabled = enabled,
                    .color = tint,
                    .density = .3f,
                    .height_falloff = 0,
                    .anisotropy = 0,
                    .max_distance = 8,
                    .ambient = 1};
                if(frame != 8)
                    world.render_items.push_back({.entity_id = 1,
                        .mesh = mesh,
                        .material = {
                            .resource = frame < 2 || frame == 6 || frame == 7 ? white : black}});
                if(frame == 3)
                    world.render_items.push_back({.entity_id = 2,
                        .model_matrix = Math::translate(Math::Mat4(1), {0, 0, 1}),
                        .mesh = mesh,
                        .material = {.resource = glass}});
                if(frame == 4 || frame == 5) {
                    auto particles = std::make_shared<ParticleSnapshot>();
                    particles->instances.push_back(
                        {.position_size = {0, 0, 1.5f, .8f}, .color = {1, 1, 1, 1}});
                    particles->batches.push_back(
                        {{}, 0, 1, frame == 4 ? ParticleBlend::Alpha : ParticleBlend::Additive});
                    world.particles = particles;
                    world.particle_textures.resize(1);
                }
                ASSERT_TRUE(scene.value()->prepare_material_programs(world));
                RenderGeometry geometry;
                geometry.prepare(world);
                begin_frame(frames);
                auto drawn = scene.value()->render(frames, {world, camera}, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                const auto output =
                    scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
                auto pixels = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), size.x * size.y * 4);
                copy_output(frames, output->get_image(), pixels, size);
                submit(device, frames, drawn.value());
                frames.wait_for_all_slots();
                const auto bytes = pixels->read();
                const auto format = output->get_image()->get_info().format;
                const bool bgra =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
                const float t = std::exp(-.3f * 2.5f), front = std::exp(-.3f * 1.5f);
                const auto scatter = tint * (1 - t);
                const auto foreground = tint * (1 - front) + Math::Vec3(front);
                Math::Vec3 expected(1);
                if(enabled) {
                    expected = scatter;
                    if(frame == 1 || frame == 7)
                        expected += Math::Vec3(t);
                    if(frame == 3)
                        expected = (scatter + foreground) * .5f;
                    if(frame == 4)
                        expected = foreground;
                    if(frame == 5)
                        expected = scatter + Math::Vec3(front);
                    if(frame == 8)
                        expected = tint * (1 - std::exp(-2.4f)) + Math::Vec3(std::exp(-2.4f));
                }
                for(unsigned channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(bytes[((size.y / 2) * size.x + size.x / 2) * 4
                                                           + (bgra ? 2 - channel : channel)]),
                        mapped_byte(expected[channel]), 3)
                        << channel;
            }
        }
    }
    TEST_F(RenderGraphGpuTest, VolumetricLightScatteringRespondsToOccluderShadows) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        const Math::Vec2u size{65, 65};
        auto scene = create_scene(programs, size);
        ASSERT_TRUE(scene);
        const auto mesh = lit_quad();
        auto black = std::make_shared<Material>("fog receiver", "unlit_color");
        ASSERT_TRUE(black->set_vector_property("color", {0, 0, 0, 1}));
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<int, 3> brightness{};
        for(unsigned frame = 0; frame < 3; ++frame) {
            RenderWorld world;
            world.environment.volumetric_fog = {.enabled = true,
                .color = {1, 1, 1},
                .density = .3f,
                .height_falloff = 0,
                .anisotropy = 0,
                .max_distance = 8,
                .ambient = 0};
            world.render_items.push_back(
                {.entity_id = 1, .mesh = mesh, .material = {.resource = black}});
            if(frame != 2)
                world.render_items.push_back({.entity_id = 2,
                    .model_matrix = Math::translate(Math::Mat4(1), {-1, 0, 2})
                                    * Math::scale(Math::Mat4(1), {.5f, .8f, 1}),
                    .mesh = mesh,
                    .material = {.resource = black}});
            world.lights.push_back({.direction = {1, 0, -1},
                .intensity = 8,
                .casts_shadow = frame != 0,
                .shadow_distance = 10});
            ASSERT_TRUE(scene.value()->prepare_material_programs(world));
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            auto drawn = scene.value()->render(frames, {world, camera}, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            auto pixels = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), size.x * size.y * 4);
            finish_readback(*scene.value(), frames, pixels, size, drawn.value());
            brightness[frame] = std::to_integer<int>(pixels->read()[(32 * 65 + 32) * 4]);
        }
        EXPECT_GT(brightness[0], brightness[1] + 5);
        EXPECT_NEAR(brightness[0], brightness[2], 3);
        const float scattering = (1 - std::exp(-.3f * 2.5f)) * 8 / (4 * Math::PI);
        EXPECT_NEAR(brightness[0], mapped_byte(scattering), 3);
    }

}
