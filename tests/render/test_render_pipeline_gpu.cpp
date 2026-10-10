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
    TEST_F(RenderGraphGpuTest, PbrParametersAndCameraProjectionMatchReferencePixels) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        auto material = std::make_shared<Material>("pbr", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {0.8f, 0.2f, 0.1f, 1}));
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        struct Scenario {
            const char* name;
            float metallic = 0;
            float roughness = 0.6f;
            Math::Vec3 camera{0, 0, 3};
            bool orthographic = false;
            LightType light_type = LightType::Directional;
            Math::Vec3 normal{0, 0, 1};
            Math::Vec3 light_direction{0.5f, 0, -1};
            float intensity = 4;
            bool has_light = true;
        };
        for(const auto& sample :
            {Scenario{.name = "dielectric"}, Scenario{.name = "metal", .metallic = 1},
                Scenario{.name = "mixed smooth", .metallic = 0.5f, .roughness = 0.2f},
                Scenario{.name = "lower parameter bounds", .metallic = -1, .roughness = -1},
                Scenario{.name = "upper parameter bounds", .metallic = 2, .roughness = 2},
                Scenario{.name = "translated camera", .camera = {1, 0, 3}},
                Scenario{.name = "orthographic", .orthographic = true},
                Scenario{.name = "point light",
                    .light_type = LightType::Point,
                    .intensity = 4 * Math::PI},
                Scenario{.name = "spot light",
                    .light_type = LightType::Spot,
                    .light_direction = {0, 0, -1},
                    .intensity = 4 * Math::PI},
                Scenario{.name = "zero normal", .normal = {}},
                Scenario{.name = "back-facing normal", .normal = {0, 0, -1}},
                Scenario{.name = "intense smooth metal",
                    .metallic = 1,
                    .roughness = 0.045f,
                    .light_direction = {0, 0, -1},
                    .intensity = 10000},
                Scenario{.name = "no lights", .has_light = false}}) {
            SCOPED_TRACE(sample.name);
            ASSERT_TRUE(material->set_scalar_property("metallic", sample.metallic));
            ASSERT_TRUE(material->set_scalar_property("roughness", sample.roughness));
            auto projection = Math::perspective(60, 1, 0.1f, 10);
            if(sample.orthographic)
                projection = Math::ortho(-1, 1, -1, 1, 0.1f, 10);
            RenderLight light{.type = sample.light_type,
                .position = {0, 0, 2.5f},
                .direction = sample.light_direction,
                .intensity = sample.intensity};
            RenderSubmission submission{
                .view_project_matrix =
                    ViewProjectMatrix{
                        Math::look_at(sample.camera, {0, 0, 0.5f}, {0, 1, 0}), projection},
                .render_items = {{.mesh = lit_quad(sample.normal),
                    .material = {AssetHandle(781), material}}},
                .lights = {light}};
            if(!sample.has_light)
                submission.lights.clear();
            begin_frame(frames);
            const auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            ASSERT_NO_FATAL_FAILURE(
                finish_readback(scene, frames, output, {33, 33}, drawn.value()));
            auto direction = -glm::dvec3(light.direction);
            double radiance = light.intensity;
            if(light.type != LightType::Directional) {
                direction = {0, 0, 1};
                radiance *= std::pow(1 - std::pow(2.0 / light.range, 4), 2) / 4;
            }
            if(!sample.has_light)
                radiance = 0;
            const auto expected = pbr_reference(glm::dvec3(sample.normal),
                glm::dvec3(sample.camera) - glm::dvec3(0, 0, 0.5), direction, {0.8, 0.2, 0.1},
                sample.metallic, sample.roughness, radiance);
            const auto bytes = output->read();
            const auto format = view->get_image()->get_info().format;
            const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
            for(unsigned x : {4u, 16u, 28u}) {
                if(!sample.orthographic && x != 16)
                    continue;
                for(unsigned channel = 0; channel < 3; ++channel) {
                    const auto component = bgra ? 2 - channel : channel;
                    EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + x) * 4 + component]),
                        mapped_byte(static_cast<float>(expected[channel])), 3);
                }
            }
        }
    }

    TEST_F(RenderGraphGpuTest, MaterialEditCandidatesPublishAtomicallyAndKeepOldFramePixels) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(prepare_offscreen_host({2, 2}));
        auto& scene = renderer.get_scene_renderer();
        const AssetHandle handle(786);
        auto material = std::make_shared<Material>("authored", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {0, 1, 0, 1}));
        auto pbr = std::make_shared<Material>("authored", "pbr");
        auto& registry = engine->get_asset_registry();
        const AssetHandle mesh_handle(787);
        ASSERT_TRUE(registry.register_asset(mesh_handle, lit_quad()));
        ASSERT_TRUE(registry.register_asset(handle, material));
        RenderScene submission{.cameras = {{.primary = true,
                                   .view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                                   .projection = RenderCamera::Projection::Orthographic,
                                   .orthographic_height = 2,
                                   .near_clip = 0.1f,
                                   .far_clip = 10}},
            .render_items = {{.mesh_handle = mesh_handle, .material_handle = handle}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 4> outputs;
        Format format{};
        for(size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1) {
                // 丢弃有效 GPU 候选，模拟后续保存文件失败而未发布。
                auto abandoned = renderer.prepare_material_update(handle, pbr);
                ASSERT_TRUE(abandoned) << abandoned.error();
                EXPECT_FALSE(renderer.prepare_material_update(
                    handle, std::make_shared<Material>("invalid", "missing_template")));
            } else if(index == 2) {
                auto update = renderer.prepare_material_update(handle, pbr);
                ASSERT_TRUE(update) << update.error();
                std::move(update).value().publish();
                ASSERT_TRUE(registry.replace_asset(handle, pbr));
            } else if(index == 3) {
                auto red = std::make_shared<Material>("authored", "unlit_color");
                ASSERT_TRUE(red->set_vector_property("color", {1, 0, 0, 1}));
                auto update = renderer.prepare_material_update(handle, red);
                ASSERT_TRUE(update) << update.error();
                std::move(update).value().publish();
                ASSERT_TRUE(registry.replace_asset(handle, std::move(red)));
            }
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 16);
            render_and_copy(renderer, submission, frames, outputs[index]);
            EXPECT_EQ(scene.get_material_statistics().draw_calls, 1);
            if(index > 0)
                EXPECT_EQ(scene.get_material_statistics().material_bindings_created, 0);
            format = scene.get_offscreen_color_view(0)->get_image()->get_info().format;
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const std::array<Math::Vec3, 4> expected{
            Math::Vec3(0, 1, 0), Math::Vec3(0, 1, 0), Math::Vec3(0), Math::Vec3(1, 0, 0)};
        for(size_t index = 0; index < outputs.size(); ++index) {
            const auto bytes = outputs[index]->read();
            for(size_t pixel = 0; pixel < 4; ++pixel)
                for(unsigned channel = 0; channel < 3; ++channel) {
                    const auto component = bgra ? 2 - channel : channel;
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + component]),
                        mapped_byte(expected[index][channel]), 2)
                        << index;
                }
        }
    }

    TEST_F(RenderGraphGpuTest, PbrTextureUsesUvColorSpaceAndFallsBackAfterClear) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = create_scene(programs, {2, 2});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        auto material = std::make_shared<Material>("pbr", "pbr");
        const Math::Vec3 tint{0.8f, 0.4f, 0.2f};
        ASSERT_TRUE(material->set_vector_property("base_color", Math::Vec4(tint, 1)));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = lit_quad(), .material = {AssetHandle(785), material}}},
            .lights = {{.intensity = Math::PI}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        const std::vector<uint8_t> pixels{
            128, 64, 32, 0, 255, 0, 128, 255, 0, 255, 64, 255, 255, 255, 255, 255};
        std::array<std::shared_ptr<Readback>, 4> outputs;
        Format output_format{};
        for(size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1 || index == 2) {
                auto format = Format::R8G8B8A8_SRGB;
                if(index == 2)
                    format = Format::R8G8B8A8_UNORM;
                auto texture = engine->get_render_resources().try_create_texture(
                    {.width = 2, .height = 2, .format = format, .pixels = pixels});
                ASSERT_TRUE(texture) << texture.error();
                material->set_texture_property("base_color_texture", std::move(texture).value());
            } else {
                material->set_texture_property("base_color_texture", nullptr);
            }
            begin_frame(frames);
            const auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.get_material_statistics().draw_calls, 1);
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 16);
            const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            output_format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), outputs[index], {2, 2});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra =
            output_format == Format::B8G8R8A8_SRGB || output_format == Format::B8G8R8A8_UNORM;
        for(size_t index = 0; index < outputs.size(); ++index) {
            SCOPED_TRACE(index);
            const auto bytes = outputs[index]->read();
            for(size_t pixel = 0; pixel < 4; ++pixel) {
                // 场景使用负高度 viewport，读回行序与此测试网格的 V 方向相反。
                const size_t texel_index = (1 - pixel / 2) * 2 + pixel % 2;
                for(unsigned channel = 0; channel < 3; ++channel) {
                    float texel = 1;
                    if(index == 1 || index == 2)
                        texel = pixels[texel_index * 4 + channel] / 255.0f;
                    if(index == 1) {
                        if(texel <= 0.04045f)
                            texel /= 12.92f;
                        else
                            texel = std::pow((texel + 0.055f) / 1.055f, 2.4f);
                    }
                    const auto component = bgra ? 2 - channel : channel;
                    // 正面、roughness=1、非金属、辐照度 PI：漫反射 0.96*base，镜面 0.01。
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + component]),
                        mapped_byte(0.96f * tint[channel] * texel + 0.01f), 3);
                }
                EXPECT_EQ(std::to_integer<int>(bytes[pixel * 4 + 3]), 255);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, PbrReloadRetainsOldFramesAndSurvivesTargetRebuild) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(prepare_offscreen_host({4, 4}));
        const auto directory = std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/material";
        auto source = read_text_file(directory / "pbr.frag");
        ASSERT_TRUE(source) << source.error();
        const std::string color = "clamp(result, 0.0, 65504.0)";
        const auto offset = source.value().find(color);
        ASSERT_NE(offset, std::string::npos);
        source.value().replace(offset, color.size(), "clamp(result * 0.5, 0.0, 65504.0)");
        TemporaryDirectory temporary;
        const auto path = temporary.path() / "pbr.frag";
        ASSERT_TRUE(write_text_file_atomic(path, source.value()));
        const auto compiled = ShaderCompiler::compile(
            {.source = path, .stage = ShaderStage::Fragment, .include_directories = {directory}});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        const MaterialShaders candidate{
            {"pbr", {{PBR_VERT.begin(), PBR_VERT.end()}, compiled.words}}};
        auto material = std::make_shared<Material>("pbr", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {0.5f, 0.5f, 0.5f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        auto& registry = engine->get_asset_registry();
        ASSERT_TRUE(registry.register_asset(AssetHandle(783), lit_quad()));
        ASSERT_TRUE(registry.register_asset(AssetHandle(784), material));
        RenderScene submission{.cameras = {{.primary = true,
                                   .view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                                   .projection = RenderCamera::Projection::Orthographic,
                                   .orthographic_height = 2,
                                   .near_clip = 0.1f,
                                   .far_clip = 10}},
            .render_items = {{.mesh_handle = AssetHandle(783),
                .material_handle = AssetHandle(784)}},
            .lights = {{.intensity = Math::PI}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 3> outputs;
        for(size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1) {
                const auto published = renderer.reload_material_shaders(candidate);
                ASSERT_TRUE(published) << published.error();
                EXPECT_EQ(published.value().pipelines, 1);
                EXPECT_EQ(published.value().material_bindings, 0);
                auto broken = candidate;
                broken.at("pbr").fragment.clear();
                EXPECT_FALSE(renderer.reload_material_shaders(broken));
                broken.at("pbr").fragment = {0};
                EXPECT_FALSE(renderer.reload_material_shaders(broken));
                auto header = read_text_file(directory.parent_path() / "lighting/forward.glsl");
                ASSERT_TRUE(header) << header.error();
                const auto binding = header.value().find("binding = 1");
                ASSERT_NE(binding, std::string::npos);
                header.value().replace(
                    binding, std::string_view("binding = 1").size(), "binding = 7");
                auto incompatible_source = source.value();
                const std::string include = "#include \"../lighting/forward.glsl\"";
                const auto offset = incompatible_source.find(include);
                ASSERT_NE(offset, std::string::npos);
                incompatible_source.replace(offset, include.size(), header.value());
                const auto incompatible_path = temporary.path() / "incompatible.frag";
                ASSERT_TRUE(write_text_file_atomic(incompatible_path, incompatible_source));
                const auto incompatible = ShaderCompiler::compile({.source = incompatible_path,
                    .stage = ShaderStage::Fragment,
                    .include_directories = {directory}});
                ASSERT_TRUE(incompatible.succeeded()) << incompatible.diagnostics;
                broken.at("pbr").fragment = incompatible.words;
                EXPECT_FALSE(renderer.reload_material_shaders(broken));
            }
            if(index == 2) {
                // 更新其他程序及重建目标不能恢复旧的 PBR 字节码。
                const auto builtin = default_material_shaders();
                ASSERT_TRUE(
                    renderer.reload_material_shaders({{"unlit_color", builtin.at("unlit_color")}}));
                ASSERT_TRUE(renderer.enable_offscreen_rendering({4, 4}));
            }

            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 64);
            render_and_copy(renderer, submission, frames, outputs[index]);
        }
        frames.wait_for_all_slots();
        for(size_t index = 0; index < outputs.size(); ++index) {
            const auto bytes = outputs[index]->read();
            const auto expected = mapped_byte(index == 0 ? 0.49f : 0.245f);
            for(size_t pixel = 0; pixel < 16; ++pixel)
                for(size_t channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + channel]), expected, 2);
        }
    }

    TEST_F(
        RenderGraphGpuTest, DirectionalShadowsFollowLightAndMeshChangesWithoutMaterialRebinding) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        const auto mesh = lit_quad();
        auto material = std::make_shared<Material>("pbr", "pbr");
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = mesh, .material = {AssetHandle(780), material}},
                {.mesh = mesh, .material = {AssetHandle(780), material}}},
            .lights = {{.direction = {1, 0, -1}, .intensity = 4}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        struct Sample {
            bool casts_shadow;
            float direction_x, occluder_x;
            bool center_shadowed;
            uint64_t upload_bytes;
        };
        const std::array samples{Sample{false, 1, -0.5f, false, 0},
            Sample{true, 1, -0.5f, true, 128}, Sample{true, 1, -0.5f, true, 128},
            Sample{true, -1, -0.5f, false, 128}, Sample{true, 1, -0.5f, true, 0},
            Sample{true, -1, 0.5f, true, 128}, Sample{true, 1, 0.5f, false, 128},
            Sample{true, -1, 0.5f, true, 0}};
        std::array<std::vector<std::byte>, samples.size()> pixels;
        for(size_t index = 0; index < samples.size(); ++index) {
            SCOPED_TRACE(index);
            const auto& sample = samples[index];
            submission.lights[0].casts_shadow = sample.casts_shadow;
            submission.lights[0].direction.x = sample.direction_x;
            submission.render_items[1].model_matrix =
                Math::scale(Math::translate(Math::Mat4(1), {sample.occluder_x, 0, 0.875f}),
                    {0.25f, 0.25f, 0.25f});
            begin_frame(frames);
            const auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(
                scene.get_material_statistics().material_bindings_created, index == 0 ? 1 : 0);
            EXPECT_EQ(scene.get_material_statistics().draw_calls, 1u);
            EXPECT_EQ(scene.get_material_statistics().drawn_instances, 2u);
            EXPECT_EQ(scene.get_shadow_statistics().draw_calls, sample.casts_shadow ? 1u : 0u);
            EXPECT_EQ(scene.get_shadow_statistics().drawn_instances, sample.casts_shadow ? 2u : 0u);
            EXPECT_EQ(scene.get_shadow_statistics().instance_upload_bytes, sample.upload_bytes);
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            ASSERT_NO_FATAL_FAILURE(
                finish_readback(scene, frames, output, {33, 33}, drawn.value()));
            pixels[index] = output->read();
        }
        for(size_t index = 0; index < samples.size(); ++index) {
            SCOPED_TRACE(index);
            for(unsigned channel = 0; channel < 3; ++channel) {
                const unsigned center = (16 * 33 + 16) * 4 + channel;
                const unsigned lit = (4 * 33 + 16) * 4 + channel;
                EXPECT_GT(std::to_integer<int>(pixels[0][center]), 50);
                if(samples[index].center_shadowed)
                    EXPECT_LE(std::to_integer<int>(pixels[index][center]), 3);
                else
                    EXPECT_NEAR(std::to_integer<int>(pixels[0][center]),
                        std::to_integer<int>(pixels[index][center]), 2);
                EXPECT_NEAR(std::to_integer<int>(pixels[0][lit]),
                    std::to_integer<int>(pixels[index][lit]), 2);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, ForwardLightsProduceExpectedPixelsAndReuseMaterialBindings) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& renderer = engine->get_renderer();
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = create_scene(programs, {17, 17});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        auto mesh = lit_quad();
        auto tilted = lit_quad({1, 0, 1});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(tilted);
        auto material = std::make_shared<Material>("lit", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {0.5f, 0.25f, 0.125f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        auto readback = std::make_shared<Readback>(
            device, context.get_context().get_physical_device(), 17 * 17 * 4);
        enum class Scenario {
            Directional,
            Point,
            Spot,
            BackFacing,
            NonuniformScale,
            SingularScale,
            NarrowSpotCone,
            NoLights,
            OutOfRange
        };
        for(const auto& [scenario, name] : {std::pair{Scenario::Directional, "directional light"},
                std::pair{Scenario::Point, "point light"}, std::pair{Scenario::Spot, "spot light"},
                std::pair{Scenario::BackFacing, "back-facing light"},
                std::pair{Scenario::NonuniformScale, "nonuniform normal scale"},
                std::pair{Scenario::SingularScale, "singular model scale"},
                std::pair{Scenario::NarrowSpotCone, "narrow spot cone"},
                std::pair{Scenario::NoLights, "no lights"},
                std::pair{Scenario::OutOfRange, "point light out of range"}}) {
            SCOPED_TRACE(name);
            RenderLight light{.entity_id = 1,
                .position = {0, 0, 2.5f},
                .intensity = Math::PI,
                .range = 10,
                .inner_angle = 5,
                .outer_angle = 20};
            const bool spot = scenario == Scenario::Spot || scenario == Scenario::NarrowSpotCone;
            const bool attenuated = scenario == Scenario::Point || spot;
            if(attenuated) {
                light.type = spot ? LightType::Spot : LightType::Point;
                light.intensity = 4 * Math::PI;
            }
            if(scenario == Scenario::NarrowSpotCone) {
                light.inner_angle = 0;
                light.outer_angle = 0.0001f;
            }
            if(scenario == Scenario::BackFacing)
                light.direction = {0, 0, 1};
            if(scenario == Scenario::OutOfRange) {
                light.type = LightType::Point;
                light.range = 0.5f;
            }
            auto model = Math::Mat4(1);
            if(scenario == Scenario::NonuniformScale)
                model = Math::scale(model, {2, 1, 1});
            if(scenario == Scenario::SingularScale)
                model = Math::scale(model, {1, 1, 0});
            RenderSubmission submission{
                .view_project_matrix = ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)},
                .render_items = {{.model_matrix = model,
                    .mesh = scenario == Scenario::NonuniformScale ? tilted : mesh,
                    .material = {AssetHandle(555), material}}},
                .lights = {light}};
            if(scenario == Scenario::NoLights)
                submission.lights.clear();
            begin_frame(frames);
            const auto rendered = scene.render(frames, submission, {});
            ASSERT_TRUE(rendered) << rendered.error().message;
            const auto& statistics = scene.get_material_statistics();
            EXPECT_EQ(statistics.draw_calls, 1);
            EXPECT_EQ(statistics.light_count, scenario == Scenario::NoLights ? 0 : 1);
            if(scenario != Scenario::Directional)
                EXPECT_EQ(statistics.material_bindings_created, 0);
            auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            ASSERT_NO_FATAL_FAILURE(
                finish_readback(scene, frames, readback, {17, 17}, rendered.value()));
            const auto bytes = readback->read();
            const auto format = view->get_image()->get_info().format;
            const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
            for(unsigned y = 0; y < 17; ++y) {
                for(unsigned x = 0; x < 17; ++x) {
                    double radiance = light.intensity;
                    glm::dvec3 direction = -glm::dvec3(light.direction);
                    if(attenuated) {
                        const Math::Vec3 position{
                            (x + 0.5f) / 8.5f - 1, 1 - (y + 0.5f) / 8.5f, 0.5f};
                        const auto delta = light.position - position;
                        const float distance = Math::length(delta);
                        const float falloff =
                            std::max(1.0f - std::pow(distance / light.range, 4.0f), 0.0f);
                        radiance *= falloff * falloff / (distance * distance);
                        direction = glm::dvec3(delta / distance);
                        if(spot) {
                            const float inner = std::cos(Math::radians(light.inner_angle));
                            const float outer = std::cos(Math::radians(light.outer_angle));
                            if(inner - outer > 1e-6f) {
                                const float t = std::clamp(
                                    (delta.z / distance - outer) / (inner - outer), 0.0f, 1.0f);
                                radiance *= t * t * (3 - 2 * t);
                            } else if(delta.z / distance < outer) {
                                radiance = 0;
                            }
                        }
                    }
                    if(scenario == Scenario::BackFacing || scenario == Scenario::SingularScale
                        || scenario == Scenario::NoLights || scenario == Scenario::OutOfRange)
                        radiance = 0;
                    glm::dvec3 normal{0, 0, 1};
                    if(scenario == Scenario::NonuniformScale)
                        normal = {0.5, 0, 1};
                    const auto expected = pbr_reference(
                        normal, {0, 0, 1}, direction, {0.5, 0.25, 0.125}, 0, 1, radiance);
                    for(size_t channel = 0; channel < 3; ++channel) {
                        const auto component = bgra ? 2 - channel : channel;
                        EXPECT_NEAR(std::to_integer<int>(bytes[(y * 17 + x) * 4 + component]),
                            mapped_byte(static_cast<float>(expected[channel])), 3)
                            << "at " << x << ',' << y;
                    }
                }
            }
        }
    }
}
