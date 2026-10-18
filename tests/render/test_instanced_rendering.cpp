#include "support/render_graph_gpu_fixture.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/registry.h"
#include "render/material/material_layout.h"
#include "render/material/material_programs.h"
#include "unlit_color_vert.h"
#include "pbr_frag.h"
#include "scene/material_parameters.h"

#include <algorithm>

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, TemporalResolveRejectsMovedSurfacesAndSurvivesSceneCutsAndResize) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene = create_scene(programs, {65, 65}, SampleCount::Count1, true);
        ASSERT_TRUE(scene) << scene.error();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        const auto mesh = lit_quad();
        auto red = std::make_shared<Material>("temporal red", "unlit_color");
        ASSERT_TRUE(red->set_vector_property("color", {1, 0, 0, 1}));
        for(unsigned frame = 0; frame < 14; ++frame) {
            SCOPED_TRACE(frame);
            const auto size = frame >= 11 ? Math::Vec2u(79, 61) : Math::Vec2u(65);
            if(frame == 11)
                ASSERT_TRUE(scene.value()->resize_offscreen_target(size));
            RenderWorld world;
            world.scene_lifetime = frame >= 10 ? 2 : 1;
            world.environment.background_color = {0, .2f, 0};
            const float x = frame >= 8 ? .5f : -.5f;
            world.render_items.push_back({.entity_id = 1,
                .model_matrix = Math::translate(Math::Mat4(1), {x, .2f, 0})
                                * Math::to_mat4(Math::angle_axis(.4f, Math::Vec3(0, 0, 1)))
                                * Math::scale(Math::Mat4(1), {.3f, .3f, 1}),
                .mesh = mesh,
                .material = {.resource = red}});
            ASSERT_TRUE(scene.value()->prepare_material_programs(world));
            RenderGeometry geometry;
            geometry.prepare(world);
            const auto view = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
            const auto projection = Math::ortho(-1, 1, -1, 1, .1f, 10);
            begin_frame(frames);
            auto drawn = scene.value()->render(
                frames, {world, ViewProjectMatrix{view, projection}}, geometry);
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
            const auto red_channel =
                format == Format::B8G8R8A8_UNORM || format == Format::B8G8R8A8_SRGB ? 2 : 0;
            const auto y = uint32_t(.4f * size.y);
            const auto current = (y * size.x + uint32_t((x * .5f + .5f) * size.x)) * 4;
            EXPECT_NEAR(std::to_integer<int>(bytes[current + red_channel]), mapped_byte(1), 3);
            if(frame == 7) {
                std::size_t partial_coverage = 0;
                for(std::size_t pixel = 0; pixel < bytes.size(); pixel += 4) {
                    const auto value = std::to_integer<int>(bytes[pixel + red_channel]);
                    partial_coverage += value > 4 && value < mapped_byte(1) - 4;
                }
                RecordProperty("taa_partial_coverage", std::to_string(partial_coverage));
                EXPECT_GT(partial_coverage, 20u);
            }
            if(frame >= 8) {
                const auto old = (y * size.x + uint32_t(.25f * size.x)) * 4;
                EXPECT_LT(std::to_integer<int>(bytes[old + red_channel]), 3);
                EXPECT_NEAR(std::to_integer<int>(bytes[old + 1]), mapped_byte(.2f), 3);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, TrailSegmentsFaceEachCameraAndKeepBillboardsWorking) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 2> pixels;
        Format format{};
        auto particles = std::make_shared<ParticleSnapshot>();
        particles->instances = {{{-.8f, 0, 0, .3f}, {1, 0, 0, 1}, {0, 0, 1, 1}, {.8f, 0, 0, .3f}},
            {{0, .6f, 0, .25f}, {0, 1, 0, 1}}};
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            RenderWorld world;
            world.particles = particles;
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            const auto up = frame == 0 ? Math::Vec3{0, 1, 0} : Math::Vec3{1, 0, 0};
            auto drawn = scene.value()->render(frames,
                {world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, up),
                            Math::ortho(-1, 1, -1, 1, .1f, 10)}},
                geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            const auto output =
                scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
            format = output->get_image()->get_info().format;
            pixels[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            copy_output(frames, output->get_image(), pixels[frame], {33, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            const auto bytes = pixels[frame]->read();
            EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + 16) * 4 + (bgra ? 2 : 0)]),
                mapped_byte(1), 3);
            const auto green = frame == 0 ? (6 * 33 + 16) : (16 * 33 + 6);
            EXPECT_GT(std::to_integer<int>(bytes[green * 4 + 1]), 100);
        }
    }

    TEST_F(RenderGraphGpuTest, FlipbookCellsKeepTheirColorOrientationAndBaseMip) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const std::array<Math::Vec3, 4> colors{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}}};
        TextureData atlas{
            .width = 4, .height = 4, .pixels = std::vector<uint8_t>(80), .mip_levels = 2};
        for(unsigned y = 0; y < 4; ++y)
            for(unsigned x = 0; x < 4; ++x) {
                const auto color = colors[(y / 2) * 2 + x / 2];
                const auto offset = (y * 4 + x) * 4;
                for(unsigned channel = 0; channel < 3; ++channel)
                    atlas.pixels[offset + channel] = uint8_t(color[channel] * 255);
                atlas.pixels[offset + 3] = 255;
            }
        for(unsigned offset = 64; offset < 80; offset += 4) {
            atlas.pixels[offset] = atlas.pixels[offset + 2] = atlas.pixels[offset + 3] = 255;
            atlas.pixels[offset + 1] = 0;
        }
        auto texture = engine->get_render_resources().try_create_texture(atlas);
        ASSERT_TRUE(texture);
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 3> pixels;
        Format format{};
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            RenderWorld world;
            auto particles = std::make_shared<ParticleSnapshot>();
            if(frame == 0)
                particles->instances = {{{0, 0, 0, 2}, {1, 1, 1, 1}}};
            else {
                ParticleEmitterComponent settings;
                settings.atlas_columns = settings.atlas_rows = 2;
                settings.frame_count = 4;
                settings.frame_rate = 1;
                for(unsigned cell = 0; cell < (frame == 1 ? 4u : 1u); ++cell) {
                    Math::Vec3 position{};
                    if(frame == 1)
                        position = {cell % 2 == 0 ? -.5f : .5f, cell / 2 == 0 ? .5f : -.5f, 0};
                    particles->instances.push_back({Math::Vec4(position, frame == 1 ? .8f : .04f),
                        {1, 1, 1, 1}, settings.texture_region(cell)});
                }
            }
            particles->batches = {
                {AssetHandle{1}, 0, uint32_t(particles->instances.size()), ParticleBlend::Alpha}};
            world.particles = particles;
            world.particle_textures = {texture.value()};
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            auto drawn = scene.value()->render(frames,
                {world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                            Math::ortho(-1, 1, -1, 1, .1f, 10)}},
                geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            const auto output =
                scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
            format = output->get_image()->get_info().format;
            pixels[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            copy_output(frames, output->get_image(), pixels[frame], {33, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            const auto bytes = pixels[frame]->read();
            for(unsigned cell = 0; cell < (frame == 2 ? 1u : 4u); ++cell) {
                const auto x = frame == 2 ? 16 : (cell % 2 == 0 ? 8 : 24);
                const auto y = frame == 2 ? 16 : (cell / 2 == 0 ? 8 : 24);
                for(unsigned channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(
                                    bytes[(y * 33 + x) * 4 + (bgra ? 2 - channel : channel)]),
                        mapped_byte(colors[cell][channel]), 2)
                        << frame << '/' << cell;
            }
        }
    }

    TEST_F(
        RenderGraphGpuTest, SoftParticlesShareCameraDepthWithAoAndHandleProjectionResizeAndMsaa) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const auto mesh = lit_quad();
        auto black = std::make_shared<Material>("occluder", "unlit_color");
        ASSERT_TRUE(black->set_vector_property("color", {0, 0, 0, 1}));
        auto white = engine->get_render_resources().try_create_texture(
            TextureData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        ASSERT_TRUE(white);
        for(const auto samples : {SampleCount::Count1, SampleCount::Count4}) {
            for(const bool perspective : {false, true}) {
                SCOPED_TRACE(static_cast<unsigned>(samples));
                SCOPED_TRACE(perspective);
                MaterialPrograms programs;
                auto scene = create_scene(programs, {33, 33}, samples);
                ASSERT_TRUE(scene);
                FrameScheduler frames(device, 2);
                frames.initialize_swapchain_images(2);
                FrameWait wait{device, frames};
                const std::array<float, 5> soft_distances{0, 1, 2, 0, 1000};
                for(unsigned frame = 0; frame < soft_distances.size(); ++frame) {
                    SCOPED_TRACE(frame);
                    const auto size = frame >= 3 ? Math::Vec2u(45, 39) : Math::Vec2u(33);
                    if(frame == 3)
                        ASSERT_TRUE(scene.value()->resize_offscreen_target(size));
                    PostProcessSettings settings;
                    settings.ao_enabled = frame == 2;
                    ASSERT_TRUE(scene.value()->prepare_post_process(settings));
                    const float distance = soft_distances[frame];
                    RenderWorld world;
                    const auto transform =
                        Math::translate(Math::Mat4(1), {0, .5f, 0})
                        * Math::scale(Math::Mat4(1), {frame == 4 ? -1.f : 1.f, .5f, 1});
                    world.render_items.push_back({.entity_id = 1,
                        .model_matrix = transform,
                        .mesh = mesh,
                        .material = {.resource = black}});
                    auto particles = std::make_shared<ParticleSnapshot>();
                    particles->instances = {
                        {{0, .5f, .75f, .5f}, {1, 0, 0, 1}}, {{0, -.5f, .75f, .5f}, {1, 0, 0, 1}}};
                    particles->batches = {{AssetHandle(1), 0, 2, ParticleBlend::Alpha, distance}};
                    world.particles = particles;
                    world.particle_textures = {white.value()};
                    ASSERT_TRUE(scene.value()->prepare_material_programs(world));
                    RenderGeometry geometry;
                    geometry.prepare(world);
                    const auto view = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
                    const auto projection =
                        perspective ? Math::perspective(90, float(size.x) / size.y, .1f, 10)
                                    : Math::ortho(-1, 1, -1, 1, .1f, 10);
                    begin_frame(frames);
                    auto drawn = scene.value()->render(
                        frames, {world, ViewProjectMatrix{view, projection}}, geometry);
                    ASSERT_TRUE(drawn) << drawn.error();
                    const auto output = scene.value()->get_offscreen_color_view(
                        frames.get_current_frame_slot_index());
                    const auto format = output->get_image()->get_info().format;
                    const auto readback = std::make_shared<Readback>(
                        device, context.get_context().get_physical_device(), size.x * size.y * 4);
                    copy_output(frames, output->get_image(), readback, size);
                    submit(device, frames, drawn.value());
                    frames.wait_for_all_slots();
                    const auto bytes = readback->read();
                    const auto red =
                        format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM ? 2u
                                                                                            : 0u;
                    for(const bool upper : {true, false}) {
                        const auto clip =
                            projection * view * Math::Vec4(0, upper ? .5f : -.5f, .75f, 1);
                        const auto x = uint32_t((clip.x / clip.w * .5f + .5f) * size.x);
                        const auto y = uint32_t((.5f - clip.y / clip.w * .5f) * size.y);
                        const float expected = upper && distance > 0 ? .25f / distance : 1;
                        EXPECT_NEAR(std::to_integer<int>(bytes[(y * size.x + x) * 4 + red]),
                            mapped_byte(expected), 2)
                            << "upper=" << upper;
                    }
                }
            }
        }
    }

    TEST_F(RenderGraphGpuTest, ExplicitSceneWarmupPreparesOnlyActualSurfaceAndMirrorCombinations) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const auto mesh = lit_quad();
        auto opaque = std::make_shared<Material>("opaque", "unlit_color");
        auto masked = std::make_shared<Material>("masked", "unlit_color");
        auto blended = std::make_shared<Material>("blended", "unlit_color");
        for(const auto& material : {opaque, masked, blended})
            ASSERT_TRUE(material->set_vector_property("color", {1, .2f, .1f, 1}));
        ASSERT_TRUE(masked->set_surface(
            {.alpha_mode = AlphaMode::Mask, .alpha_cutoff = .25f, .double_sided = true}));
        ASSERT_TRUE(blended->set_surface({.alpha_mode = AlphaMode::Blend, .double_sided = true}));
        for(const auto samples : {SampleCount::Count1, SampleCount::Count4}) {
            SCOPED_TRACE(static_cast<uint32_t>(samples));
            MaterialPrograms programs;
            auto scene = create_scene(programs, {33, 33}, samples);
            ASSERT_TRUE(scene);
            RenderWorld world;
            for(const auto& [handle, material] :
                {std::pair{1u, opaque}, {2u, masked}, {3u, blended}}) {
                world.render_items.push_back({.entity_id = handle,
                    .model_matrix = Math::translate(Math::Mat4(1), {10, 0, 0}),
                    .mesh = mesh,
                    .material = {AssetHandle(handle), material->snapshot()}});
                if(handle != 1) {
                    auto mirrored = world.render_items.back();
                    mirrored.entity_id += 10;
                    mirrored.model_matrix *= Math::scale(Math::Mat4(1), {-1, 1, 1});
                    world.render_items.push_back(std::move(mirrored));
                }
            }
            ASSERT_TRUE(scene.value()->prepare_material_programs(world));
            ASSERT_TRUE(scene.value()->warmup_scene(world));
            const auto warmed = scene.value()->get_material_statistics().pipeline_creations;
            EXPECT_EQ(scene.value()->get_material_statistics().warmup_pipeline_creations, 6u);
            ASSERT_TRUE(scene.value()->warmup_scene(world));
            EXPECT_EQ(scene.value()->get_material_statistics().warmup_pipeline_creations, 0u);
            EXPECT_EQ(scene.value()->get_material_statistics().pipeline_creations, warmed);

            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            for(unsigned frame = 0; frame < 3; ++frame) {
                for(auto& item : world.render_items)
                    item.model_matrix[3].x = frame == 0 ? 10.f : 0.f;
                ASSERT_TRUE(scene.value()->prepare_material_programs(world));
                begin_frame(frames);
                RenderGeometry geometry;
                geometry.prepare(world);
                auto drawn = scene.value()->render(
                    frames, {world, ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)}}, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                EXPECT_EQ(scene.value()->get_material_statistics().pipeline_creations, warmed);
                EXPECT_EQ(scene.value()->get_material_statistics().material_versions_created, 0u);
                EXPECT_EQ(scene.value()->get_material_statistics().drawn_instances,
                    frame == 0 ? 0u : world.render_items.size());
                submit(device, frames, drawn.value());
            }
            frames.wait_for_all_slots();
            world.post_process.weighted_oit = true;
            ASSERT_TRUE(scene.value()->prepare_post_process(world.post_process));
            ASSERT_TRUE(scene.value()->warmup_scene(world));
            const auto weighted_warmed =
                scene.value()->get_material_statistics().pipeline_creations;
            EXPECT_GT(weighted_warmed, warmed);
            ASSERT_TRUE(scene.value()->warmup_scene(world));
            EXPECT_EQ(scene.value()->get_material_statistics().warmup_pipeline_creations, 0u);
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            auto drawn = scene.value()->render(
                frames, {world, ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)}}, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.value()->get_material_statistics().pipeline_creations, weighted_warmed);
            submit(device, frames, drawn.value());
            frames.wait_for_all_slots();
        }
    }

    TEST_F(RenderGraphGpuTest, SceneWarmupSettlesPendingQualityBeforePreparingPipelines) {
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(prepare_offscreen_host({33, 33}));
        auto quality = renderer.get_quality_settings();
        quality.msaa_samples = 4;
        ASSERT_TRUE(renderer.request_quality_settings(quality));
        ASSERT_TRUE(renderer.quality_pending());
        auto world = std::make_shared<RenderWorld>();
        world->cameras.push_back({.primary = true});
        world->render_items.push_back({.entity_id = 1,
            .mesh = lit_quad(),
            .material = {.resource = std::make_shared<Material>("surface", "unlit_color")}});
        ASSERT_TRUE(renderer.warmup_scene(world));
        EXPECT_FALSE(renderer.quality_pending());
        EXPECT_EQ(renderer.get_quality_settings().msaa_samples, 4u);
        const auto warmed =
            renderer.get_scene_renderer().get_material_statistics().pipeline_creations;
        for(unsigned frame = 0; frame < 2; ++frame) {
            ASSERT_TRUE(render_frontend->prepare_frame());
            ASSERT_TRUE(renderer.render_frame(world));
            EXPECT_EQ(
                renderer.get_scene_renderer().get_material_statistics().pipeline_creations, warmed);
        }
    }

    TEST_F(RenderGraphGpuTest, ParticlesAndTransparentMeshesShareDepthOrderAndSurvivePassSwitches) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto mesh = lit_quad();
        ASSERT_TRUE(mesh);
        auto green = std::make_shared<Material>("glass green", "unlit_color");
        auto blue = std::make_shared<Material>("glass blue", "unlit_color");
        ASSERT_TRUE(green->set_vector_property("color", {0, 1, 0, .5}));
        ASSERT_TRUE(blue->set_vector_property("color", {0, 0, 1, .5}));
        const MaterialSurface transparent{.alpha_mode = AlphaMode::Blend, .double_sided = true};
        ASSERT_TRUE(green->set_surface(transparent));
        ASSERT_TRUE(blue->set_surface(transparent));
        auto particles = std::make_shared<ParticleSnapshot>();
        particles->instances = {{{0, 0, 1, 1}, {1, 0, 0, .5}}, {{0, 0, -2, 1}, {1, 0, 0, .5}},
            {{0, 0, -.5, 1}, {1, 1, 0, .25}}};
        particles->batches = {
            {{}, 0, 2, ParticleBlend::Alpha}, {{}, 2, 1, ParticleBlend::Additive}};
        const auto original = particles->instances;
        auto additive = std::make_shared<ParticleSnapshot>(*particles);
        additive->batches[0].blend = ParticleBlend::Additive;
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 7> pixels;
        Format format{};
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            SCOPED_TRACE(frame);
            if(frame == 5)
                ASSERT_TRUE(green->set_surface({.double_sided = true}));
            if(frame == 6) {
                ASSERT_TRUE(green->set_surface(transparent));
                scene = create_scene(programs, {33, 33}, SampleCount::Count4);
                ASSERT_TRUE(scene);
            }
            RenderWorld world;
            if(frame != 3) {
                world.render_items = {
                    {.entity_id = 1,
                        .model_matrix = Math::translate(Math::Mat4(1), {0, 0, -.5f}),
                        .mesh = mesh,
                        .material = {.resource = green}},
                    {.entity_id = 2,
                        .model_matrix = Math::translate(Math::Mat4(1), {0, 0, -1.5f}),
                        .mesh = mesh,
                        .material = {.resource = blue}}};
            }
            if(frame != 4) {
                world.particles = frame == 2 ? additive : particles;
                world.particle_textures.resize(2);
            }
            RenderGeometry geometry;
            geometry.prepare(world);
            const float camera_z = frame == 1 ? -4 : 4;
            RenderSubmission submission{
                world, ViewProjectMatrix{Math::look_at({0, 0, camera_z}, {0, 0, 0}, {0, 1, 0}),
                           Math::ortho(-1, 1, -1, 1, .1f, 10)}};
            ASSERT_TRUE(scene.value()->prepare_material_programs(world));
            begin_frame(frames);
            const auto slot = frames.get_current_frame_slot_index();
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.value()->get_material_statistics().draw_calls, frame == 3 ? 0u : 2u);
            pixels[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            const auto output = scene.value()->get_offscreen_color_view(slot);
            format = output->get_image()->get_info().format;
            copy_output(frames, output->get_image(), pixels[frame], {33, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        EXPECT_EQ(particles->instances, original);
        const std::array expected{Math::Vec3{.625, .3125, .125}, Math::Vec3{.625, .1875, .25},
            Math::Vec3{.75, .625, .25}, Math::Vec3{.875, .125, 0}, Math::Vec3{0, .5, .25},
            Math::Vec3{.5, .5, 0}, Math::Vec3{.625, .3125, .125}};
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            const auto bytes = pixels[frame]->read();
            for(unsigned channel = 0; channel < 3; ++channel) {
                const auto component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + 16) * 4 + component]),
                    mapped_byte(expected[frame][channel]), 2)
                    << frame;
            }
        }
    }

    TEST_F(RenderGraphGpuTest, ParticleAlphaSortsAcrossTexturesAndBlendsForEachCamera) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& assets = engine->get_asset_registry();
        for(const auto handle : {AssetHandle{9951}, AssetHandle{9952}}) {
            auto texture = engine->get_render_resources().try_create_texture(
                TextureData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
            ASSERT_TRUE(texture);
            ASSERT_TRUE(assets.register_asset(handle, texture.value()));
        }
        auto particles = std::make_shared<ParticleSnapshot>();
        particles->instances = {{{0, 0, 1, 1}, {1, 0, 0, .5}}, {{0, 0, -1, 1}, {0, 0, 1, .5}},
            {{0, 0, 0, 1}, {0, 1, 0, .5}}, {{0, 0, -2, 1}, {1, 1, 0, .5}}};
        particles->batches = {{AssetHandle{9951}, 0, 2, ParticleBlend::Alpha},
            {AssetHandle{9952}, 2, 1, ParticleBlend::Alpha},
            {AssetHandle{9951}, 3, 1, ParticleBlend::Additive}};
        const auto original = particles->instances;
        auto additive = std::make_shared<ParticleSnapshot>(*particles);
        for(auto& batch : additive->batches)
            batch.blend = ParticleBlend::Additive;
        RenderScene input{.scene_lifetime = 1};
        input.item_changes.begin_update(true);
        SceneResolver resolver(assets);
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 4> pixels;
        Format format{};
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            SCOPED_TRACE(frame);
            input.particles = frame == 2 ? additive : particles;
            auto world = resolver.update(input);
            RenderGeometry geometry;
            geometry.prepare(*world);
            const float camera_z = frame == 1 ? -3 : 3;
            RenderSubmission submission{
                *world, ViewProjectMatrix{Math::look_at({0, 0, camera_z}, {0, 0, 0}, {0, 1, 0}),
                            Math::ortho(-1, 1, -1, 1, .1f, 10)}};
            begin_frame(frames);
            const auto slot = frames.get_current_frame_slot_index();
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            pixels[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            const auto output = scene.value()->get_offscreen_color_view(slot);
            format = output->get_image()->get_info().format;
            copy_output(frames, output->get_image(), pixels[frame], {33, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        EXPECT_EQ(particles->instances, original);
        const std::array expected{Math::Vec3{.5625, .3125, .125}, Math::Vec3{.625, .75, .5},
            Math::Vec3{1, 1, .5}, Math::Vec3{.5625, .3125, .125}};
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            const auto bytes = pixels[frame]->read();
            for(unsigned channel = 0; channel < 3; ++channel) {
                const auto component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + 16) * 4 + component]),
                    mapped_byte(expected[frame][channel]), 2)
                    << frame;
            }
        }
    }

    TEST_F(RenderGraphGpuTest, ParticleTexturesReloadAndKeepEarlierFrameBindingsAlive) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& assets = engine->get_asset_registry();
        const AssetHandle handle{9950};
        const auto texture = [&](std::vector<uint8_t> pixel) {
            auto uploaded = engine->get_render_resources().try_create_texture(
                TextureData{.width = 1, .height = 1, .pixels = std::move(pixel)});
            EXPECT_TRUE(uploaded);
            return uploaded.value();
        };
        ASSERT_TRUE(assets.register_asset(handle, texture({255, 0, 0, 128})));
        auto particles = std::make_shared<ParticleSnapshot>();
        particles->instances.push_back({{0, 0, 0, 1}, {1, 1, 1, 1}});
        particles->batches.push_back({handle, 0, 1});
        RenderScene input{.scene_lifetime = 1, .particles = particles};
        input.item_changes.begin_update(true);
        SceneResolver resolver(assets);
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 4> pixels;
        std::shared_ptr<const RenderWorld> old_world;
        Format format{};
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            SCOPED_TRACE(frame);
            if(frame == 1)
                ASSERT_TRUE(assets.replace_asset(handle, texture({0, 255, 0, 64})));
            else if(frame == 2)
                ASSERT_TRUE(assets.unregister_asset(handle));
            else if(frame == 3)
                ASSERT_TRUE(assets.register_asset(handle, texture({0, 0, 255, 255})));
            auto world = resolver.update(input);
            if(frame == 0)
                old_world = world;
            else
                EXPECT_NE(old_world->particle_textures.front(), world->particle_textures.front());
            RenderGeometry geometry;
            geometry.prepare(*world);
            RenderSubmission submission{
                *world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                            Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
            begin_frame(frames);
            const auto slot = frames.get_current_frame_slot_index();
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            pixels[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            const auto output = scene.value()->get_offscreen_color_view(slot);
            format = output->get_image()->get_info().format;
            copy_output(frames, output->get_image(), pixels[frame], {33, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const std::array expected{Math::Vec3{128.f / 255, 0, 0}, Math::Vec3{0, 64.f / 255, 0},
            Math::Vec3{0}, Math::Vec3{0, 0, 1}};
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            const auto bytes = pixels[frame]->read();
            for(unsigned channel = 0; channel < 3; ++channel) {
                const auto component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + 16) * 4 + component]),
                    mapped_byte(expected[frame][channel]), 2)
                    << frame;
            }
        }
        const auto diagnostics = messages.str();
        EXPECT_NE(diagnostics.find("Particle emitter references missing texture handle 9950"),
            std::string::npos);
        EXPECT_EQ(std::count(diagnostics.begin(), diagnostics.end(), '\n'), 1);
        EXPECT_EQ(diagnostics.find("VUID-"), std::string::npos);
        EXPECT_EQ(diagnostics.find("Validation Error"), std::string::npos);
        messages.str({});
        messages.clear();
    }

    TEST_F(RenderGraphGpuTest, ParticleBillboardsBlendAndRespectDepthAcrossFrameSlots) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto mesh = lit_quad();
        ASSERT_TRUE(mesh);
        auto material = std::make_shared<Material>("occluder", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {0, 0, 1, 1}));
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 5> pixels;
        Format format{};
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            SCOPED_TRACE(frame);
            RenderWorld world;
            if(frame > 0)
                world.render_items.push_back(
                    {.entity_id = 1, .mesh = mesh, .material = {.resource = material}});
            auto particles = std::make_shared<ParticleSnapshot>();
            // 后方实例应被遮挡；相同位置的红、绿前景实例做加法合成。
            particles->instances.push_back({{0, 0, 0, 1}, {1, 0, 0, 1}});
            if(frame > 1) {
                particles->instances[0].position_size.z = 1;
                particles->instances.push_back({{0, 0, 1, 1}, {0, 1, 0, 1}});
            }
            if(frame == 3)
                particles->instances.clear();
            if(frame == 4) {
                particles->instances[0].color.w = .5f;
                particles->instances[1].color.w = .5f;
                particles->instances[1].position_size.z = .5f;
                particles->instances.push_back({{0, 0, -1, 1}, {1000, 1000, 1000, 1}});
                particles->batches.push_back({{}, 0, 3, ParticleBlend::Alpha});
                world.particle_textures.resize(1);
            }
            world.particles = particles;
            RenderGeometry geometry;
            geometry.prepare(world);
            RenderSubmission submission{
                world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                           Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
            ASSERT_TRUE(scene.value()->prepare_material_programs(world));
            begin_frame(frames);
            const auto slot = frames.get_current_frame_slot_index();
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            pixels[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            const auto output = scene.value()->get_offscreen_color_view(slot);
            format = output->get_image()->get_info().format;
            copy_output(frames, output->get_image(), pixels[frame], {33, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const std::array expected{Math::Vec3{1, 0, 0}, Math::Vec3{0, 0, 1}, Math::Vec3{1, 1, 1},
            Math::Vec3{0, 0, 1}, Math::Vec3{.5, .25, .25}};
        for(unsigned frame = 0; frame < pixels.size(); ++frame) {
            const auto bytes = pixels[frame]->read();
            for(unsigned channel = 0; channel < 3; ++channel) {
                const auto component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + 16) * 4 + component]),
                    mapped_byte(expected[frame][channel]), 2)
                    << frame;
            }
        }
    }

    TEST_F(RenderGraphGpuTest, TwoViewsShareWorldDataWithIndependentCullingPickingAndTargets) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& assets = engine->get_asset_registry();
        const auto mesh = lit_quad();
        ASSERT_TRUE(mesh);
        const auto make_material = [](Math::Vec4 color) {
            auto material = std::make_shared<Material>("view", "unlit_color");
            EXPECT_TRUE(material->set_vector_property("color", color));
            return material;
        };
        ASSERT_TRUE(assets.register_asset(AssetHandle{9900}, mesh));
        ASSERT_TRUE(assets.register_asset(AssetHandle{9901}, make_material({1, 0, 0, 1})));
        ASSERT_TRUE(assets.register_asset(AssetHandle{9902}, make_material({0, 1, 0, 1})));
        RenderScene scene{.scene_lifetime = 1,
            .render_items = {{.entity_id = 1,
                                 .transform_revision = 1,
                                 .mesh_handle = AssetHandle{9900},
                                 .material_handle = AssetHandle{9901}},
                {.entity_id = 2,
                    .transform_revision = 1,
                    .model_matrix = Math::translate(Math::Mat4(1), {6, 0, 0}),
                    .mesh_handle = AssetHandle{9900},
                    .material_handle = AssetHandle{9902}}}};
        scene.item_changes.begin_update(true);
        SceneResolver resolver(assets);
        RenderCameraResolver camera_resolver;
        auto world = resolver.resolve(scene);
        RenderGeometry geometry;
        geometry.prepare(world);
        MaterialPrograms programs;
        auto left = create_scene(programs, {33, 33});
        auto right = create_scene(programs, {33, 17});
        ASSERT_TRUE(left);
        ASSERT_TRUE(right);
        const auto make_view = [](float x, Math::Vec2u size) {
            return RenderView{.render_size = size,
                .camera_selection = RenderView::CameraSelection::Override,
                .camera_override =
                    RenderCamera{.view_matrix = Math::look_at({x, 0, 3}, {x, 0, 0}, {0, 1, 0}),
                        .projection = RenderCamera::Projection::Orthographic,
                        .orthographic_height = 2,
                        .far_clip = 10}};
        };
        const auto left_view = make_view(0, {33, 33});
        auto right_view = make_view(6, {33, 17});
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 4> left_pixels, right_pixels;
        std::array<Math::Vec2u, 4> right_sizes;
        std::weak_ptr<ImageView> old_right;
        Format format{};
        for(unsigned frame = 0; frame < left_pixels.size(); ++frame) {
            SCOPED_TRACE(frame);
            if(frame == 2) {
                geometry.clear_frame();
                ASSERT_TRUE(assets.replace_asset(AssetHandle{9901}, make_material({0, 0, 1, 1})));
                resolver.refresh_assets(world);
                resolver.resolve(scene, world);
                geometry.prepare(world);
                right_view.render_size = {11, 23};
                ASSERT_TRUE(right.value()->resize_offscreen_target(right_view.render_size));
                EXPECT_FALSE(old_right.expired());
            } else if(frame == 3) {
                right = create_scene(programs, right_view.render_size);
                ASSERT_TRUE(right);
            }
            right_view.visible = frame != 1;
            const RenderSubmission a{world, camera_resolver.resolve(world, left_view)};
            const RenderSubmission b{world, camera_resolver.resolve(world, right_view)};
            const auto* items = world.render_items.data();
            const auto* bounds = geometry.get_items().front().world_bounds;
            const auto left_pick = pick_render_submission(a, {16, 16}, left_view.render_size);
            ASSERT_TRUE(left_pick);
            EXPECT_EQ(left_pick->entity_id, 1u);
            const auto right_pick =
                pick_render_submission(b, right_view.render_size / 2u, right_view.render_size);
            ASSERT_EQ(right_pick.has_value(), right_view.visible);
            if(right_pick)
                EXPECT_EQ(right_pick->entity_id, 2u);
            ASSERT_TRUE(left.value()->prepare_material_programs(world));
            ASSERT_TRUE(right.value()->prepare_material_programs(world));
            begin_frame(frames);
            const auto slot = frames.get_current_frame_slot_index();
            auto left_draw = left.value()->render(frames, a, geometry);
            ASSERT_TRUE(left_draw) << left_draw.error();
            auto right_draw = right.value()->render(frames, b, geometry);
            ASSERT_TRUE(right_draw) << right_draw.error();
            EXPECT_EQ(world.render_items.data(), items);
            EXPECT_EQ(geometry.get_items().front().world_bounds, bounds);
            const auto left_stats = left.value()->get_material_statistics();
            EXPECT_EQ(left_stats.draw_calls, 1u);
            EXPECT_EQ(left_stats.culled_items, 1u);
            const auto right_stats = right.value()->get_material_statistics();
            EXPECT_EQ(right_stats.draw_calls, right_view.visible ? 1u : 0u);
            EXPECT_EQ(right_stats.culled_items, right_view.visible ? 1u : 0u);
            left_pixels[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            right_sizes[frame] = right_view.render_size;
            right_pixels[frame] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(),
                    right_view.render_size.x * right_view.render_size.y * 4);
            const auto left_output = left.value()->get_offscreen_color_view(slot);
            const auto right_output = right.value()->get_offscreen_color_view(slot);
            EXPECT_NE(left_output, right_output);
            format = left_output->get_image()->get_info().format;
            copy_output(
                frames, left_output->get_image(), left_pixels[frame], left_view.render_size);
            copy_output(
                frames, right_output->get_image(), right_pixels[frame], right_view.render_size);
            for(const auto& semaphore : right_draw.value())
                merge_semaphore_wait(left_draw.value(), semaphore);
            submit(device, frames, left_draw.value());
            if(frame == 1)
                old_right = right_output;
            if(frame == 2)
                right.value().reset();
        }
        frames.wait_for_all_slots();
        EXPECT_TRUE(old_right.expired());
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(unsigned frame = 0; frame < left_pixels.size(); ++frame) {
            const auto a = left_pixels[frame]->read(), b = right_pixels[frame]->read();
            const Math::Vec3 left_color = frame < 2 ? Math::Vec3(1, 0, 0) : Math::Vec3(0, 0, 1);
            const Math::Vec3 right_color = frame == 1 ? Math::Vec3(0) : Math::Vec3(0, 1, 0);
            const auto size = right_sizes[frame];
            for(unsigned channel = 0; channel < 3; ++channel) {
                const auto component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(a[(16 * 33 + 16) * 4 + component]),
                    mapped_byte(left_color[channel]), 2);
                EXPECT_NEAR(
                    std::to_integer<int>(b[((size.y / 2) * size.x + size.x / 2) * 4 + component]),
                    mapped_byte(right_color[channel]), 2);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, InstancedPbrMatchesIndividualDrawsAcrossBufferGrowthAndMeshChanges) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        constexpr AssetHandle material_handle(9830), program_handle(9831);
        auto program = std::make_shared<ShaderProgramArtifact>();
        program->handle = program_handle;
        program->vertex_words.assign(PBR_VERT.begin(), PBR_VERT.end());
        program->fragment_words.assign(PBR_FRAG.begin(), PBR_FRAG.end());
        ASSERT_TRUE(engine->get_asset_registry().register_asset(program_handle, program));
        MaterialPrograms programs;
        auto batched = create_scene(programs, {65, 65});
        auto individual = create_scene(programs, {65, 65});
        ASSERT_TRUE(batched);
        ASSERT_TRUE(individual);
        auto material = std::make_shared<Material>("builtin", "pbr");
        auto reference = std::make_shared<Material>("project", "pbr", program_handle);
        auto mesh = lit_quad({1, 0, 1});
        ASSERT_TRUE(mesh);
        const auto other_mesh = lit_quad({1, 0, 1});
        ASSERT_TRUE(other_mesh);
        FrameScheduler batch_frames(device, 2), reference_frames(device, 2);
        batch_frames.initialize_swapchain_images(2);
        reference_frames.initialize_swapchain_images(2);
        FrameWait batch_wait{device, batch_frames}, reference_wait{device, reference_frames};
        std::vector<std::shared_ptr<Readback>> actual, expected;
        const std::array counts{
            2u, 32u, 2u, 64u, 64u, 64u, 64u, 2u, 2u, 2u, 8u, 8u, 8u, 8u, 8u, 8u};
        for(size_t frame = 0; frame < counts.size(); ++frame) {
            SCOPED_TRACE(frame);
            if(frame == 2) {
                mesh = lit_quad({0, 1, 1});
                ASSERT_TRUE(mesh);
            }
            if(frame == 3) {
                MeshData nonindexed{
                    .vertices = {{{-1, -1, 0.5f}, {}, {0, 1, 1}}, {{1, -1, 0.5f}, {}, {0, 1, 1}},
                        {{1, 1, 0.5f}, {}, {0, 1, 1}}, {{1, 1, 0.5f}, {}, {0, 1, 1}},
                        {{-1, 1, 0.5f}, {}, {0, 1, 1}}, {{-1, -1, 0.5f}, {}, {0, 1, 1}}}};
                auto uploaded = engine->get_render_resources().try_create_mesh(nonindexed);
                ASSERT_TRUE(uploaded);
                mesh = std::move(uploaded).value();
            }
            const Math::Vec4 color{0.8f, 0.2f + 0.1f * frame, 0.1f, 1};
            ASSERT_TRUE(material->set_vector_property("base_color", color));
            ASSERT_TRUE(reference->set_vector_property("base_color", color));
            RenderWorld world{
                .lights = {{.direction = {-0.4f, 0, -1}, .intensity = 4, .casts_shadow = true}}};
            RenderSubmission submission{
                world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                           Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
            for(unsigned group = 0; group < 3; ++group) {
                std::shared_ptr<const MaterialOverrides> overrides;
                if(group > 0)
                    overrides = std::make_shared<const MaterialOverrides>(
                        MaterialOverrides{.instance_id = group,
                            .material = material_handle,
                            .vector_properties = {{"base_color", {0.1f, 0.2f, 0.3f * group, 1}}}});
                for(unsigned index = 0; index < counts[frame]; ++index) {
                    auto model = Math::translate(
                        Math::Mat4(1), {-0.65f + 0.65f * group, index % 2 ? 0.35f : -0.35f, 0});
                    if(frame >= 6 && group == 1)
                        model[3].x += 0.04f;
                    const float x_scale = frame == 2 && group == 1 ? -0.27f : 0.27f;
                    model *= Math::scale(Math::Mat4(1), {x_scale, 0.28f, index % 2 ? 0.8f : 1.4f});
                    world.render_items.push_back({.entity_id = group * counts[frame] + index + 1,
                        .model_matrix = model,
                        .mesh = frame >= 10 && index % 2 ? other_mesh : mesh,
                        .material = {material_handle, material->snapshot(), overrides},
                        .vertex_bend = frame >= 12 ? Math::Vec4(.3f, -.2f, 1, 0) : Math::Vec4(0)});
                }
            }
            // 同一内容覆盖材质有序、Mesh 有序、倒序与循环移位提交。
            if(frame == 11) {
                std::sort(world.render_items.begin(), world.render_items.end(),
                    [](const auto& a, const auto& b) {
                        if(a.mesh != b.mesh)
                            return std::less<const Mesh*>{}(a.mesh.get(), b.mesh.get());
                        return a.entity_id < b.entity_id;
                    });
            } else if(frame == 12) {
                std::reverse(world.render_items.begin(), world.render_items.end());
            } else if(frame == 13) {
                std::rotate(world.render_items.begin(), world.render_items.begin() + 7,
                    world.render_items.end());
            }
            const auto draw = [&](SceneRenderer& scene, FrameScheduler& frames,
                                  std::vector<std::shared_ptr<Readback>>& outputs) {
                ASSERT_TRUE(scene.prepare_material_programs(world));
                begin_frame(frames);
                RenderGeometry geometry;
                geometry.prepare(world);
                auto rendered = scene.render(frames, submission, geometry);
                ASSERT_TRUE(rendered) << rendered.error();
                const auto shadow = scene.get_shadow_statistics();
                // 镜像变换需要相反的正面绕序，不能与普通实例共用一个剔除批次。
                EXPECT_EQ(shadow.cascades, 4u);
                EXPECT_EQ(
                    shadow.draw_calls, (frame == 2 || frame >= 10 ? 2u : 1u) * shadow.cascades);
                // 每组的一半物体落在三个级联内，其余落在四个级联内。
                // 柔性包络还覆盖一组额外的半组物体。
                EXPECT_EQ(shadow.drawn_instances,
                    3 * counts[frame] / 2 * 7 + (frame >= 12 ? counts[frame] / 2 : 0));
                auto output = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), 65 * 65 * 4);
                copy_output(frames,
                    scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                        ->get_image(),
                    output, {65, 65});
                submit(device, frames, rendered.value());
                outputs.push_back(std::move(output));
            };
            ASSERT_NO_FATAL_FAILURE(draw(*batched.value(), batch_frames, actual));
            const auto stats = batched.value()->get_material_statistics();
            EXPECT_EQ(stats.draw_calls, frame >= 10 ? 6u : 3u);
            EXPECT_EQ(stats.drawn_instances, 3 * counts[frame]);
            auto expected_upload = 3 * counts[frame] * sizeof(PushConstant);
            if(frame == 5 || frame == 9 || frame >= 14)
                expected_upload = 0;
            EXPECT_EQ(stats.instance_upload_bytes, expected_upload);
            for(auto& item : world.render_items) {
                item.material.resource = reference->snapshot();
                item.material.program_source = program;
            }
            ASSERT_NO_FATAL_FAILURE(draw(*individual.value(), reference_frames, expected));
            EXPECT_EQ(individual.value()->get_material_statistics().draw_calls, 3 * counts[frame]);
            EXPECT_EQ(individual.value()->get_material_statistics().instance_upload_bytes, 0u);
        }
        batch_frames.wait_for_all_slots();
        reference_frames.wait_for_all_slots();
        for(size_t frame = 0; frame < actual.size(); ++frame) {
            SCOPED_TRACE(frame);
            const auto a = actual[frame]->read(), b = expected[frame]->read();
            int difference = 0;
            for(size_t byte = 0; byte < a.size(); ++byte)
                difference = std::max(difference,
                    std::abs(std::to_integer<int>(a[byte]) - std::to_integer<int>(b[byte])));
            EXPECT_LE(difference, 2) << "frame=" << frame;
            unsigned lit_pixels = 0;
            for(size_t pixel = 0; pixel < a.size(); pixel += 4)
                if(std::to_integer<int>(a[pixel]) > 10 || std::to_integer<int>(a[pixel + 1]) > 10
                    || std::to_integer<int>(a[pixel + 2]) > 10)
                    ++lit_pixels;
            EXPECT_GT(lit_pixels, 500u);
        }
    }

    TEST_F(RenderGraphGpuTest, SameHandleWithDistinctMaterialSnapshotsKeepsEachColor) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene = create_scene(programs, {65, 65});
        ASSERT_TRUE(scene);
        const auto mesh = lit_quad();
        ASSERT_TRUE(mesh);
        RenderWorld world{};
        RenderSubmission submission{
            world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                       Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
        const std::array colors{
            Math::Vec4(1, 0, 0, 1), Math::Vec4(0, 1, 0, 1), Math::Vec4(0, 0, 1, 1)};
        for(unsigned group = 0; group < colors.size(); ++group) {
            auto material = std::make_shared<Material>("snapshot", "unlit_color");
            ASSERT_TRUE(material->set_vector_property("color", colors[group]));
            for(unsigned row = 0; row < 2; ++row) {
                const auto model =
                    Math::scale(Math::translate(Math::Mat4(1),
                                    {-0.65f + 0.65f * group, row ? 0.35f : -0.35f, 0}),
                        {0.25f, 0.25f, 1});
                world.render_items.push_back({.model_matrix = model,
                    .mesh = mesh,
                    .material = {AssetHandle{9832}, material->snapshot()}});
            }
        }
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        for(const bool interleaved : {false, true}) {
            SCOPED_TRACE(interleaved);
            if(interleaved) {
                std::swap(world.render_items[1], world.render_items[2]);
                std::swap(world.render_items[3], world.render_items[4]);
            }
            ASSERT_TRUE(scene.value()->prepare_material_programs(world));
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            auto rendered = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(rendered) << rendered.error();
            const auto stats = scene.value()->get_material_statistics();
            EXPECT_EQ(stats.draw_calls, interleaved ? 6u : 3u);
            EXPECT_EQ(stats.drawn_instances, 6u);
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 65 * 65 * 4);
            const auto image = scene.value()
                                   ->get_offscreen_color_view(frames.get_current_frame_slot_index())
                                   ->get_image();
            ASSERT_NO_FATAL_FAILURE(
                finish_readback(*scene.value(), frames, output, {65, 65}, rendered.value()));
            const auto bytes = output->read();
            const auto format = image->get_info().format;
            const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
            std::array<unsigned, 3> pixels{};
            for(size_t offset = 0; offset < bytes.size(); offset += 4) {
                const std::array rgb{std::to_integer<unsigned>(bytes[offset + (bgra ? 2 : 0)]),
                    std::to_integer<unsigned>(bytes[offset + 1]),
                    std::to_integer<unsigned>(bytes[offset + (bgra ? 0 : 2)])};
                for(size_t channel = 0; channel < 3; ++channel)
                    if(rgb[channel] > 20 && rgb[channel] > 2 * rgb[(channel + 1) % 3]
                        && rgb[channel] > 2 * rgb[(channel + 2) % 3])
                        ++pixels[channel];
            }
            for(const auto count : pixels)
                EXPECT_GT(count, 100u);
        }
    }

    TEST_F(RenderGraphGpuTest, ProjectShaderProgramShowsErrorUntilFixedVersionIsPublished) {
        constexpr AssetHandle program_handle(9811);
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene_owner = create_scene(programs, {4, 4});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        TemporaryDirectory sources;
        auto& registry = engine->get_asset_registry();
        const auto compile_program = [&](const float scale) {
            const auto path = sources.path() / "project.frag";
            const std::string fragment =
                "#version 450\n"
                "layout(location=0) out vec4 color;\n"
                "layout(set=1,binding=0,std140) uniform MaterialData {\n"
                "  vec4 color; float intensity; float frequency;\n"
                "} material;\n"
                "void main() { color = vec4(material.color.rgb * material.intensity "
                "* material.frequency * "
                + std::to_string(scale) + ", material.color.a); }\n";
            EXPECT_TRUE(write_text_file_atomic(path, fragment));
            auto compiled =
                ShaderCompiler::compile({.source = path, .stage = ShaderStage::Fragment});
            EXPECT_TRUE(compiled.succeeded()) << compiled.diagnostics;
            auto program = std::make_shared<ShaderProgramArtifact>();
            program->handle = program_handle;
            program->vertex_words.assign(UNLIT_COLOR_VERT.begin(), UNLIT_COLOR_VERT.end());
            program->fragment_words = std::move(compiled.words);
            program->material = ShaderProgramMaterial{
                .scalars = {{"intensity", "Intensity", 1.0f, 0.0f, 10.0f, 0.05f},
                    {"frequency", "Frequency", 1.0f, 0.0f, 10.0f, 0.1f}},
                .vectors = {{"color", "Color", {1, 1, 1, 1}, true}}};
            return program;
        };
        ASSERT_TRUE(registry.register_asset(program_handle, compile_program(0.25f)));
        auto left = std::make_shared<Material>("left", "unlit_color", program_handle);
        ASSERT_TRUE(left->set_vector_property("color", {0.6f, 0.3f, 0.1f, 1}));
        ASSERT_TRUE(left->set_scalar_property("frequency", 1.0f));
        auto right = std::make_shared<Material>("right", "unlit_color", program_handle);
        ASSERT_TRUE(right->set_vector_property("color", {0.1f, 0.3f, 0.6f, 1}));
        ASSERT_TRUE(right->set_scalar_property("frequency", 2.0f));
        auto quad = lit_quad();
        ASSERT_NE(quad, nullptr);
        RenderWorld world{
            .render_items = {{.model_matrix = Math::scale(
                                  Math::translate(Math::Mat4(1), {-0.5f, 0, 0}), {0.5f, 1, 1}),
                                 .mesh = quad,
                                 .material = {AssetHandle(9812), left}},
                {.model_matrix =
                        Math::scale(Math::translate(Math::Mat4(1), {0.5f, 0, 0}), {0.5f, 1, 1}),
                    .mesh = quad,
                    .material = {AssetHandle(9813), right}},
                {.model_matrix = Math::translate(Math::Mat4(1), {20, 0, 0}),
                    .mesh = quad,
                    .material = {AssetHandle(9812), left}}}};
        RenderSubmission submission{
            world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                       Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 5> outputs;
        std::shared_ptr<const ShaderProgramArtifact> accepted;
        for(std::size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1) {
                ASSERT_TRUE(registry.replace_asset(program_handle, compile_program(0.5f)));
            } else if(index == 2) {
                auto broken = compile_program(1.0f);
                broken->material->scalars[1].name = "missing";
                ASSERT_TRUE(registry.replace_asset(program_handle, std::move(broken)));
            } else if(index == 3) {
                ASSERT_TRUE(registry.unregister_asset(program_handle));
            } else if(index == 4) {
                ASSERT_TRUE(registry.register_asset(program_handle, compile_program(0.75f)));
            }
            for(auto& item : world.render_items) {
                item.material.resource = item.material.resource->snapshot();
                item.material.program_source =
                    registry.resolve<ShaderProgramArtifact>(program_handle);
            }
            ASSERT_TRUE(scene.prepare_material_programs(world));
            const auto* published = programs.published(program_handle, "unlit_color");
            ASSERT_NE(published, nullptr);
            const bool failed = index == 2 || index == 3;
            if(failed) {
                EXPECT_EQ(published->source, accepted);
                EXPECT_EQ(published->layout->get_scalars()[1].name, "frequency");
            } else {
                accepted = published->source;
            }
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            auto drawn = scene.render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.get_material_statistics().cached_material_versions, failed ? 0u : 2u);
            EXPECT_EQ(scene.get_material_statistics().culled_items, 0u);
            EXPECT_EQ(scene.get_material_statistics().draw_calls, failed ? 2u : 3u);
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 64);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())->get_image(),
                outputs[index], {4, 4});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const auto format = scene.get_offscreen_color_view(0)->get_image()->get_info().format;
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(std::size_t index = 0; index < outputs.size(); ++index) {
            const auto bytes = outputs[index]->read();
            const bool failed = index == 2 || index == 3;
            const float scale = index == 0 ? 0.25f : (index == 4 ? 0.75f : 0.5f);
            for(std::size_t side = 0; side < 2; ++side) {
                const auto pixel = (2 * 4 + (side == 0 ? 0 : 3)) * 4;
                auto color =
                    side == 0 ? Math::Vec3{0.6f, 0.3f, 0.1f} : Math::Vec3{0.1f, 0.3f, 0.6f};
                const float frequency = side == 0 ? 1.0f : 2.0f;
                color *= frequency * scale;
                if(failed)
                    color = {1, 0, 1};
                for(std::size_t channel = 0; channel < 3; ++channel) {
                    const auto component = bgra ? 2 - channel : channel;
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel + component]),
                        mapped_byte(color[channel]), 3);
                }
            }
        }
        const auto diagnostics = messages.str();
        EXPECT_NE(diagnostics.find("Missing or incompatible material parameter: missing"),
            std::string::npos);
        const auto failure = diagnostics.find("Cannot publish project Shader program");
        ASSERT_NE(failure, std::string::npos);
        EXPECT_EQ(diagnostics.find("Cannot publish project Shader program", failure + 1),
            std::string::npos);
        EXPECT_EQ(diagnostics.find("VUID-"), std::string::npos);
        EXPECT_EQ(diagnostics.find("Validation Error"), std::string::npos);
        messages.str(std::string{});
        messages.clear();
    }

    TEST_F(RenderGraphGpuTest, BuiltinShaderErrorSurvivesNewViewAndRecoversAfterSuccessfulReload) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        auto& assets = engine->get_asset_registry();
        ASSERT_TRUE(prepare_offscreen_host({4, 4}));
        const RenderView view{.render_size = {4, 4},
            .camera_selection = RenderView::CameraSelection::Override,
            .camera_override =
                RenderCamera{.view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                    .projection = RenderCamera::Projection::Orthographic,
                    .orthographic_height = 2,
                    .far_clip = 10}};
        ASSERT_TRUE(renderer.set_render_view(view));
        const auto mesh = lit_quad();
        ASSERT_TRUE(mesh);
        auto material = std::make_shared<Material>("color", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {0, 1, 0, 1}));
        ASSERT_TRUE(assets.register_asset(AssetHandle{9860}, mesh));
        ASSERT_TRUE(assets.register_asset(AssetHandle{9861}, material));
        RenderScene scene{.scene_lifetime = 1,
            .render_items = {{.entity_id = 1,
                .mesh_handle = AssetHandle{9860},
                .material_handle = AssetHandle{9861}}}};
        FrameScheduler readback_frames(device, 2);
        readback_frames.initialize_swapchain_images(2);
        FrameWait wait{device, readback_frames};
        const std::array failed_programs{std::string("unlit_color")};
        std::array<std::shared_ptr<Readback>, 6> outputs;
        Renderer::ViewId extra = Renderer::DEFAULT_VIEW;
        std::shared_ptr<Readback> rebuilt;
        Format format{};
        for(size_t index = 0; index < outputs.size(); ++index) {
            SCOPED_TRACE(index);
            if(index == 1) {
                ASSERT_TRUE(renderer.invalidate_material_shaders(failed_programs));
            } else if(index == 2) {
                auto created = renderer.create_offscreen_view({4, 4});
                ASSERT_TRUE(created) << created.error();
                extra = created.value();
                ASSERT_TRUE(renderer.set_render_view(view, extra));
            } else if(index == 3 || index == 5) {
                ASSERT_TRUE(renderer.reload_material_shaders(default_material_shaders()));
            } else if(index == 4) {
                auto broken = default_material_shaders();
                broken.at("unlit_color").fragment.clear();
                broken.erase("pbr");
                EXPECT_FALSE(renderer.reload_material_shaders(std::move(broken)));
            }
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 64);
            auto prepared = render_frontend->prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error();
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            const auto output = renderer.get_offscreen_frame();
            const auto extra_output = renderer.get_offscreen_frame(extra);
            format = output.color_view->get_image()->get_info().format;
            ASSERT_TRUE(render_frontend->render(scene));
            begin_frame(readback_frames);
            copy_output(readback_frames, output.color_view->get_image(), outputs[index], {4, 4});
            if(index == 2) {
                rebuilt = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), 64);
                copy_output(readback_frames, extra_output.color_view->get_image(), rebuilt, {4, 4});
            }
            submit(device, readback_frames);
        }
        readback_frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const auto check = [&](const Readback& output, const Math::Vec3 color) {
            const auto pixels = output.read();
            for(size_t channel = 0; channel < 3; ++channel) {
                const auto component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(pixels[(2 * 4 + 2) * 4 + component]),
                    mapped_byte(color[channel]), 3);
            }
        };
        for(size_t index = 0; index < outputs.size(); ++index) {
            SCOPED_TRACE(index);
            const bool failed = index == 1 || index == 2 || index == 4;
            check(*outputs[index], failed ? Math::Vec3(1, 0, 1) : Math::Vec3(0, 1, 0));
        }
        ASSERT_TRUE(rebuilt);
        check(*rebuilt, {1, 0, 1});
    }
    TEST_F(RenderGraphGpuTest, StaticLodSelectsBeforeInstancingAndSharesMeshWithShadow) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto high = lit_quad();
        auto low = engine->get_render_resources()
                       .try_create_mesh({.vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{0, 1, 0}}}})
                       .value();
        ASSERT_EQ(high->get_triangle_count(), 2u);
        ASSERT_EQ(low->get_triangle_count(), 1u);
        auto material = std::make_shared<Material>("LOD", "unlit_color");
        ASSERT_TRUE(material->set_surface({.double_sided = true}));
        auto lod = std::make_shared<ResolvedMeshLod>();
        lod->settings.level1 = AssetHandle{11};
        lod->settings.level1_height = .1f;
        lod->meshes[0] = low;
        RenderWorld world{.scene_lifetime = 1};
        for(unsigned index = 0; index < 4; ++index)
            world.render_items.push_back({.entity_id = index + 1,
                .model_matrix = Math::translate(Math::Mat4(1), {float(index) * 3 - 4.5f, 0, 0}),
                .mesh = high,
                .material = {.resource = material},
                .lod = lod});
        world.lights.push_back(
            {.type = LightType::Directional, .direction = {0, 0, -1}, .casts_shadow = true});
        MaterialPrograms programs;
        auto scene = create_scene(programs, {65, 65});
        ASSERT_TRUE(scene);
        RenderGeometry geometry;
        geometry.prepare(world);
        ASSERT_TRUE(scene.value()->prepare_material_programs(world));
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        for(unsigned frame = 0; frame < 3; ++frame) {
            const float half_height = frame == 1 ? 6.0f : 20.0f;
            RenderSubmission submission{world,
                ViewProjectMatrix{Math::look_at({0, 0, 10}, {0, 0, 0}, {0, 1, 0}),
                    Math::ortho(-half_height, half_height, -half_height, half_height, .1f, 100)}};
            begin_frame(frames);
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            const auto stats = scene.value()->get_material_statistics();
            EXPECT_EQ(stats.draw_calls, 1u);
            EXPECT_EQ(stats.drawn_instances, 4u);
            EXPECT_EQ(stats.drawn_triangles, frame == 1 ? 8u : 4u);
            const auto shadow = scene.value()->get_shadow_statistics();
            EXPECT_EQ(shadow.cascades, 4u);
            EXPECT_EQ(shadow.draw_calls, 4u);
            EXPECT_EQ(shadow.drawn_instances, 16u);
            EXPECT_EQ(shadow.drawn_triangles, frame == 1 ? 32u : 16u);
            submit(device, frames, drawn.value());
        }
    }
}
