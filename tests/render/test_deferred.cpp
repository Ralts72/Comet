#include "support/render_graph_gpu_fixture.h"
#include "render/material/material_programs.h"

#include <array>

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, DeferredMatchesForwardLightingAndMixedOpaqueTransparentSurfaces) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const Math::Vec2u size{49, 37};
        const auto mesh = lit_quad();
        auto pbr = std::make_shared<Material>("rough metal", "pbr");
        ASSERT_TRUE(pbr->set_vector_property("base_color", {.7f, .2f, .05f, 1}));
        ASSERT_TRUE(pbr->set_scalar_property("metallic", .6f));
        ASSERT_TRUE(pbr->set_scalar_property("roughness", .5f));
        const auto normal = engine->get_render_resources().try_create_texture(
            {.width = 1, .height = 1, .pixels = {180, 100, 255, 255}});
        ASSERT_TRUE(normal);
        pbr->set_texture_property("normal_texture", normal.value());
        auto unlit = std::make_shared<Material>("emission", "unlit_color");
        ASSERT_TRUE(unlit->set_vector_property("color", {.1f, .5f, .2f, 1}));
        auto glass = std::make_shared<Material>("glass", "unlit_color");
        ASSERT_TRUE(glass->set_vector_property("color", {.05f, .1f, .8f, .4f}));
        ASSERT_TRUE(glass->set_surface({.alpha_mode = AlphaMode::Blend}));
        const auto mask = engine->get_render_resources().try_create_texture(
            {.width = 2, .height = 1, .pixels = {255, 255, 255, 0, 255, 255, 255, 255}});
        ASSERT_TRUE(mask);
        auto cutout = std::make_shared<Material>("cutout", "pbr");
        ASSERT_TRUE(cutout->set_vector_property("base_color", {.1f, .3f, .7f, 1}));
        ASSERT_TRUE(cutout->set_scalar_property("roughness", .8f));
        ASSERT_TRUE(cutout->set_surface({.alpha_mode = AlphaMode::Mask, .double_sided = true}));
        cutout->set_texture_property("base_color_texture", mask.value());
        const ViewProjectMatrix camera{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
            Math::ortho(-1.2f, 1.2f, -.9f, .9f, .1f, 10)};
        RenderWorld world{.scene_lifetime = 1};
        world.environment.background_color = {.02f, .04f, .08f};
        world.render_items = {{.entity_id = 1, .mesh = mesh, .material = {AssetHandle(1), pbr}},
            {.entity_id = 2,
                .model_matrix = Math::translate(Math::Mat4(1), {-.4f, .3f, .3f})
                                * Math::scale(Math::Mat4(1), {.2f, .2f, 1}),
                .mesh = mesh,
                .material = {AssetHandle(2), unlit}},
            {.entity_id = 3,
                .model_matrix = Math::translate(Math::Mat4(1), {.4f, -.3f, .5f})
                                * Math::scale(Math::Mat4(1), {.25f, .2f, 1}),
                .mesh = mesh,
                .material = {AssetHandle(3), glass}},
            {.entity_id = 4,
                .model_matrix = Math::translate(Math::Mat4(1), {0, 0, .7f})
                                * Math::scale(Math::Mat4(1), {.3f, .2f, 1}),
                .mesh = mesh,
                .material = {AssetHandle(4), cutout}}};
        world.lights = {
            {.direction = {.1f, -.3f, -1}, .intensity = 2}, {.type = LightType::Point,
                                                                .position = {-.5f, .8f, 2},
                                                                .color = {.2f, .5f, 1},
                                                                .intensity = 4,
                                                                .range = 5}};
        std::array<std::vector<std::byte>, 2> results;
        for(unsigned path = 0; path < 2; ++path) {
            MaterialPrograms programs;
            auto scene = create_scene(programs, size, SampleCount::Count1, false,
                path ? RenderPath::Deferred : RenderPath::Forward);
            ASSERT_TRUE(scene) << scene.error();
            ASSERT_TRUE(scene.value()->prepare_material_programs(world));
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            auto drawn = scene.value()->render(frames, {world, camera}, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            const auto statistics = scene.value()->get_material_statistics();
            EXPECT_EQ(statistics.gbuffer_draw_calls != 0, path != 0);
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), size.x * size.y * 4);
            finish_readback(*scene.value(), frames, output, size, drawn.value());
            results[path] = output->read();
        }
        ASSERT_EQ(results[0].size(), results[1].size());
        unsigned lit_pixels = 0;
        for(std::size_t byte = 0; byte < results[0].size(); ++byte) {
            if(byte % 4 == 3)
                continue;
            const auto reference = std::to_integer<int>(results[0][byte]);
            lit_pixels += reference > 100;
            EXPECT_NEAR(std::to_integer<int>(results[1][byte]), reference, 3) << byte;
        }
        EXPECT_GT(lit_pixels, 100u);
    }

    TEST_F(RenderGraphGpuTest, DeferredUsesEditedShaderAndRestoresGBufferAcrossResize) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        Math::Vec2u size{41, 31};
        ASSERT_TRUE(prepare_offscreen_host(size));
        ASSERT_TRUE(renderer.request_quality_settings({1, 1, 1, false, RenderPath::Deferred}));
        constexpr AssetHandle mesh_handle(9870), material_handle(9871);
        const auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-1, -1, -2}}, {{1, -1, -2}}, {{0, 1, -2}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        auto material = std::make_shared<Material>("edited unlit", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {1, 0, 0, 1}));
        ASSERT_TRUE(engine->get_asset_registry().register_asset(mesh_handle, mesh.value()));
        ASSERT_TRUE(engine->get_asset_registry().register_asset(material_handle, material));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back(
            {.mesh_handle = mesh_handle, .material_handle = material_handle});
        TemporaryDirectory source;
        const auto shader = source.path() / "edited.frag";
        ASSERT_TRUE(write_text_file_atomic(shader,
            "#version 450\nlayout(location=0) out vec4 color;\n"
            "layout(set=1,binding=0,std140) uniform MaterialData {"
            "vec4 color; float intensity;} material;\n"
            "void main(){color=vec4(0,material.color.r*material.intensity,0,1);}\n"));
        const auto compiled =
            ShaderCompiler::compile({.source = shader, .stage = ShaderStage::Fragment});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        FrameScheduler readback_frames(device, 2);
        readback_frames.initialize_swapchain_images(2);
        FrameWait wait{device, readback_frames};
        for(unsigned step = 0; step < 4; ++step) {
            SCOPED_TRACE(step);
            if(step == 1) {
                auto shaders = default_material_shaders();
                shaders.at("unlit_color").fragment = compiled.words;
                ASSERT_TRUE(renderer.reload_material_shaders(shaders));
            } else if(step == 2)
                ASSERT_TRUE(renderer.reload_material_shaders(default_material_shaders()));
            else if(step == 3) {
                size = {61, 43};
                ASSERT_TRUE(renderer.set_render_view({.render_size = size}));
            }
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), size.x * size.y * 4);
            render_and_copy(renderer, scene, readback_frames, output);
            ASSERT_FALSE(HasFatalFailure());
            readback_frames.wait_for_all_slots();
            const auto stats = renderer.get_scene_renderer().get_material_statistics();
            EXPECT_EQ(stats.draw_calls, 1u);
            EXPECT_EQ(stats.gbuffer_draw_calls, step == 1 ? 0u : 1u);
            EXPECT_EQ(renderer.get_scene_size(), size);
            const auto bytes = output->read();
            const auto pixel = (size.y / 2 * size.x + size.x / 2) * 4;
            const auto view = renderer.get_scene_renderer().get_offscreen_color_view(0);
            const auto format = view->get_image()->get_info().format;
            const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
            const auto red = std::to_integer<int>(bytes[pixel + (bgra ? 2 : 0)]);
            const auto green = std::to_integer<int>(bytes[pixel + 1]);
            EXPECT_GT(step == 1 ? green : red, 180);
            EXPECT_LT(step == 1 ? red : green, 5);
        }
    }
}
