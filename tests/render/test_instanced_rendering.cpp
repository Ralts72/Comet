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
        auto world = resolver.resolve(scene);
        RenderGeometry geometry;
        geometry.prepare(world);
        MaterialPrograms programs(assets);
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
                geometry.clear();
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
            const RenderSubmission a{world, resolver.resolve_camera(world, left_view)};
            const RenderSubmission b{world, resolver.resolve_camera(world, right_view)};
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
        MaterialPrograms programs(engine->get_asset_registry());
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
        const std::array counts{2u, 32u, 2u, 64u, 64u, 64u, 64u, 2u, 2u, 2u, 8u, 8u, 8u, 8u};
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
                        .material = {material_handle, material, overrides}});
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
                EXPECT_EQ(shadow.draw_calls, frame >= 10 ? 2u : 1u);
                EXPECT_EQ(shadow.drawn_instances, 3 * counts[frame]);
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
            auto expected_upload = 3 * counts[frame] * sizeof(Math::Mat4);
            if(frame == 5 || frame == 9 || frame >= 12)
                expected_upload = 0;
            EXPECT_EQ(stats.instance_upload_bytes, expected_upload);
            for(auto& item : world.render_items)
                item.material.resource = reference;
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
        MaterialPrograms programs(engine->get_asset_registry());
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
                    .material = {AssetHandle{9832}, material}});
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

    TEST_F(RenderGraphGpuTest, ProjectShaderProgramKeepsTwoMaterialsAfterRejectedVersion) {
        constexpr AssetHandle program_handle(9811);
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs(engine->get_asset_registry());
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
        std::array<std::shared_ptr<Readback>, 3> outputs;
        std::shared_ptr<const ShaderProgramArtifact> accepted;
        for(std::size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1) {
                ASSERT_TRUE(registry.replace_asset(program_handle, compile_program(0.5f)));
            } else if(index == 2) {
                auto broken = compile_program(1.0f);
                broken->material->scalars[1].name = "missing";
                ASSERT_TRUE(registry.replace_asset(program_handle, std::move(broken)));
            }
            ASSERT_TRUE(scene.prepare_material_programs(world));
            const auto* published = programs.published(program_handle, "unlit_color");
            ASSERT_NE(published, nullptr);
            if(index == 2) {
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
            EXPECT_EQ(scene.get_material_statistics().cached_material_versions, 2u);
            EXPECT_EQ(scene.get_material_statistics().culled_items, 0u);
            EXPECT_EQ(scene.get_material_statistics().draw_calls, 3u);
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
            const float scale = index == 0 ? 0.25f : 0.5f;
            for(std::size_t side = 0; side < 2; ++side) {
                const auto pixel = (2 * 4 + (side == 0 ? 0 : 3)) * 4;
                const std::array<float, 3> base =
                    side == 0 ? std::array{0.6f, 0.3f, 0.1f} : std::array{0.1f, 0.3f, 0.6f};
                const float frequency = side == 0 ? 1.0f : 2.0f;
                for(std::size_t channel = 0; channel < base.size(); ++channel) {
                    const auto component = bgra ? 2 - channel : channel;
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel + component]),
                        mapped_byte(base[channel] * frequency * scale), 3);
                }
            }
        }
        const auto diagnostics = messages.str();
        EXPECT_NE(diagnostics.find("Missing or incompatible material parameter: missing"),
            std::string::npos);
        EXPECT_EQ(std::count(diagnostics.begin(), diagnostics.end(), '\n'), 1);
        EXPECT_EQ(diagnostics.find("VUID-"), std::string::npos);
        EXPECT_EQ(diagnostics.find("Validation Error"), std::string::npos);
        messages.str(std::string{});
        messages.clear();
    }
}
