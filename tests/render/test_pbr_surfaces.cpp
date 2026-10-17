#include <numbers>
#include "asset/import/mesh_importer.h"
#include "support/render_graph_gpu_fixture.h"
#include "render/material/material_programs.h"
#include "scene/material_parameters.h"
#include "graphics/resource/sampler.h"

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, SourceTextureSamplingControlsFilteringWrappingAndMaskShadows) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        const Math::Vec2u size{17, 17};
        auto scene = create_scene(programs, size);
        ASSERT_TRUE(scene);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        struct Scenario {
            Filter filter;
            SamplerAddressMode wrap;
            float u;
            Math::Vec3 color;
            bool shadow = false;
        };
        const Scenario cases[]{
            {Filter::Nearest, SamplerAddressMode::Repeat, 1.25f, {1, 0, 0}},
            {Filter::Nearest, SamplerAddressMode::ClampToEdge, 1.25f, {0, 1, 0}},
            {Filter::Nearest, SamplerAddressMode::MirrorRepeat, 1.25f, {0, 1, 0}},
            {Filter::Nearest, SamplerAddressMode::ClampToEdge, .4f, {1, 0, 0}},
            {Filter::Linear, SamplerAddressMode::ClampToEdge, .4f, {.7f, .3f, 0}},
            {Filter::Nearest, SamplerAddressMode::Repeat, 1.25f, {}, true},
            {Filter::Nearest, SamplerAddressMode::ClampToEdge, 1.25f, {}, true},
        };
        auto receiver = std::make_shared<Material>("receiver", "pbr");
        ASSERT_TRUE(receiver->set_scalar_property("roughness", 1));
        const auto receiver_mesh = lit_quad();
        std::vector<std::shared_ptr<Readback>> outputs;
        Format format{};
        for(const auto& sample : cases) {
            TextureData pixels{.width = 2,
                .height = 1,
                .pixels = {255, 0, 0, 0, 0, 255, 0, 255},
                .sampler = SamplerDesc{.mag_filter = sample.filter,
                    .min_filter = sample.filter,
                    .address_mode_u = sample.wrap}};
            auto texture = engine->get_render_resources().try_create_texture(pixels);
            ASSERT_TRUE(texture);
            ASSERT_TRUE(texture.value()->get_sampler());
            EXPECT_EQ(texture.value()->get_sampler()->get_description(), *pixels.sampler);
            MeshData data{.vertices = {{{-1, -1, .5f}, {sample.u, .5f}, {0, 0, 1}},
                              {{1, -1, .5f}, {sample.u, .5f}, {0, 0, 1}},
                              {{1, 1, .5f}, {sample.u, .5f}, {0, 0, 1}},
                              {{-1, 1, .5f}, {sample.u, .5f}, {0, 0, 1}}},
                .indices = {0, 1, 2, 2, 3, 0}};
            auto mesh = engine->get_render_resources().try_create_mesh(data);
            ASSERT_TRUE(mesh);
            auto material = std::make_shared<Material>("source sampling", "pbr");
            RenderWorld world;
            if(sample.shadow) {
                ASSERT_TRUE(material->set_surface({.alpha_mode = AlphaMode::Mask}));
                material->set_texture_property("base_color_texture", texture.value());
                world.render_items = {
                    {.mesh = receiver_mesh, .material = {AssetHandle(1), receiver}},
                    {.model_matrix = Math::scale(
                         Math::translate(Math::Mat4(1), {0, 0, 1.5f}), {.5f, .5f, .5f}),
                        .mesh = mesh.value(),
                        .material = {AssetHandle(2), material}}};
                world.lights = {{.intensity = Math::PI, .casts_shadow = true}};
            } else {
                ASSERT_TRUE(material->set_vector_property("base_color", {0, 0, 0, 1}));
                ASSERT_TRUE(material->set_vector_property("emissive_color", {1, 1, 1, 1}));
                material->set_texture_property("emissive_texture", texture.value());
                world.render_items = {
                    {.mesh = mesh.value(), .material = {AssetHandle(2), material}}};
            }
            RenderSubmission submission{world, ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)}};
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            const auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), size.x * size.y * 4);
            const auto view =
                scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
            format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), output, size);
            submit(device, frames, drawn.value());
            outputs.push_back(output);
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(size_t index = 0; index < outputs.size(); ++index) {
            SCOPED_TRACE(index);
            const auto bytes = outputs[index]->read();
            const auto& sample = cases[index];
            if(sample.shadow) {
                const int center = std::to_integer<int>(bytes[(8 * 17 + 8) * 4]);
                if(sample.wrap == SamplerAddressMode::Repeat)
                    EXPECT_GT(center, 150);
                else
                    EXPECT_LT(center, 20);
                continue;
            }
            for(unsigned channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(
                    std::to_integer<int>(bytes[(8 * 17 + 8) * 4 + (bgra ? 2 - channel : channel)]),
                    mapped_byte(sample.color[channel]), 3);
        }
    }

    TEST_F(RenderGraphGpuTest, SurfaceModesSortTransparentLayersAndPreserveDepthAndFacing) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        const Math::Vec2u size{9, 9};
        auto scene = create_scene(programs, size);
        ASSERT_TRUE(scene);
        const auto mesh = lit_quad();
        auto texture = engine->get_render_resources().try_create_texture(
            {.width = 1, .height = 1, .pixels = {255, 255, 255, 128}});
        ASSERT_TRUE(texture);
        auto blue = std::make_shared<Material>("background", "unlit_color");
        ASSERT_TRUE(blue->set_vector_property("color", {0, 0, 1, 1}));
        struct Scenario {
            const char* name;
            AlphaMode mode;
            float alpha;
            float cutoff;
            bool back;
            bool sided;
            int layers;
            bool pbr;
            Math::Vec3 expected;
            bool mirrored = false;
        };
        const Scenario cases[]{
            {"mirrored opaque", AlphaMode::Opaque, 1, .5f, false, false, 1, false, {1, 0, 0}, true},
            {"opposite winding batches", AlphaMode::Opaque, 1, .5f, false, false, 4, false,
                {1, 0, 0}, true},
            {"mirrored blend and ordinary instance", AlphaMode::Blend, .5f, .5f, false, false, 2,
                false, {.75f, 0, .25f}, true},
            {"opaque ignores alpha", AlphaMode::Opaque, 0, .5f, false, false, 1, false, {1, 0, 0}},
            {"mask hole", AlphaMode::Mask, .25f, .5f, false, false, 1, false, {0, 0, 1}},
            {"mask threshold", AlphaMode::Mask, .5f, .5f, false, false, 1, false, {1, 0, 0}},
            {"blend", AlphaMode::Blend, .5f, .5f, false, false, 1, false, {.5f, 0, .5f}},
            {"same material layers", AlphaMode::Blend, .5f, .5f, false, false, 2, false,
                {.75f, 0, .25f}},
            {"depth before material", AlphaMode::Blend, .5f, .5f, false, false, 3, false,
                {.5f, .25f, .25f}},
            {"single sided", AlphaMode::Opaque, 1, .5f, true, false, 1, false, {0, 0, 1}},
            {"double sided", AlphaMode::Opaque, 1, .5f, true, true, 1, false, {1, 0, 0}},
            {"texture times factor rejected", AlphaMode::Mask, .5f, .3f, false, false, 1, true,
                {0, 0, 1}},
            {"texture times factor accepted", AlphaMode::Mask, .5f, .2f, false, false, 1, true,
                {1, 0, 0}},
            {"pbr blend", AlphaMode::Blend, 1, .5f, false, false, 1, true,
                {128.f / 255, 0, 127.f / 255}},
        };
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::vector<std::shared_ptr<Readback>> outputs;
        Format format{};
        for(const auto& sample : cases) {
            SCOPED_TRACE(sample.name);
            auto red = std::make_shared<Material>("foreground", sample.pbr ? "pbr" : "unlit_color");
            ASSERT_TRUE(red->set_surface({sample.mode, sample.cutoff, sample.sided}));
            if(sample.pbr) {
                ASSERT_TRUE(red->set_vector_property("base_color", {0, 0, 0, sample.alpha}));
                ASSERT_TRUE(red->set_vector_property("emissive_color", {1, 0, 0, 1}));
                red->set_texture_property("base_color_texture", texture.value());
            } else
                ASSERT_TRUE(red->set_vector_property("color", {1, 0, 0, sample.alpha}));
            auto green = std::make_shared<Material>("middle", "unlit_color");
            ASSERT_TRUE(green->set_surface({AlphaMode::Blend}));
            ASSERT_TRUE(green->set_vector_property("color", {0, 1, 0, .5f}));
            auto transform = Math::translate(Math::Mat4(1), {0, 0, .5f});
            if(sample.back)
                transform = Math::rotate(transform, Math::PI, Math::Vec3(0, 1, 0));
            if(sample.mirrored)
                transform = Math::scale(transform, {-1, 1, 1});
            RenderWorld world{
                .render_items = {
                    {.model_matrix = transform, .mesh = mesh, .material = {AssetHandle(1), red}},
                    {.model_matrix = Math::translate(Math::Mat4(1), {0, 0, -1}),
                        .mesh = mesh,
                        .material = {AssetHandle(10), blue}}}};
            if(sample.layers > 1)
                world.render_items.push_back({.mesh = mesh,
                    .material = {AssetHandle(sample.layers != 3 ? 1 : 2),
                        sample.layers != 3 ? red : green}});
            RenderSubmission submission{
                world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                           Math::ortho(-1, 1, -1, 1, .1f, 10)}};
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            const auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            if(sample.layers > 1) {
                EXPECT_EQ(scene.value()->get_material_statistics().draw_calls, 3u);
                EXPECT_EQ(scene.value()->get_material_statistics().instanced_draw_calls, 0u);
            }
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 9 * 9 * 4);
            const auto view =
                scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
            format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), output, size);
            submit(device, frames, drawn.value());
            outputs.push_back(output);
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(size_t index = 0; index < outputs.size(); ++index) {
            SCOPED_TRACE(cases[index].name);
            const auto bytes = outputs[index]->read();
            for(unsigned channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(
                    std::to_integer<int>(bytes[(4 * 9 + 4) * 4 + (bgra ? 2 - channel : channel)]),
                    mapped_byte(cases[index].expected[channel]), 2);
        }
    }

    TEST_F(RenderGraphGpuTest, AlphaMaskShadowsFollowTextureFactorAndOverrides) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        const auto mesh = lit_quad();
        auto receiver = std::make_shared<Material>("receiver", "pbr");
        ASSERT_TRUE(receiver->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(receiver->set_scalar_property("roughness", 1));
        auto texture = engine->get_render_resources().try_create_texture(
            {.width = 2, .height = 1, .pixels = {255, 255, 255, 0, 255, 255, 255, 255}});
        ASSERT_TRUE(texture);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 5> outputs;
        for(unsigned index = 0; index < outputs.size(); ++index) {
            auto caster = std::make_shared<Material>("caster", "pbr");
            ASSERT_TRUE(caster->set_surface(
                {.alpha_mode = index == 2 ? AlphaMode::Blend : AlphaMode::Mask}));
            caster->set_texture_property("base_color_texture", texture.value());
            ASSERT_TRUE(
                caster->set_vector_property("base_color", {1, 1, 1, index == 1 ? .25f : 1.f}));
            std::shared_ptr<MaterialOverrides> overrides;
            if(index == 3) {
                overrides = std::make_shared<MaterialOverrides>();
                overrides->instance_id = 1;
                overrides->material = AssetHandle(2);
                overrides->vector_properties["base_color"] = {1, 1, 1, .25f};
            }
            RenderWorld world{
                .render_items = {{.mesh = mesh, .material = {AssetHandle(1), receiver}},
                    {.model_matrix = Math::scale(Math::translate(Math::Mat4(1), {0, 0, 1.5f}),
                         {index == 4 ? -.5f : .5f, .5f, .5f}),
                        .mesh = mesh,
                        .material = {AssetHandle(2), caster, overrides}}},
                .lights = {{.intensity = Math::PI, .casts_shadow = true}}};
            // The caster is outside the camera clip range, but remains visible to the light.
            RenderSubmission submission{world, ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)}};
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            const auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            outputs[index] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            const auto view =
                scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
            copy_output(frames, view->get_image(), outputs[index], {33, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const int lit = mapped_byte(
            float(pbr_reference({0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {1, 1, 1}, 0, 1, Math::PI).x));
        for(unsigned index = 0; index < outputs.size(); ++index) {
            SCOPED_TRACE(index);
            const auto bytes = outputs[index]->read();
            for(unsigned channel = 0; channel < 3; ++channel) {
                EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + 12) * 4 + channel]),
                    index == 4 ? 0 : lit, 3);
                EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + 20) * 4 + channel]),
                    index == 0 ? 0 : lit, 3);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, OcclusionAffectsIndirectLightingAndLeavesEmissionVisible) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        const Math::Vec2u size{9, 9};
        auto scene = create_scene(programs, size);
        ASSERT_TRUE(scene);
        TemporaryDirectory directory;
        write_hdr(directory.path() / "uniform.hdr");
        auto data = EnvironmentImporter{}.import(directory.path() / "uniform.hdr");
        ASSERT_TRUE(data);
        auto environment = Environment::try_create(engine->get_render_resources(), data.value());
        ASSERT_TRUE(environment);
        auto dark = engine->get_render_resources().try_create_texture(
            {.width = 1, .height = 1, .pixels = {0, 255, 255, 255}});
        ASSERT_TRUE(dark);
        const auto mesh = lit_quad();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 4> outputs;
        Format format{};
        for(unsigned index = 0; index < outputs.size(); ++index) {
            auto material = std::make_shared<Material>("occlusion", "pbr");
            ASSERT_TRUE(material->set_scalar_property("roughness", 1));
            if(index != 0)
                material->set_texture_property("occlusion_texture", dark.value());
            if(index == 2)
                ASSERT_TRUE(material->set_scalar_property("occlusion_strength", 0));
            if(index == 3)
                ASSERT_TRUE(material->set_vector_property("emissive_color", {0.2f, 0.4f, 0.6f, 1}));
            RenderWorld world{
                .render_items = {{.mesh = mesh, .material = {AssetHandle{9600 + index}, material}}},
                .environment = {{}, false, 0, 0, true, 0.25f},
                .environment_resource = environment.value()};
            RenderSubmission submission{
                world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                           Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            const auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            outputs[index] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 9 * 9 * 4);
            const auto view =
                scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
            format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), outputs[index], size);
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const auto baseline = outputs[0]->read(), occluded = outputs[1]->read();
        const auto disabled = outputs[2]->read(), emissive = outputs[3]->read();
        for(unsigned channel = 0; channel < 3; ++channel) {
            const auto offset = (4 * 9 + 4) * 4 + (bgra ? 2 - channel : channel);
            EXPECT_GT(std::to_integer<int>(baseline[offset]), 50);
            EXPECT_EQ(std::to_integer<int>(occluded[offset]), 0);
            EXPECT_EQ(disabled[offset], baseline[offset]);
            EXPECT_NEAR(
                std::to_integer<int>(emissive[offset]), mapped_byte(0.2f * (channel + 1)), 2);
        }
    }

    TEST_F(RenderGraphGpuTest, PbrInterpolatesBakedTangentFramesOnSmoothGeometry) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        const Math::Vec2u size{9, 9};
        auto scene = create_scene(programs, size);
        ASSERT_TRUE(scene);
        const auto normal = engine->get_render_resources().try_create_texture(
            {.width = 1, .height = 1, .pixels = {204, 179, 230, 255}});
        ASSERT_TRUE(normal);
        auto material = std::make_shared<Material>("smooth tangent frame", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {.6f, .4f, .2f, 1}));
        ASSERT_TRUE(material->set_scalar_property("metallic", .8f));
        ASSERT_TRUE(material->set_scalar_property("roughness", .8f));
        material->set_texture_property("normal_texture", normal.value());
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        for(const float handedness : {1.f, -1.f}) {
            for(const bool instanced : {false, true}) {
                SCOPED_TRACE(handedness);
                SCOPED_TRACE(instanced);
                MeshData data{
                    .vertices = {{{-1, -1, .5f}, {}, {.6f, 0, .8f}, {.8f, 0, -.6f, handedness}},
                        {{1, -1, .5f}, {}, {0, .6f, .8f}, {1, 0, 0, handedness}},
                        {{0, 1, .5f}, {}, {0, 0, 1},
                            {std::sqrt(.5f), std::sqrt(.5f), 0, handedness}}},
                    .indices = {0, 1, 2}};
                const auto mesh = engine->get_render_resources().try_create_mesh(data);
                ASSERT_TRUE(mesh);
                RenderWorld world{.render_items = {{.mesh = mesh.value(),
                                      .material = {AssetHandle(9700), material}}},
                    .lights = {{.direction = {-.6f, 0, -.8f}, .intensity = Math::PI}}};
                if(instanced) {
                    auto second = world.render_items.front();
                    second.model_matrix = Math::translate(Math::Mat4(1), {0, 0, -.1f});
                    world.render_items.push_back(second);
                }
                RenderSubmission submission{
                    world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                               Math::ortho(-1, 1, -1, 1, .1f, 10)}};
                RenderGeometry geometry;
                geometry.prepare(world);
                begin_frame(frames);
                const auto drawn = scene.value()->render(frames, submission, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                EXPECT_EQ(scene.value()->get_material_statistics().instanced_draw_calls,
                    instanced ? 1u : 0u);
                const auto view =
                    scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
                const auto format = view->get_image()->get_info().format;
                auto output = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), 9 * 9 * 4);
                finish_readback(*scene.value(), frames, output, size, drawn.value());
                // 屏幕中心的重心权重为 1/4、1/4、1/2，直接插值三个完整顶点切线基。
                Math::Vec3d n(0), t(0), b(0);
                for(size_t vertex = 0; vertex < 3; ++vertex) {
                    const double weight = vertex == 2 ? .5 : .25;
                    const Math::Vec3d normal(data.vertices[vertex].normal),
                        tangent(data.vertices[vertex].tangent);
                    n += weight * normal;
                    t += weight * tangent;
                    b += weight * double(handedness) * Math::cross(normal, tangent);
                }
                const auto local = Math::Vec3d(204, 179, 230) * (2.0 / 255.0) - 1.0;
                const auto mapped = Math::normalize(t * local.x + b * local.y + n * local.z);
                const auto expected = pbr_reference(
                    mapped, {0, 0, 1}, {.6, 0, .8}, {.6, .4, .2}, .8, .8, std::numbers::pi);
                const auto bytes = output->read();
                const bool bgra =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
                for(unsigned channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(
                                    bytes[(4 * 9 + 4) * 4 + (bgra ? 2 - channel : channel)]),
                        mapped_byte(float(expected[channel])), 2);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, PbrMapsUseGltfChannelsAndRespectNormalCoordinates) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        const Math::Vec2u size{9, 9};
        auto scene = create_scene(programs, size);
        ASSERT_TRUE(scene) << scene.error();
        const auto texture = [&](std::array<uint8_t, 4> pixel) {
            return engine->get_render_resources().try_create_texture(
                {.width = 1, .height = 1, .pixels = {pixel.begin(), pixel.end()}});
        };
        const auto metal_roughness = texture({0, 128, 192, 255});
        const auto occlusion = texture({0, 255, 255, 255});
        const auto normal = texture({204, 179, 230, 255});
        const auto emission = texture({64, 128, 192, 255});
        ASSERT_TRUE(metal_roughness);
        ASSERT_TRUE(occlusion);
        ASSERT_TRUE(normal);
        ASSERT_TRUE(emission);
        struct Scenario {
            bool packed = false;
            bool ao = false;
            bool mapped = false;
            int uv_mode = 0; // 普通、镜像、退化、旋转。
            float normal_scale = 1;
            bool emissive = false;
            bool light = true;
            int transform = 0;
            int authored_tangent = 0;
            bool instanced = false;
            bool imported = false;
        };
        const std::array cases{Scenario{}, Scenario{.transform = 1}, Scenario{.transform = 2},
            Scenario{.packed = true}, Scenario{.ao = true}, Scenario{.mapped = true},
            Scenario{.mapped = true, .uv_mode = 1}, Scenario{.mapped = true, .uv_mode = 2},
            Scenario{.mapped = true, .uv_mode = 3}, Scenario{.mapped = true, .normal_scale = 0},
            Scenario{.emissive = true, .light = false}, Scenario{.light = false},
            Scenario{.mapped = true, .authored_tangent = 1},
            Scenario{.mapped = true, .authored_tangent = 2},
            Scenario{.mapped = true, .transform = 1, .authored_tangent = 1},
            Scenario{.mapped = true, .transform = 3, .authored_tangent = 1},
            Scenario{.mapped = true, .authored_tangent = 1, .instanced = true},
            Scenario{.mapped = true, .imported = true}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::vector<std::shared_ptr<Readback>> outputs;
        std::vector<Math::Vec3d> expected;
        Format format{};
        for(std::size_t index = 0; index < cases.size(); ++index) {
            const auto sample = cases[index];
            MeshData data{.vertices = {{{-1, -1, 0.5f}, {0, 0}, {0, 0, 1}},
                              {{1, -1, 0.5f}, {1, 0}, {0, 0, 1}}, {{1, 1, 0.5f}, {1, 1}, {0, 0, 1}},
                              {{-1, 1, 0.5f}, {0, 1}, {0, 0, 1}}},
                .indices = {0, 1, 2, 2, 3, 0}};
            if(sample.imported) {
                const auto imported =
                    MeshImporter{}.import(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                          / "assets/meshes/surface_quad.gltf");
                ASSERT_TRUE(imported) << imported.error();
                data = imported.value();
                for(const auto& vertex : data.vertices)
                    ASSERT_LT(Math::length(vertex.tangent - Math::Vec4(1, 0, 0, 1)), 1e-5f);
            }
            for(auto& vertex : data.vertices) {
                if(sample.authored_tangent)
                    vertex.tangent = {0, 1, 0, sample.authored_tangent == 2 ? -1.f : 1.f};
                if(sample.uv_mode == 1)
                    vertex.texcoord.x = 1 - vertex.texcoord.x;
                if(sample.uv_mode == 2)
                    vertex.texcoord = {0, 0};
                if(sample.uv_mode == 3)
                    vertex.texcoord = {vertex.texcoord.y, 1 - vertex.texcoord.x};
            }
            auto mesh = engine->get_render_resources().try_create_mesh(data);
            ASSERT_TRUE(mesh);
            auto material = std::make_shared<Material>("surface", "pbr");
            ASSERT_TRUE(material->set_vector_property("base_color", {0.6f, 0.4f, 0.2f, 1}));
            ASSERT_TRUE(material->set_scalar_property("metallic", 0.8f));
            ASSERT_TRUE(material->set_scalar_property("roughness", 0.8f));
            ASSERT_TRUE(material->set_scalar_property("normal_scale", sample.normal_scale));
            if(sample.packed)
                material->set_texture_property(
                    "metallic_roughness_texture", metal_roughness.value());
            if(sample.ao)
                material->set_texture_property("occlusion_texture", occlusion.value());
            if(sample.mapped)
                material->set_texture_property("normal_texture", normal.value());
            if(sample.emissive) {
                material->set_texture_property("emissive_texture", emission.value());
                ASSERT_TRUE(material->set_vector_property("emissive_color", {1, 0.5f, 0.25f, 1}));
                ASSERT_TRUE(material->set_scalar_property("emissive_strength", 2));
            }
            auto model = Math::Mat4(1);
            if(sample.transform == 1)
                model = Math::scale(model, {-1, 1, 1});
            if(sample.transform == 2) {
                model = Math::rotate(model, Math::PI, Math::Vec3(0, 1, 0));
                ASSERT_TRUE(material->set_surface({.double_sided = true}));
            }
            if(sample.transform == 3)
                model = Math::scale(
                    Math::rotate(model, Math::PI / 4, Math::Vec3(0, 0, 1)), {1.5f, .75f, 1});
            RenderWorld world{.render_items = {{.model_matrix = model,
                                  .mesh = mesh.value(),
                                  .material = {AssetHandle{9500 + index}, material}}}};
            if(sample.instanced) {
                auto second = world.render_items.front();
                second.model_matrix = Math::translate(model, {0, 0, -.1f});
                world.render_items.push_back(second);
            }
            if(sample.light)
                world.lights.push_back({.direction = {-0.6f, 0, -0.8f}, .intensity = Math::PI});
            RenderSubmission submission{
                world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                           Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            if(sample.instanced)
                EXPECT_EQ(scene.value()->get_material_statistics().instanced_draw_calls, 1u);
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 9 * 9 * 4);
            const auto view =
                scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
            format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), output, size);
            submit(device, frames, drawn.value());
            outputs.push_back(output);
            Math::Vec3d n{0, 0, 1};
            if(sample.mapped && sample.uv_mode != 2 && sample.normal_scale != 0) {
                n = Math::normalize(Math::Vec3d(204, 179, 230) * (2.0 / 255.0) - 1.0);
                if(sample.authored_tangent) {
                    n = {sample.authored_tangent == 2 ? n.y : -n.y, n.x, n.z};
                    if(sample.transform == 1)
                        n.x = -n.x;
                    if(sample.transform == 3) {
                        const double angle = std::numbers::pi / 4;
                        n = {std::cos(angle) * n.x - std::sin(angle) * n.y,
                            std::sin(angle) * n.x + std::cos(angle) * n.y, n.z};
                    }
                }
                if(sample.uv_mode == 1)
                    n.x = -n.x;
                if(sample.uv_mode == 3)
                    n = {-n.y, n.x, n.z};
            }
            Math::Vec3d color{0};
            if(sample.light)
                color = pbr_reference(n, {0, 0, 1}, {0.6, 0, 0.8}, {0.6, 0.4, 0.2},
                    sample.packed ? 0.8 * 192 / 255 : 0.8, sample.packed ? 0.8 * 128 / 255 : 0.8,
                    std::numbers::pi);
            if(sample.emissive)
                color += Math::Vec3d(64, 64, 48) * (2.0 / 255.0);
            expected.push_back(color);
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(std::size_t index = 0; index < outputs.size(); ++index) {
            SCOPED_TRACE(index);
            const auto bytes = outputs[index]->read();
            for(unsigned channel = 0; channel < 3; ++channel) {
                const auto offset = (4 * 9 + 4) * 4 + (bgra ? 2 - channel : channel);
                EXPECT_NEAR(std::to_integer<int>(bytes[offset]),
                    mapped_byte(float(expected[index][channel])), 3);
            }
        }
    }
}
