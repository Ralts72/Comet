#include "support/engine_fixture.h"
#include "render/render_thread.h"
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
    TEST_F(RenderGraphGpuTest, CapturedFrameKeepsViewAndPickWhenMainChangesPendingInput) {
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(prepare_offscreen_host({32, 32}));
        const AssetHandle mesh(797), material(798);
        auto& assets = engine->get_asset_registry();
        ASSERT_TRUE(assets.register_asset(mesh, lit_quad()));
        ASSERT_TRUE(
            assets.register_asset(material, std::make_shared<Material>("pick", "unlit_color")));
        const RenderScene scene{.scene_lifetime = 100,
            .cameras = {{.primary = true,
                .view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0})}},
            .render_items = {
                {.entity_id = EntityId(1), .mesh_handle = mesh, .material_handle = material}}};
        const auto prepared = render_frontend->prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        renderer.request_viewport_pick({16, 16}, {32, 32}, 100);
        auto captured = renderer.capture_frame();
        captured.world = render_frontend->resolve(scene);
        const auto* world = captured.world.get();
        const auto* items = captured.world->render_items.data();
        ASSERT_TRUE(renderer.set_render_view({.visible = false}));
        renderer.cancel_viewport_pick();
        RenderThread recording(renderer);
        ASSERT_TRUE(recording.submit(std::move(captured)));
        const auto busy =
            recording.try_create_texture({.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        EXPECT_FALSE(busy);
        EXPECT_EQ(busy.result(), vk::Result::eNotReady);
        EXPECT_FALSE(recording.submit({}));
        const auto completed = recording.complete();
        ASSERT_TRUE(completed) << completed.error();
        const auto next = render_frontend->resolve(scene);
        EXPECT_EQ(next.get(), world);
        EXPECT_EQ(next->render_items.data(), items);
        const auto pick = renderer.take_viewport_pick_result();
        ASSERT_TRUE(pick);
        ASSERT_TRUE(pick->hit);
        EXPECT_EQ(pick->hit->entity_id, EntityId(1));
        EXPECT_EQ(pick->scene_lifetime, 100u);
        EXPECT_TRUE(recording.try_create_texture(
            {.width = 1, .height = 1, .pixels = {255, 255, 255, 255}}));
    }

    TEST_F(RenderGraphGpuTest, PickResultsDiscardStaleRequestsCancellationAndFailedFrames) {
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(prepare_offscreen_host({32, 32}));
        const AssetHandle mesh(795), material(796);
        auto& assets = engine->get_asset_registry();
        ASSERT_TRUE(assets.register_asset(mesh, lit_quad()));
        ASSERT_TRUE(
            assets.register_asset(material, std::make_shared<Material>("pick", "unlit_color")));
        RenderScene scene{.scene_lifetime = 100,
            .cameras = {{.primary = true,
                .view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0})}},
            .render_items = {
                {.entity_id = EntityId(1), .mesh_handle = mesh, .material_handle = material}}};
        const auto draw = [&](uint64_t lifetime, Math::Vec2u pixel, Math::Vec2u resolution,
                              bool visible = true, bool cancelled = false) {
            const auto prepared = render_frontend->prepare_frame();
            EXPECT_TRUE(prepared);
            if(!prepared || prepared.value() != Renderer::FramePreparation::Ready)
                return false;
            EXPECT_TRUE(renderer.set_render_view({.visible = visible}));
            renderer.request_viewport_pick(pixel, resolution, lifetime);
            if(cancelled)
                renderer.cancel_viewport_pick();
            EXPECT_FALSE(renderer.take_viewport_pick_result());
            return bool(render_frontend->render(scene));
        };
        ASSERT_TRUE(draw(100, {16, 16}, {32, 32}));
        const auto hit = renderer.take_viewport_pick_result();
        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->scene_lifetime, 100u);
        ASSERT_TRUE(hit->hit);
        EXPECT_EQ(hit->hit->entity_id, EntityId(1));
        EXPECT_FALSE(renderer.take_viewport_pick_result());
        ASSERT_TRUE(draw(100, {16, 16}, {32, 32}));
        renderer.cancel_viewport_pick();
        EXPECT_FALSE(renderer.take_viewport_pick_result());
        ASSERT_TRUE(draw(100, {16, 16}, {32, 32}, true, true));
        EXPECT_FALSE(renderer.take_viewport_pick_result());
        scene.scene_lifetime = 200;
        ASSERT_TRUE(draw(100, {16, 16}, {32, 32}));
        EXPECT_FALSE(renderer.take_viewport_pick_result());
        ASSERT_TRUE(draw(200, {16, 16}, {31, 32}));
        EXPECT_FALSE(renderer.take_viewport_pick_result());
        ASSERT_TRUE(draw(200, {16, 16}, {32, 32}, false));
        EXPECT_FALSE(renderer.take_viewport_pick_result());
        ASSERT_TRUE(draw(200, {0, 0}, {32, 32}));
        const auto miss = renderer.take_viewport_pick_result();
        ASSERT_TRUE(miss);
        EXPECT_EQ(miss->scene_lifetime, 200u);
        EXPECT_FALSE(miss->hit);
        Comet::Tests::set_test_overlay(renderer, {.render = [](OverlayRecordContext&) {
            return Result<void, GraphicsError>::failure({"Rejected overlay"});
        }});
        EXPECT_FALSE(draw(200, {16, 16}, {32, 32}));
        EXPECT_FALSE(renderer.take_viewport_pick_result());
        renderer.set_overlay({});
    }

    TEST_F(RenderGraphGpuTest, RendererViewsKeepOutputsAndPublicationIndependent) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(prepare_offscreen_host({33, 33}));
        auto created = renderer.create_offscreen_view({33, 17});
        ASSERT_TRUE(created) << created.error();
        auto second = created.value();
        auto& registry = engine->get_asset_registry();
        constexpr AssetHandle mesh(798), left_material(799), right_material(800);
        ASSERT_TRUE(registry.register_asset(mesh, lit_quad()));
        auto red = std::make_shared<Material>("left", "unlit_color");
        auto green = std::make_shared<Material>("right", "unlit_color");
        ASSERT_TRUE(red->set_vector_property("color", {1, 0, 0, 1}));
        ASSERT_TRUE(green->set_vector_property("color", {0, 1, 0, 1}));
        ASSERT_TRUE(registry.register_asset(left_material, red));
        ASSERT_TRUE(registry.register_asset(right_material, green));
        RenderCamera camera{.primary = true,
            .view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
            .projection = RenderCamera::Projection::Orthographic,
            .orthographic_height = 2,
            .near_clip = 0.1f,
            .far_clip = 10};
        auto other_camera = camera;
        other_camera.view_matrix = Math::look_at({6, 0, 3}, {6, 0, 0}, {0, 1, 0});
        RenderScene world{.cameras = {camera},
            .render_items = {
                {.entity_id = EntityId(1), .mesh_handle = mesh, .material_handle = left_material},
                {.entity_id = EntityId(2),
                    .model_matrix = Math::translate(Math::Mat4(1), {6, 0, 0}),
                    .mesh_handle = mesh,
                    .material_handle = right_material}}};
        world.lights = {{.casts_shadow = true}};
        unsigned left_picks = 0, right_picks = 0;
        TemporaryDirectory temporary;
        const auto shader_path = temporary.path() / "dim.frag";
        ASSERT_TRUE(write_text_file_atomic(shader_path,
            "#version 450\nlayout(location=0) out vec4 color;\n"
            "layout(set=1,binding=0,std140) uniform MaterialData {vec4 color; float intensity;} material;\n"
            "void main(){color=vec4(material.color.rgb*material.intensity*0.5,material.color.a);}"));
        auto compiled =
            ShaderCompiler::compile({.source = shader_path, .stage = ShaderStage::Fragment});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        auto shader = default_material_shaders().at("unlit_color");
        shader.fragment = compiled.words;
        FrameScheduler readback_frames(device, 2);
        readback_frames.initialize_swapchain_images(2);
        FrameWait wait{device, readback_frames};
        struct Sample {
            std::shared_ptr<Readback> pixels;
            Math::Vec2u size;
            Format format;
            Math::Vec3 color;
        };
        std::vector<Sample> samples;
        Math::Vec2u second_size{33, 17};
        for(unsigned frame = 0; frame < 6; ++frame) {
            SCOPED_TRACE(frame);
            if(frame == 1) {
                auto quality = renderer.get_quality_settings();
                quality.render_scale = 0.75f;
                ASSERT_TRUE(renderer.request_quality_settings(quality));
                second_size = {17, 23};
                auto blue = std::make_shared<Material>("left", "unlit_color");
                ASSERT_TRUE(blue->set_vector_property("color", {0, 0, 1, 1}));
                auto candidate =
                    renderer.prepare_material_update(left_material, blue->snapshot(), {});
                ASSERT_TRUE(candidate) << candidate.error();
                ASSERT_TRUE(std::move(candidate).value().publish());
                ASSERT_TRUE(registry.replace_asset(left_material, blue));
            }
            if(frame == 2) {
                auto published = renderer.reload_material_shaders({{"unlit_color", shader}});
                ASSERT_TRUE(published) << published.error();
                EXPECT_EQ(published.value().pipelines, 2u);
                auto invalid = shader;
                invalid.fragment = {0};
                EXPECT_FALSE(renderer.reload_material_shaders({{"unlit_color", invalid}}));
                ASSERT_TRUE(renderer.reload_material_shaders({{"unlit_color", shader}}));
            }
            if(frame == 5) {
                ASSERT_TRUE(renderer.remove_offscreen_view(second));
                EXPECT_FALSE(renderer.set_render_view({}, second));
                EXPECT_FALSE(renderer.remove_offscreen_view(Renderer::DEFAULT_VIEW));
                auto replacement = renderer.create_offscreen_view({11, 13});
                ASSERT_TRUE(replacement) << replacement.error();
                EXPECT_NE(replacement.value(), second);
                second = replacement.value();
                second_size = {11, 13};
            }
            const auto prepared = render_frontend->prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error();
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            EXPECT_FALSE(renderer.create_offscreen_view({8, 8}));
            EXPECT_FALSE(renderer.remove_offscreen_view(second));
            ASSERT_TRUE(renderer.set_render_view({.visible = frame != 3}));
            ASSERT_TRUE(renderer.set_render_view(
                {.render_size = second_size,
                    .camera_selection = RenderView::CameraSelection::Override,
                    .camera_override = other_camera},
                second));
            renderer.request_viewport_pick({16, 16}, {33, 33}, world.scene_lifetime);
            renderer.request_viewport_pick(
                second_size / 2u, second_size, world.scene_lifetime, second);
            const auto first_output = renderer.get_offscreen_frame();
            const auto second_output = renderer.get_offscreen_frame(second);
            EXPECT_NE(first_output.color_view, second_output.color_view);
            ASSERT_TRUE(render_frontend->render(world));
            const auto left = renderer.take_viewport_pick_result();
            EXPECT_EQ(left.has_value(), frame != 3);
            if(left) {
                ASSERT_TRUE(left->hit);
                EXPECT_EQ(left->hit->entity_id, EntityId(1));
                ++left_picks;
            }
            const auto right = renderer.take_viewport_pick_result(second);
            ASSERT_TRUE(right);
            ASSERT_TRUE(right->hit);
            EXPECT_EQ(right->hit->entity_id, EntityId(2));
            ++right_picks;
            EXPECT_FALSE(renderer.take_viewport_pick_result());
            EXPECT_FALSE(renderer.take_viewport_pick_result(second));
            const auto shadow = renderer.get_scene_renderer().get_shadow_statistics();
            EXPECT_EQ(shadow.draw_calls, shadow.cascades);
            EXPECT_EQ(shadow.cascades, frame == 3 ? 0u : 4u);
            EXPECT_EQ(shadow.drawn_instances, shadow.cascades);
            const auto other_shadow = renderer.get_scene_renderer(second).get_shadow_statistics();
            EXPECT_EQ(other_shadow.cascades, 4u);
            EXPECT_EQ(other_shadow.drawn_instances, 4u);
            if(frame == 3)
                EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 0u);
            else
                EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().culled_items, 1u);
            EXPECT_EQ(renderer.get_scene_renderer(second).get_material_statistics().draw_calls, 1u);
            EXPECT_FLOAT_EQ(renderer.get_scene_renderer(second).get_quality_settings().render_scale,
                frame == 0 ? 1.0f : 0.75f);
            begin_frame(readback_frames);
            const auto copy = [&](const Renderer::OffscreenFrame& output, Math::Vec3 color) {
                auto pixels = std::make_shared<Readback>(device,
                    context.get_context().get_physical_device(), output.size.x * output.size.y * 4);
                copy_output(readback_frames, output.color_view->get_image(), pixels, output.size);
                samples.push_back({pixels, output.size,
                    output.color_view->get_image()->get_info().format, color});
            };
            const float intensity = frame < 2 ? 1.0f : 0.5f;
            if(frame != 3)
                copy(first_output, frame == 0 ? Math::Vec3(1, 0, 0) : Math::Vec3(0, 0, intensity));
            copy(second_output, {0, intensity, 0});
            submit(device, readback_frames);
        }
        readback_frames.wait_for_all_slots();
        renderer.wait_idle();
        EXPECT_EQ(left_picks, 5u);
        EXPECT_EQ(right_picks, 6u);
        for(const auto& sample : samples) {
            const auto pixels = sample.pixels->read();
            const auto offset = (sample.size.y / 2 * sample.size.x + sample.size.x / 2) * 4;
            const bool bgra =
                sample.format == Format::B8G8R8A8_SRGB || sample.format == Format::B8G8R8A8_UNORM;
            for(unsigned channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(std::to_integer<int>(pixels[offset + (bgra ? 2 - channel : channel)]),
                    mapped_byte(sample.color[channel]), 3);
        }
    }

    TEST_F(RenderGraphGpuTest, PbrParametersAndCameraProjectionMatchReferencePixels) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
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
            RenderWorld world{.render_items = {{.mesh = lit_quad(sample.normal),
                                  .material = {AssetHandle(781), material}}},
                .lights = {light}};
            RenderSubmission submission{
                world, ViewProjectMatrix{
                           Math::look_at(sample.camera, {0, 0, 0.5f}, {0, 1, 0}), projection}};
            if(!sample.has_light)
                world.lights.clear();
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            const auto drawn = scene.render(frames, submission, geometry);
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
                auto abandoned = renderer.prepare_material_update(handle, pbr->snapshot(), {});
                ASSERT_TRUE(abandoned) << abandoned.error();
                EXPECT_FALSE(renderer.prepare_material_update(handle,
                    std::make_shared<Material>("invalid", "missing_template")->snapshot(), {}));
            } else if(index == 2) {
                auto update = renderer.prepare_material_update(handle, pbr->snapshot(), {});
                ASSERT_TRUE(update) << update.error();
                ASSERT_TRUE(std::move(update).value().publish());
                ASSERT_TRUE(registry.replace_asset(handle, pbr));
            } else if(index == 3) {
                auto red = std::make_shared<Material>("authored", "unlit_color");
                ASSERT_TRUE(red->set_vector_property("color", {1, 0, 0, 1}));
                auto update = renderer.prepare_material_update(handle, red->snapshot(), {});
                ASSERT_TRUE(update) << update.error();
                ASSERT_TRUE(std::move(update).value().publish());
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
        MaterialPrograms programs;
        auto scene_owner = create_scene(programs, {2, 2});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        auto material = std::make_shared<Material>("pbr", "pbr");
        const Math::Vec3 tint{0.8f, 0.4f, 0.2f};
        ASSERT_TRUE(material->set_vector_property("base_color", Math::Vec4(tint, 1)));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        RenderWorld world{
            .render_items = {{.mesh = lit_quad(), .material = {AssetHandle(785), material}}},
            .lights = {{.intensity = Math::PI}}};
        RenderSubmission submission{
            world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                       Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
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
            RenderGeometry geometry;
            geometry.prepare(world);
            const auto drawn = scene.render(frames, submission, geometry);
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

    TEST_F(RenderGraphGpuTest, PbrReloadShowsErrorPreservesSubmittedFramesAndRecoversAfterRebuild) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(prepare_offscreen_host({4, 4}));
        const auto directory = std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/material";
        auto source = read_text_file(directory / "pbr.frag");
        ASSERT_TRUE(source) << source.error();
        const std::string color = "clamp(result + emission, 0.0, 65504.0)";
        const auto offset = source.value().find(color);
        ASSERT_NE(offset, std::string::npos);
        source.value().replace(
            offset, color.size(), "clamp((result + emission) * 0.5, 0.0, 65504.0)");
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
        std::array<std::shared_ptr<Readback>, 4> outputs;
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
                // 更新其他程序及重建目标不能隐藏 PBR 的错误状态。
                const auto builtin = default_material_shaders();
                ASSERT_TRUE(
                    renderer.reload_material_shaders({{"unlit_color", builtin.at("unlit_color")}}));
                ASSERT_TRUE(renderer.enable_offscreen_rendering({4, 4}));
            }
            if(index == 3)
                ASSERT_TRUE(renderer.reload_material_shaders(candidate));

            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 64);
            render_and_copy(renderer, submission, frames, outputs[index]);
        }
        frames.wait_for_all_slots();
        for(size_t index = 0; index < outputs.size(); ++index) {
            const auto bytes = outputs[index]->read();
            Math::Vec3 color(index == 0 ? 0.49f : 0.245f);
            if(index == 1 || index == 2)
                color = {1, 0, 1};
            for(size_t pixel = 0; pixel < 16; ++pixel)
                for(size_t channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + channel]),
                        mapped_byte(color[channel]), 2);
        }
    }

    TEST_F(
        RenderGraphGpuTest, DirectionalShadowsFollowLightAndMeshChangesWithoutMaterialRebinding) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene_owner = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        const auto mesh = lit_quad();
        auto material = std::make_shared<Material>("pbr", "pbr");
        RenderWorld world{.render_items = {{.mesh = mesh, .material = {AssetHandle(780), material}},
                              {.mesh = mesh, .material = {AssetHandle(780), material}}},
            .lights = {{.direction = {1, 0, -1}, .intensity = 4}}};
        RenderSubmission submission{
            world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                       Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
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
            Sample{true, 1, -0.5f, true, 320}, Sample{true, 1, -0.5f, true, 320},
            Sample{true, -1, -0.5f, false, 320}, Sample{true, 1, -0.5f, true, 0},
            Sample{true, -1, 0.5f, true, 320}, Sample{true, 1, 0.5f, false, 320},
            Sample{true, -1, 0.5f, true, 0}};
        std::array<std::vector<std::byte>, samples.size()> pixels;
        for(size_t index = 0; index < samples.size(); ++index) {
            SCOPED_TRACE(index);
            const auto& sample = samples[index];
            world.lights[0].casts_shadow = sample.casts_shadow;
            world.lights[0].direction.x = sample.direction_x;
            world.render_items[1].model_matrix =
                Math::scale(Math::translate(Math::Mat4(1), {sample.occluder_x, 0, 0.875f}),
                    {0.25f, 0.25f, 0.25f});
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            const auto drawn = scene.render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(
                scene.get_material_statistics().material_bindings_created, index == 0 ? 1 : 0);
            EXPECT_EQ(scene.get_material_statistics().draw_calls, 1u);
            EXPECT_EQ(scene.get_material_statistics().drawn_instances, 2u);
            EXPECT_EQ(scene.get_shadow_statistics().draw_calls, sample.casts_shadow ? 3u : 0u);
            EXPECT_EQ(scene.get_shadow_statistics().drawn_instances, sample.casts_shadow ? 5u : 0u);
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

    TEST_F(RenderGraphGpuTest, ShadowAtlasCoversEveryCascadeAndBlendsAtDistanceLimit) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto owner = create_scene(programs, {33, 33});
        ASSERT_TRUE(owner) << owner.error();
        auto& scene = *owner.value();
        const auto mesh = lit_quad();
        auto material = std::make_shared<Material>("pbr", "pbr");
        RenderWorld world{
            .render_items = {{.mesh = mesh, .material = {AssetHandle(781), material}},
                {.model_matrix = Math::scale(
                     Math::translate(Math::Mat4(1), {-0.5f, 0, 0.875f}), {0.25f, 0.25f, 0.25f}),
                    .mesh = mesh,
                    .material = {AssetHandle(781), material}}},
            .lights = {{.direction = {1, 0, -1},
                .intensity = 4,
                .casts_shadow = true,
                .shadow_distance = 8}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        // 正交相机每两米一级；覆盖各级内部、交界两侧和最后 10% 的淡出。
        const std::array depths{1.0f, 1.9f, 2.1f, 3.9f, 4.1f, 5.9f, 6.1f, 7.7f, 7.9f, 8.2f};
        std::array<int, depths.size()> centers{};
        for(size_t index = 0; index < depths.size(); ++index) {
            SCOPED_TRACE(depths[index]);
            const float eye = 0.6f + depths[index];
            RenderSubmission submission{
                world, ViewProjectMatrix{Math::look_at({0, 0, eye}, {0, 0, 0}, {0, 1, 0}),
                           Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            auto drawn = scene.render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.get_shadow_statistics().cascades, 4u);
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            ASSERT_NO_FATAL_FAILURE(
                finish_readback(scene, frames, output, {33, 33}, drawn.value()));
            const auto pixels = output->read();
            centers[index] = std::to_integer<int>(pixels[(16 * 33 + 16) * 4]);
            const auto lit = std::to_integer<int>(pixels[(4 * 33 + 16) * 4]);
            EXPECT_GT(lit, 50);
            if(index < 8)
                EXPECT_LE(centers[index], 3);
            if(index == depths.size() - 1)
                EXPECT_NEAR(centers[index], lit, 2);
        }
        EXPECT_GT(centers[8], centers[7] + 10);
        EXPECT_LT(centers[8], centers[9] - 10);
    }

    TEST_F(RenderGraphGpuTest, SpotShadowAtlasKeepsLightsMasksAndInFlightFramesIndependent) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        const auto mesh = lit_quad();
        auto receiver = std::make_shared<Material>("receiver", "pbr");
        auto masked = std::make_shared<Material>("mask", "pbr");
        ASSERT_TRUE(masked->set_surface({.alpha_mode = AlphaMode::Mask}));
        RenderWorld world{
            .render_items = {{.mesh = mesh, .material = {AssetHandle(782), receiver}}},
            .lights = {{.entity_id = 1, .intensity = .1f, .casts_shadow = true}}};
        const std::array<unsigned, 4> columns{6, 13, 19, 26};
        for(unsigned index = 0; index < columns.size(); ++index) {
            const float x = 2 * (columns[index] + .5f) / 33 - 1;
            world.render_items.push_back({.entity_id = index + 1,
                .model_matrix =
                    Math::scale(Math::translate(Math::Mat4(1), {x, 0, .95f}), {.1f, .1f, .1f}),
                .mesh = mesh,
                .material = {AssetHandle(index == 2 ? 783 : 782), index == 2 ? masked : receiver}});
            world.lights.push_back({.entity_id = 10 + index,
                .type = LightType::Spot,
                .position = {x, 0, 2.5f},
                .intensity = 20,
                .range = 5,
                .inner_angle = 7,
                .outer_angle = 8,
                .casts_shadow = true});
        }
        // 投射者在主相机近裁剪面之外，只有阴影影响接收面。
        RenderSubmission submission{
            world, ViewProjectMatrix{Math::look_at({0, 0, .8f}, {0, 0, 0}, {0, 1, 0}),
                       Math::ortho(-1, 1, -1, 1, .1f, 1)}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 4> outputs;
        for(unsigned frame = 0; frame < outputs.size(); ++frame) {
            SCOPED_TRACE(frame);
            world.lights[1].casts_shadow = frame != 1;
            ASSERT_TRUE(
                masked->set_vector_property("base_color", {1, 1, 1, frame == 2 ? 0.0f : 1.0f}));
            if(frame == 3)
                world.render_items[1].model_matrix[3].x += .25f;
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            const auto stats = scene.value()->get_shadow_statistics();
            EXPECT_EQ(stats.cascades, 4u);
            EXPECT_EQ(stats.spot_lights, frame == 1 ? 3u : 4u);
            EXPECT_EQ(stats.excess_spot_lights, 0u);
            outputs[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            copy_output(frames,
                scene.value()
                    ->get_offscreen_color_view(frames.get_current_frame_slot_index())
                    ->get_image(),
                outputs[frame], {33, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        for(unsigned frame = 0; frame < outputs.size(); ++frame) {
            SCOPED_TRACE(frame);
            const auto pixels = outputs[frame]->read();
            for(unsigned index = 0; index < columns.size(); ++index) {
                SCOPED_TRACE(index);
                const auto value = std::to_integer<int>(pixels[(16 * 33 + columns[index]) * 4]);
                const bool lit =
                    (index == 0 && (frame == 1 || frame == 3)) || (index == 2 && frame == 2);
                if(lit)
                    EXPECT_GT(value, 50);
                else
                    EXPECT_LE(value, 3);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, SpotShadowFollowsLightPoseRangeAndCone) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        const auto mesh = lit_quad();
        auto material = std::make_shared<Material>("pbr", "pbr");
        RenderWorld world{.render_items = {{.mesh = mesh, .material = {AssetHandle(784), material}},
                              {.mesh = mesh, .material = {AssetHandle(784), material}}},
            .lights = {{.type = LightType::Spot,
                .position = {-.5f, 0, 2},
                .direction = {.5f, 0, -1.5f},
                .intensity = 10,
                .range = 5,
                .inner_angle = 40,
                .outer_angle = 45,
                .casts_shadow = true}}};
        RenderSubmission submission{
            world, ViewProjectMatrix{Math::look_at({0, 0, .8f}, {0, 0, 0}, {0, 1, 0}),
                       Math::ortho(-1, 1, -1, 1, .1f, 1)}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        for(unsigned frame = 0; frame < 5; ++frame) {
            SCOPED_TRACE(frame);
            auto& light = world.lights.front();
            if(frame == 1) {
                light.position.x = .5f;
                light.direction.x = -.5f;
            }
            if(frame == 3)
                light.range = 1;
            if(frame == 4) {
                light.range = 5;
                light.inner_angle = 7;
                light.outer_angle = 8;
                light.direction = {1, 0, -1};
            }
            world.render_items[1].model_matrix =
                Math::scale(Math::translate(Math::Mat4(1), {frame < 2 ? -.2f : .2f, 0, 1.05f}),
                    {.1f, .1f, .1f});
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.value()->get_shadow_statistics().cascades, 0u);
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            ASSERT_NO_FATAL_FAILURE(
                finish_readback(*scene.value(), frames, output, {33, 33}, drawn.value()));
            const auto pixels = output->read();
            const auto center = std::to_integer<int>(pixels[(16 * 33 + 16) * 4]);
            if(frame == 1)
                EXPECT_GT(center, 50);
            else
                EXPECT_LE(center, 3);
        }
    }

    TEST_F(RenderGraphGpuTest, PointShadowsCoverFacesEdgesAndCornersWithoutLightLeaks) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene = create_scene(programs, {33, 33});
        ASSERT_TRUE(scene);
        const auto mesh = lit_quad();
        auto material = std::make_shared<Material>("pbr", "pbr");
        RenderWorld world{.render_items = {{.mesh = mesh, .material = {AssetHandle(784), material}},
                              {.mesh = mesh, .material = {AssetHandle(784), material}}},
            .lights = {
                {.type = LightType::Point, .intensity = 20, .range = 5, .casts_shadow = true}}};
        std::vector<Math::Vec3> directions;
        for(int x = -1; x <= 1; ++x)
            for(int y = -1; y <= 1; ++y)
                for(int z = -1; z <= 1; ++z)
                    if(x || y || z)
                        directions.emplace_back(x, y, z);
        // 接缝两侧，避免只验证完全相等的主轴选面。
        directions.insert(directions.end(), {{1, .998f, 0}, {1, 1.002f, 0}, {1, 1.002f, .998f}});
        RenderSubmission submission{world, ViewProjectMatrix{}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        for(const auto direction : directions) {
            SCOPED_TRACE(
                testing::Message() << direction.x << ',' << direction.y << ',' << direction.z);
            const auto axis = Math::normalize(direction);
            const auto up = std::abs(axis.y) > .95f ? Math::Vec3(1, 0, 0) : Math::Vec3(0, 1, 0);
            const auto basis = Math::inverse(Math::look_at(Math::Vec3(0), axis, up));
            // lit_quad 的局部平面在 z=0.5，先归到原点再摆放接收面／遮挡面。
            const auto centered = Math::translate(Math::Mat4(1), {0, 0, -.5f});
            world.render_items[0].model_matrix =
                Math::translate(Math::Mat4(1), axis * 2.0f) * basis * centered;
            world.render_items[1].model_matrix =
                Math::scale(Math::translate(Math::Mat4(1), axis * .8f) * basis, Math::Vec3(.25f))
                * centered;
            submission.view_project_matrix =
                ViewProjectMatrix{Math::look_at(axis * 1.6f, axis * 2.0f, up),
                    Math::ortho(-.5f, .5f, -.5f, .5f, .1f, 1)};
            for(bool casts_shadow : {false, true}) {
                world.lights[0].casts_shadow = casts_shadow;
                begin_frame(frames);
                RenderGeometry geometry;
                geometry.prepare(world);
                auto drawn = scene.value()->render(frames, submission, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                EXPECT_EQ(
                    scene.value()->get_shadow_statistics().point_lights, casts_shadow ? 1u : 0u);
                auto output = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), 33 * 33 * 4);
                ASSERT_NO_FATAL_FAILURE(
                    finish_readback(*scene.value(), frames, output, {33, 33}, drawn.value()));
                const auto pixels = output->read();
                // 三像素跨过面交界，不能只在某个面的中心取样。
                for(unsigned x = 15; x <= 17; ++x) {
                    const auto value = std::to_integer<int>(pixels[(16 * 33 + x) * 4]);
                    if(casts_shadow)
                        EXPECT_LE(value, 3);
                    else
                        EXPECT_GT(value, 80);
                }
            }
        }
    }

    TEST_F(RenderGraphGpuTest, PointShadowSlotsFollowMotionMaskAndDisableAcrossInFlightFrames) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        auto scene = create_scene(programs, {65, 33});
        ASSERT_TRUE(scene);
        const auto mesh = lit_quad();
        auto receiver = std::make_shared<Material>("receiver", "pbr");
        auto masked = std::make_shared<Material>("mask", "pbr");
        ASSERT_TRUE(masked->set_surface({.alpha_mode = AlphaMode::Mask}));
        RenderWorld world;
        const std::array<unsigned, 2> columns{16, 48};
        for(unsigned i = 0; i < columns.size(); ++i) {
            const float x = 8 * (columns[i] + .5f) / 65 - 4;
            world.render_items.push_back({.entity_id = i * 2 + 1,
                .model_matrix = Math::translate(Math::Mat4(1), {x, 0, 0}),
                .mesh = mesh,
                .material = {AssetHandle(782), receiver}});
            world.render_items.push_back({.entity_id = i * 2 + 2,
                .model_matrix =
                    Math::scale(Math::translate(Math::Mat4(1), {x, 0, .95f}), Math::Vec3(.15f)),
                .mesh = mesh,
                .material = {AssetHandle(783), masked}});
            world.lights.push_back({.entity_id = 10 + i,
                .type = LightType::Point,
                .position = {x, 0, 2},
                .intensity = 20,
                .range = 3,
                .casts_shadow = true});
        }
        RenderSubmission submission{
            world, ViewProjectMatrix{Math::look_at({0, 0, .8f}, {0, 0, 0}, {0, 1, 0}),
                       Math::ortho(-4, 4, -1, 1, .1f, 1)}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 5> outputs;
        for(unsigned frame = 0; frame < outputs.size(); ++frame) {
            world.lights[0].casts_shadow = frame != 1;
            ASSERT_TRUE(
                masked->set_vector_property("base_color", {1, 1, 1, frame == 2 ? 0.0f : 1.0f}));
            if(frame == 3)
                world.lights[0].position.x += 1;
            if(frame == 4)
                world.lights[0].range = 1;
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            auto drawn = scene.value()->render(frames, submission, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.value()->get_shadow_statistics().point_lights,
                (frame == 1 || frame == 4) ? 1u : 2u);
            EXPECT_EQ(scene.value()->get_shadow_statistics().excess_point_lights, 0u);
            outputs[frame] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 65 * 33 * 4);
            copy_output(frames,
                scene.value()
                    ->get_offscreen_color_view(frames.get_current_frame_slot_index())
                    ->get_image(),
                outputs[frame], {65, 33});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        for(unsigned frame = 0; frame < outputs.size(); ++frame) {
            SCOPED_TRACE(frame);
            const auto pixels = outputs[frame]->read();
            for(unsigned i = 0; i < columns.size(); ++i) {
                const auto value = std::to_integer<int>(pixels[(16 * 65 + columns[i]) * 4]);
                const bool lit = frame == 2 || (i == 0 && (frame == 1 || frame == 3));
                if(lit)
                    EXPECT_GT(value, 50);
                else
                    EXPECT_LE(value, 3);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, ForwardLightsProduceExpectedPixelsAndReuseMaterialBindings) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& renderer = engine->get_renderer();
        MaterialPrograms programs;
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
            RenderWorld world{.render_items = {{.model_matrix = model,
                                  .mesh = scenario == Scenario::NonuniformScale ? tilted : mesh,
                                  .material = {AssetHandle(555), material}}},
                .lights = {light}};
            RenderSubmission submission{world, ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)}};
            if(scenario == Scenario::NoLights)
                world.lights.clear();
            begin_frame(frames);
            RenderGeometry geometry;
            geometry.prepare(world);
            const auto rendered = scene.render(frames, submission, geometry, {});
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
    TEST_F(RenderGraphGpuTest, AmbientOcclusionAffectsOnlyEnvironmentAndTracksMasksResizeAndMsaa) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        TemporaryDirectory directory;
        write_hdr(directory.path() / "white.hdr", 16, 8,
            [](int, int) { return std::array<unsigned char, 4>{128, 128, 128, 129}; });
        auto imported = EnvironmentImporter{}.import(directory.path() / "white.hdr");
        ASSERT_TRUE(imported);
        auto environment =
            Environment::try_create(engine->get_render_resources(), imported.value());
        ASSERT_TRUE(environment);
        auto material = std::make_shared<Material>("receiver", "pbr");
        auto occluder = std::make_shared<Material>("occluder", "pbr");
        const auto mesh = lit_quad();
        RenderWorld world{
            .render_items = {{.entity_id = 1,
                                 .mesh = mesh,
                                 .material = {AssetHandle(1524), material}},
                {.entity_id = 2, .mesh = mesh, .material = {AssetHandle(1525), occluder}}},
            .environment = {{}, false, 0, 0, true, 1},
            .environment_resource = environment.value()};
        for(const auto samples : {SampleCount::Count1, SampleCount::Count4}) {
            MaterialPrograms programs;
            auto scene = create_scene(programs, {33, 33}, samples);
            ASSERT_TRUE(scene) << scene.error();
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            for(bool perspective : {false, true}) {
                SCOPED_TRACE(perspective);
                RenderSubmission submission{
                    world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                               perspective ? Math::perspective(45, 1, .1f, 10)
                                           : Math::ortho(-1, 1, -1, 1, .1f, 10)}};
                std::array<int, 11> centers{};
                for(unsigned frame = 0; frame < centers.size(); ++frame) {
                    SCOPED_TRACE(frame);
                    const Math::Vec2u size = frame % 2 ? Math::Vec2u(65, 65) : Math::Vec2u(33, 33);
                    PostProcessSettings settings{
                        .ao_enabled = frame != 0 && frame != 7 && frame != 9,
                        .ao_radius = 1,
                        .ao_strength = frame == 2 ? 0.0f : 2.0f};
                    ASSERT_TRUE(scene.value()->prepare_post_process(settings));
                    ASSERT_TRUE(scene.value()->resize_offscreen_target(size));
                    world.environment.lighting = frame < 6 || frame == 10;
                    world.lights.clear();
                    if(frame == 6 || frame == 7)
                        world.lights.push_back({.intensity = 1.5f});
                    ASSERT_TRUE(material->set_scalar_property(
                        "emissive_strength", frame == 8 || frame == 9 ? 2.0f : 0.0f));
                    ASSERT_TRUE(material->set_vector_property("emissive_color", {1, 1, 1, 1}));
                    ASSERT_TRUE(occluder->set_surface(
                        {.alpha_mode = frame == 10 ? AlphaMode::Blend : AlphaMode::Mask}));
                    ASSERT_TRUE(occluder->set_vector_property(
                        "base_color", {1, 1, 1, frame == 3 ? 0.0f : 1.0f}));
                    world.render_items[1].model_matrix = Math::scale(
                        Math::translate(Math::Mat4(1), {frame == 4 ? 3.0f : .6f, 0, .65f}),
                        Math::Vec3(.4f));
                    begin_frame(frames);
                    RenderGeometry geometry;
                    geometry.prepare(world);
                    auto drawn = scene.value()->render(frames, submission, geometry);
                    ASSERT_TRUE(drawn) << drawn.error();
                    auto output = std::make_shared<Readback>(
                        device, context.get_context().get_physical_device(), size.x * size.y * 4);
                    ASSERT_NO_FATAL_FAILURE(
                        finish_readback(*scene.value(), frames, output, size, drawn.value()));
                    const auto pixels = output->read();
                    centers[frame] =
                        std::to_integer<int>(pixels[((size.y / 2) * size.x + size.x / 2) * 4]);
                }
                EXPECT_GT(centers[0], 80);
                EXPECT_LT(centers[1], centers[0] - 5);
                EXPECT_LT(centers[5], centers[0] - 5);
                for(unsigned unchanged : {2u, 3u, 4u, 10u})
                    EXPECT_NEAR(centers[unchanged], centers[0], 2);
                EXPECT_NEAR(centers[6], centers[7], 2);
                EXPECT_NEAR(centers[8], centers[9], 2);
                EXPECT_GT(centers[6], 80);
                EXPECT_GT(centers[8], 80);
            }
        }
    }

}
