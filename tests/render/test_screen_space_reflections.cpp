#include "support/render_graph_gpu_fixture.h"
#include "render/material/material_programs.h"
#include "asset/artifact/shader_program_artifact.h"

#include <cmath>

namespace Comet::Tests {
    namespace {
        RenderWorld reflection_world(const std::shared_ptr<Mesh>& mesh,
            const std::shared_ptr<Material>& mirror, const std::shared_ptr<Material>& emitter) {
            const auto target = Math::translate(Math::Mat4(1), {0, .6f, -.5f})
                                * Math::scale(Math::Mat4(1), {.6f, .6f, 1});
            RenderWorld world{.scene_lifetime = 1};
            world.render_items = {
                {.entity_id = 1,
                    .model_matrix = Math::translate(Math::Mat4(1), {0, -.5f, 0})
                                    * Math::rotate(Math::Mat4(1), -Math::PI / 2, {1, 0, 0})
                                    * Math::scale(Math::Mat4(1), {2, 2, 1}),
                    .mesh = mesh,
                    .material = {AssetHandle(1), mirror}},
                {.entity_id = 2,
                    .model_matrix = target,
                    .mesh = mesh,
                    .material = {AssetHandle(2), emitter}}};
            return world;
        }
        ViewProjectMatrix reflection_camera() {
            return {
                Math::look_at({0, 2, 4}, {0, 0, 0}, {0, 1, 0}), Math::perspective(55, 1, .1f, 20)};
        }
        Math::Vec3 receiver_pixel(std::span<const std::byte> pixels, Math::Vec2u size,
            Format format, const ViewProjectMatrix& camera = reflection_camera(),
            Math::Vec3 point = {0, 0, 1}) {
            const auto clip = camera.projection * camera.view * Math::Vec4(point, 1);
            const auto x = unsigned((clip.x / clip.w * .5f + .5f) * float(size.x));
            const auto y = unsigned((.5f - clip.y / clip.w * .5f) * float(size.y));
            return read_sdr_pixel(pixels, size, format, {x, y});
        }
    }

    TEST_F(RenderGraphGpuTest, ScreenSpaceReflectionsFollowVisibleObjectsAndPreserveMisses) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        for(const auto& [orthographic, temporal] : {std::pair{false, false}, std::pair{false, true},
                std::pair{true, false}, std::pair{true, true}}) {
            SCOPED_TRACE(orthographic);
            SCOPED_TRACE(temporal);
            Math::Vec2u size{81, 81};
            MaterialPrograms programs;
            auto created =
                create_scene(programs, size, SampleCount::Count1, temporal, RenderPath::Deferred);
            ASSERT_TRUE(created) << created.error();
            auto& scene = *created.value();
            auto mirror = std::make_shared<Material>("mirror", "pbr");
            ASSERT_TRUE(mirror->set_vector_property("base_color", {1, 1, 1, 1}));
            ASSERT_TRUE(mirror->set_scalar_property("metallic", 1));
            ASSERT_TRUE(mirror->set_scalar_property("roughness", .05f));
            auto emitter = std::make_shared<Material>("reflected object", "unlit_color");
            ASSERT_TRUE(emitter->set_vector_property("color", {1, 0, 0, 1}));
            ASSERT_TRUE(emitter->set_scalar_property("intensity", 2));
            const auto mesh = lit_quad();
            auto world = reflection_world(mesh, mirror, emitter);
            if(orthographic)
                world.render_items[0].model_matrix =
                    Math::scale(Math::Mat4(1), {1, 1, 2}) * world.render_items[0].model_matrix;
            const auto target = world.render_items[1].model_matrix;
            auto camera = reflection_camera();
            if(orthographic)
                camera.projection = Math::ortho(-2, 2, -2, 2, .1f, 20);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            PostProcessSettings settings;
            int full_red = 0, fallback_blue = 0, rough_blue = 0;
            TemporaryDirectory environment_source;
            write_hdr(environment_source.path() / "blue.hdr", 16, 8,
                [](int, int) { return std::array<unsigned char, 4>{0, 0, 128, 129}; });
            auto data = EnvironmentImporter{}.import(environment_source.path() / "blue.hdr");
            ASSERT_TRUE(data) << data.error();
            auto environment =
                Environment::try_create(engine->get_render_resources(), data.value());
            ASSERT_TRUE(environment) << environment.error();
            enum class Expected {
                Black,
                Red,
                Green,
                DimGreen,
                BlueReference,
                Blue,
                ReplacedBlue,
                DimBlue,
                RoughReference,
                RoughBlue
            };
            struct Case {
                const char* name;
                Expected expected;
                bool enabled = true;
                bool offscreen = false;
                float shift_x = 0;
                float roughness = .05f;
                float strength = 1;
                bool ibl = false;
                bool resize = false;
                Math::Vec3 color{0, 1, 0};
            };
            const std::array cases{
                Case{.name = "disabled", .expected = Expected::Black, .enabled = false},
                Case{.name = "red object", .expected = Expected::Red, .color = {1, 0, 0}},
                Case{.name = "green object", .expected = Expected::Green},
                Case{.name = "onscreen movement", .expected = Expected::Black, .shift_x = 1.2f},
                Case{.name = "offscreen", .expected = Expected::Black, .offscreen = true},
                Case{.name = "rough surface", .expected = Expected::Black, .roughness = 1},
                Case{.name = "reduced strength", .expected = Expected::DimGreen, .strength = .25f},
                Case{.name = "zero strength", .expected = Expected::Black, .strength = 0},
                Case{.name = "resize", .expected = Expected::Green, .resize = true},
                Case{.name = "IBL reference",
                    .expected = Expected::BlueReference,
                    .enabled = false,
                    .ibl = true},
                Case{.name = "replace IBL", .expected = Expected::ReplacedBlue, .ibl = true},
                Case{.name = "IBL offscreen fallback",
                    .expected = Expected::Blue,
                    .offscreen = true,
                    .ibl = true},
                Case{.name = "blend with IBL",
                    .expected = Expected::DimBlue,
                    .strength = .5f,
                    .ibl = true},
                Case{.name = "rough IBL reference",
                    .expected = Expected::RoughReference,
                    .enabled = false,
                    .roughness = 1,
                    .ibl = true},
                Case{.name = "rough IBL fallback",
                    .expected = Expected::RoughBlue,
                    .roughness = 1,
                    .ibl = true}};
            for(const auto& sample : cases) {
                SCOPED_TRACE(sample.name);
                settings.reflections.enabled = sample.enabled;
                settings.reflections.strength = sample.strength;
                ASSERT_TRUE(emitter->set_vector_property(
                    "color", Math::Vec4(sample.ibl ? Math::Vec3(0) : sample.color, 1)));
                ASSERT_TRUE(mirror->set_scalar_property("roughness", sample.roughness));
                world.render_items[1].model_matrix =
                    Math::translate(Math::Mat4(1), {sample.offscreen ? 4 : sample.shift_x, 0, 0})
                    * target;
                world.environment.lighting = sample.ibl;
                world.environment_resource = sample.ibl ? environment.value() : nullptr;
                if(sample.resize) {
                    size = {93, 67};
                    ASSERT_TRUE(scene.resize_offscreen_target(size));
                }
                const auto post_process = scene.prepare_post_process(settings);
                ASSERT_TRUE(post_process) << post_process.error();
                ASSERT_TRUE(scene.prepare_material_programs(world));
                RenderGeometry geometry;
                geometry.prepare(world);
                begin_frame(frames);
                const auto drawn = scene.render(frames, {world, camera}, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                if(sample.shift_x != 0)
                    EXPECT_EQ(scene.get_material_statistics().culled_items, 0u);
                auto output = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), size.x * size.y * 4);
                const auto format =
                    scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                        ->get_image()
                        ->get_info()
                        .format;
                finish_readback(scene, frames, output, size, drawn.value());
                ASSERT_FALSE(HasFatalFailure());
                const auto point = orthographic ? Math::Vec3(0, 0, 1.2f) : Math::Vec3(0, 0, 1);
                const auto rgb = receiver_pixel(output->read(), size, format, camera, point);
                const auto red = int(rgb.r), green = int(rgb.g), blue = int(rgb.b);
                switch(sample.expected) {
                    case Expected::Black:
                        EXPECT_LT(red, 5);
                        EXPECT_LT(green, 5);
                        break;
                    case Expected::Red:
                        EXPECT_GT(red, 80);
                        EXPECT_LT(green, 5);
                        full_red = red;
                        break;
                    case Expected::Green:
                    case Expected::DimGreen:
                        EXPECT_GT(green, 20);
                        EXPECT_LT(red, 5);
                        if(sample.expected == Expected::DimGreen)
                            EXPECT_LT(green, full_red - 10);
                        break;
                    case Expected::BlueReference:
                        fallback_blue = blue;
                        EXPECT_GT(blue, 100);
                        break;
                    case Expected::Blue:
                        EXPECT_NEAR(blue, fallback_blue, 3);
                        break;
                    case Expected::ReplacedBlue:
                        EXPECT_LT(blue, fallback_blue / 4);
                        break;
                    case Expected::DimBlue:
                        EXPECT_GT(blue, 20);
                        EXPECT_LT(blue, fallback_blue - 10);
                        break;
                    case Expected::RoughReference:
                        rough_blue = blue;
                        break;
                    case Expected::RoughBlue:
                        EXPECT_NEAR(blue, rough_blue, 3);
                        break;
                }
            }
        }
    }
    TEST_F(RenderGraphGpuTest, ScreenSpaceReflectionsRejectForwardOcclusionAndTransparentLayers) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const Math::Vec2u size{81, 81};
        MaterialPrograms programs;
        auto created =
            create_scene(programs, size, SampleCount::Count1, false, RenderPath::Deferred);
        ASSERT_TRUE(created);
        auto& scene = *created.value();
        const auto post_process = scene.prepare_post_process({.reflections = {.enabled = true}});
        ASSERT_TRUE(post_process) << post_process.error();
        auto mirror = std::make_shared<Material>("mirror", "pbr");
        ASSERT_TRUE(mirror->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(mirror->set_scalar_property("metallic", 1));
        ASSERT_TRUE(mirror->set_scalar_property("roughness", .05f));
        auto red = std::make_shared<Material>("reflected", "unlit_color");
        ASSERT_TRUE(red->set_vector_property("color", {1, 0, 0, 1}));
        auto overlay = std::make_shared<Material>("overlay", "unlit_color");
        ASSERT_TRUE(overlay->set_vector_property("color", {0, 0, 1, .5f}));
        ASSERT_TRUE(overlay->set_surface({.alpha_mode = AlphaMode::Blend}));
        constexpr AssetHandle program_handle(19901);
        auto forward = std::make_shared<Material>("Forward", "unlit_color", program_handle);
        ASSERT_TRUE(forward->set_vector_property("color", {0, 0, 1, 1}));
        auto program = std::make_shared<ShaderProgramArtifact>();
        program->handle = program_handle;
        const auto source = default_material_shaders().at("unlit_color");
        program->vertex_words = source.vertex;
        program->fragment_words = source.fragment;
        const auto mesh = lit_quad();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        enum class Case {
            ForwardOccluder,
            TransparentOverlay,
            TransparentHit,
            ForwardHit,
            Restored
        };
        for(const auto sample : {Case::ForwardOccluder, Case::TransparentOverlay,
                Case::TransparentHit, Case::ForwardHit, Case::Restored}) {
            SCOPED_TRACE(int(sample));
            auto world = reflection_world(mesh, mirror, red);
            ASSERT_TRUE(red->set_surface(
                {.alpha_mode =
                        sample == Case::TransparentHit ? AlphaMode::Blend : AlphaMode::Opaque}));
            if(sample == Case::ForwardOccluder || sample == Case::TransparentOverlay) {
                auto cover = world.render_items.front();
                cover.entity_id = 3;
                cover.model_matrix =
                    Math::translate(Math::Mat4(1), {0, .1f, 0}) * cover.model_matrix;
                cover.material =
                    sample == Case::ForwardOccluder
                        ? MaterialBinding{.material_handle = AssetHandle(3),
                              .resource = forward,
                              .program_source = program}
                        : MaterialBinding{.material_handle = AssetHandle(4), .resource = overlay};
                world.render_items.push_back(cover);
            } else if(sample == Case::ForwardHit)
                world.render_items[1].material = {.material_handle = AssetHandle(3),
                    .resource = forward,
                    .program_source = program};
            ASSERT_TRUE(scene.prepare_material_programs(world));
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            auto drawn = scene.render(frames, {world, reflection_camera()}, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), size.x * size.y * 4);
            const auto format =
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                    ->get_image()
                    ->get_info()
                    .format;
            finish_readback(scene, frames, output, size, drawn.value());
            ASSERT_FALSE(HasFatalFailure());
            const auto rgb = receiver_pixel(output->read(), size, format);
            if(sample == Case::Restored)
                EXPECT_GT(rgb.r, 80);
            else
                EXPECT_LT(rgb.r, 5);
            if(sample == Case::ForwardOccluder || sample == Case::TransparentOverlay)
                EXPECT_GT(rgb.b, 100);
            else
                EXPECT_LT(rgb.b, 5);
        }
    }

    TEST_F(RenderGraphGpuTest, ScreenSpaceReflectionsStayStableAndDoNotRetainOldColors) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        constexpr Math::Vec2u size{129, 129};
        const auto mesh = lit_quad();
        for(const bool temporal : {false, true}) {
            SCOPED_TRACE(temporal);
            MaterialPrograms programs;
            auto created =
                create_scene(programs, size, SampleCount::Count1, temporal, RenderPath::Deferred);
            ASSERT_TRUE(created);
            auto& scene = *created.value();
            const auto post_process =
                scene.prepare_post_process({.reflections = {.enabled = true}});
            ASSERT_TRUE(post_process) << post_process.error();
            auto mirror = std::make_shared<Material>("temporal receiver", "pbr");
            ASSERT_TRUE(mirror->set_scalar_property("metallic", 1));
            ASSERT_TRUE(mirror->set_scalar_property("roughness", .15f));
            auto emitter = std::make_shared<Material>("moving reflected surface", "unlit_color");
            ASSERT_TRUE(emitter->set_vector_property("color", {1, 0, 0, 1}));
            ASSERT_TRUE(emitter->set_scalar_property("intensity", 2));
            auto world = reflection_world(mesh, mirror, emitter);
            auto camera = reflection_camera();
            const auto target = world.render_items[1].model_matrix;
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            auto readback = std::make_shared<Readback>(
                device, device.get_capability().physical_device, size.x * size.y * 4);
            Format format = Format::R8G8B8A8_UNORM;
            const auto capture = [&]() -> std::vector<std::byte> {
                const auto ready = scene.prepare_material_programs(world);
                EXPECT_TRUE(ready);
                if(!ready)
                    return {};
                RenderGeometry geometry;
                geometry.prepare(world);
                begin_frame(frames);
                const auto drawn = scene.render(frames, {world, camera}, geometry);
                EXPECT_TRUE(drawn);
                if(!drawn)
                    return {};
                format = scene.get_offscreen_color_view(frames.get_current_frame_slot_index())
                             ->get_image()
                             ->get_info()
                             .format;
                finish_readback(scene, frames, readback, size, drawn.value());
                const auto bytes = readback->read();
                return {bytes.begin(), bytes.end()};
            };
            std::vector<std::byte> previous;
            double max_rms = 0;
            // 下半图只有镜面反射；排除全黑像素，避免大块背景掩盖反射边缘的变化。
            for(int frame = 0; frame < 32; ++frame) {
                const auto pixels = capture();
                ASSERT_EQ(pixels.size(), size.x * size.y * 4);
                if(frame >= 8) {
                    double square = 0;
                    size_t samples = 0;
                    for(unsigned y = size.y / 2 + 3; y < size.y; ++y)
                        for(unsigned x = 0; x < size.x; ++x) {
                            const auto offset = (y * size.x + x) * 4;
                            bool visible = false;
                            for(int channel = 0; channel < 3; ++channel)
                                visible |= std::to_integer<int>(pixels[offset + channel]) > 5
                                           || std::to_integer<int>(previous[offset + channel]) > 5;
                            if(!visible)
                                continue;
                            for(int channel = 0; channel < 3; ++channel) {
                                const auto delta =
                                    std::to_integer<int>(pixels[offset + channel])
                                    - std::to_integer<int>(previous[offset + channel]);
                                square += delta * delta;
                                ++samples;
                            }
                        }
                    ASSERT_GT(samples, 0u);
                    max_rms = std::max(max_rms, std::sqrt(square / samples));
                }
                previous = pixels;
            }
            RecordProperty(
                temporal ? "static_reflection_rms_taa" : "static_reflection_rms_unfiltered",
                std::to_string(max_rms));
            EXPECT_LT(max_rms, temporal ? 20 : .01);
            ASSERT_TRUE(emitter->set_vector_property("color", {0, 1, 0, 1}));
            for(int frame = 0; frame < 8; ++frame) {
                const bool hidden = frame % 2 != 0;
                world.render_items[1].model_matrix =
                    Math::translate(Math::Mat4(1), {hidden ? 1.2f : 0.f, 0, 0}) * target;
                camera.view = Math::look_at({.02f * frame, 2, 4}, {0, 0, 0}, {0, 1, 0});
                const auto pixels = capture();
                ASSERT_EQ(pixels.size(), size.x * size.y * 4);
                const auto color = receiver_pixel(pixels, size, format, camera);
                EXPECT_LT(color.r, 5); // 换色和运动不保留上一次红色反射。
                if(hidden)
                    EXPECT_LT(color.g, 5);
                else
                    EXPECT_GT(color.g, 20);
            }
        }
    }

}
