#include "core/engine.h"
#include "render/material/material_layout.h"
#include "render/material/material_programs.h"
#include "scene/material_parameters.h"
#include "core/math_utils.h"
#include "support/engine_fixture.h"
#include "support/render_gpu_test.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "render/render_target.h"
#include "render/frame_scheduler.h"
#include "render/resource/render_resources.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/attachment.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/convert.h"
#include "render/material/material.h"
#include "render/material/material_renderer.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "scene/scene.h"
#include "scene/script_component.h"
#include "render/debug/debug_renderer.h"
#include "render/scene/scene_renderer.h"
#include "render/scene/scene_resolver.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "shader/compiler.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"
#include "unlit_color_vert.h"
#include "pbr_vert.h"
#include "pbr_frag.h"
#include "unlit_color_frag.h"

#include <gtest/gtest.h>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace Comet::Tests {
    namespace {
        Result<std::string> material_fragment_source(const char* file) {
            auto source = read_text_file(
                std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/material" / file);
            if(source)
                source.value().insert(
                    0, "#version 450\n#extension GL_GOOGLE_include_directive : require\n");
            return source;
        }
    }

    TEST_F(RenderGpuTest, MaterialMinificationSamplesUploadedTwoDimensionalMips) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        const Math::Vec2u size{9, 9};
        auto scene = create_scene(programs, size);
        ASSERT_TRUE(scene) << scene.error();
        // 基级红色、后两级绿色／蓝色；UV 的大梯度必须选中最后一级。
        TextureData data{.width = 4, .height = 4, .mip_levels = 3};
        for(const auto& [extent, color] : {std::pair{4, std::array<uint8_t, 4>{255, 0, 0, 255}},
                {2, {0, 255, 0, 255}}, {1, {0, 0, 255, 255}}})
            for(int i = 0; i < extent * extent; ++i)
                data.pixels.insert(data.pixels.end(), color.begin(), color.end());
        auto texture = engine->get_render_resources().try_create_texture(data);
        ASSERT_TRUE(texture) << texture.error();
        const auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-1, -1, 0.5f}, {0, 0}, {0, 0, 1}}, {{1, -1, 0.5f}, {128, 0}, {0, 0, 1}},
                 {{1, 1, 0.5f}, {128, 128}, {0, 0, 1}}, {{-1, 1, 0.5f}, {0, 128}, {0, 0, 1}}},
                .indices = {0, 1, 2, 2, 3, 0}});
        ASSERT_TRUE(mesh);
        auto material = std::make_shared<Material>("mip test", "pbr");
        material->set_texture_property("base_color_texture", texture.value());
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        RenderWorld world{
            .render_items = {{.mesh = mesh.value(), .material = {AssetHandle{9400}, material}}},
            .lights = {{.intensity = Math::PI}}};
        RenderSubmission submission{
            world, ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                       Math::ortho(-1, 1, -1, 1, 0.1f, 10)}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        begin_frame(frames);
        RenderGeometry geometry;
        geometry.prepare(world);
        auto drawn = scene.value()->render(frames, submission, geometry);
        ASSERT_TRUE(drawn) << drawn.error();
        auto output = std::make_shared<Readback>(
            device, context.get_context().get_physical_device(), 9 * 9 * 4);
        const auto view =
            scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
        const auto format = view->get_image()->get_info().format;
        finish_readback(*scene.value(), frames, output, size, drawn.value());
        const auto bytes = output->read();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const auto center = (4 * 9 + 4) * 4;
        const auto blue = std::to_integer<int>(bytes[center + (bgra ? 0 : 2)]);
        EXPECT_GT(blue, std::to_integer<int>(bytes[center + (bgra ? 2 : 0)]) + 100);
        EXPECT_GT(blue, std::to_integer<int>(bytes[center + 1]) + 100);
    }

    class MaterialRenderingTest: public EngineTest {
    protected:
        bool draw(const RenderScene& scene) {
            auto& renderer = engine->get_renderer();
            const auto prepared = render_frontend->prepare_frame();
            EXPECT_TRUE(prepared);
            if(!prepared || prepared.value() != Renderer::FramePreparation::Ready)
                return false;
            const auto rendered = render_frontend->render(scene);
            EXPECT_TRUE(rendered);
            return bool(rendered);
        }
        GpuResourceResult<std::shared_ptr<Texture>> texture(std::vector<uint8_t> rgba) {
            return engine->get_render_resources().try_create_texture(
                {.width = 1, .height = 1, .pixels = std::move(rgba)});
        }

        Result<Renderer::MaterialUpdate, GraphicsError> prepare_material_update(
            AssetHandle handle, const std::shared_ptr<Material>& material) {
            const auto snapshot = material->snapshot();
            const auto program = engine->get_asset_registry().resolve<ShaderProgramArtifact>(
                snapshot->get_shader_program());
            return engine->get_renderer().prepare_material_update(handle, snapshot, program);
        }

        std::shared_ptr<ShaderProgramArtifact> scalar_program(const std::filesystem::path& path,
            AssetHandle handle, const std::string& scalar, float maximum) {
            const auto saved = write_text_file_atomic(
                path, "#version 450\nlayout(location=0) out vec4 color;\n"
                      "layout(set=1,binding=0,std140) uniform MaterialData {\n"
                      "vec4 color; float "
                          + scalar
                          + ";} material;\n"
                            "void main(){color=vec4(material.color.rgb*material."
                          + scalar + ",material.color.a);}\n");
            EXPECT_TRUE(saved);
            if(!saved)
                return nullptr;
            auto compiled =
                ShaderCompiler::compile({.source = path, .stage = ShaderStage::Fragment});
            EXPECT_TRUE(compiled.succeeded()) << compiled.diagnostics;
            if(!compiled.succeeded())
                return nullptr;
            auto program = std::make_shared<ShaderProgramArtifact>();
            program->handle = handle;
            program->vertex_words.assign(UNLIT_COLOR_VERT.begin(), UNLIT_COLOR_VERT.end());
            program->fragment_words = std::move(compiled.words);
            program->material =
                ShaderProgramMaterial{.scalars = {{scalar, scalar, 0.1f, 0, maximum, 0.05f}},
                    .vectors = {{"color", "Color", {1, 1, 1, 1}, true}}};
            return program;
        }
    };

    TEST_F(MaterialRenderingTest, RepeatedDrawsReusePreparationAndKeepRuntimeOverridesSeparate) {
        constexpr AssetHandle mesh_handle(9180), other_mesh_handle(9181), material_handle(9182);
        auto& assets = engine->get_asset_registry();
        MeshData data{.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
            .indices = {0, 1, 2}};
        const auto mesh = engine->get_render_resources().try_create_mesh(data);
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(assets.register_asset(mesh_handle, mesh.value()));
        data.indices.clear();
        const auto other_mesh = engine->get_render_resources().try_create_mesh(data);
        ASSERT_TRUE(other_mesh);
        ASSERT_TRUE(assets.register_asset(other_mesh_handle, other_mesh.value()));
        const auto material = std::make_shared<Material>("shared", "unlit_color");
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        auto red = std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 11,
            .material = material_handle,
            .vector_properties = {{"color", {1, 0, 0, 1}}}});
        const auto green =
            std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 12,
                .material = material_handle,
                .vector_properties = {{"color", {0, 1, 0, 1}}}});
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        auto& renderer = engine->get_renderer();
        TemporaryDirectory sources;
        for(unsigned frame = 0; frame < 5; ++frame) {
            SCOPED_TRACE(frame);
            if(frame == 2) {
                ASSERT_TRUE(material->set_vector_property("color", {0, 0, 1, 1}));
                red = std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 11,
                    .material = material_handle,
                    .vector_properties = {{"color", {0.5f, 0, 0, 1}}}});
            }
            if(frame == 4) {
                const auto path = sources.path() / "override.vert";
                ASSERT_TRUE(write_text_file_atomic(path,
                    "#version 450\n#extension GL_GOOGLE_include_directive : require\n"
                    "#include \"frame.glsl\"\nlayout(location=0) in vec3 position;\n"
                    "layout(push_constant) uniform ObjectData {mat4 model;vec4 vertex_bend;} object;\n"
                    "void main(){vec4 world=object.model*vec4(position,1);world.x+=0.01;"
                    "gl_Position=frame.projection*frame.view*world;}\n"));
                auto vertex = ShaderCompiler::compile({.source = path,
                    .stage = ShaderStage::Vertex,
                    .include_directories = {
                        std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/common"}});
                ASSERT_TRUE(vertex.succeeded()) << vertex.diagnostics;
                auto shaders = default_material_shaders();
                shaders.at("unlit_color").vertex = std::move(vertex.words);
                ASSERT_TRUE(renderer.reload_material_shaders(shaders));
            }
            scene.render_items.clear();
            for(unsigned index = 0; index < 96; ++index) {
                std::shared_ptr<const MaterialOverrides> overrides;
                if(index % 3 == 1)
                    overrides = red;
                else if(index % 3 == 2)
                    overrides = green;
                scene.render_items.push_back(
                    {.mesh_handle = frame == 3 && index % 2 ? other_mesh_handle : mesh_handle,
                        .material_handle = material_handle,
                        .material_overrides = std::move(overrides)});
            }
            const auto prepared = render_frontend->prepare_frame();
            ASSERT_TRUE(prepared);
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            const auto rendered = render_frontend->render(scene);
            ASSERT_TRUE(rendered) << rendered.error();
            const auto stats = renderer.get_scene_renderer().get_material_statistics();
            uint32_t expected_draws = 3;
            if(frame == 3)
                expected_draws = 6;
            else if(frame == 4)
                expected_draws = 96;
            EXPECT_EQ(stats.draw_calls, expected_draws);
            EXPECT_EQ(stats.drawn_instances, 96u);
            EXPECT_EQ(stats.instanced_draw_calls, frame == 4 ? 0u : stats.draw_calls);
            EXPECT_EQ(stats.instance_upload_bytes, frame >= 2 ? 0u : 96u * sizeof(PushConstant));
            EXPECT_EQ(stats.material_preparations, 3u);
            EXPECT_EQ(stats.material_binds, 3u);
            EXPECT_EQ(stats.cached_material_versions, 3u);
            if(frame == 3)
                EXPECT_GT(stats.mesh_binds, 1u);
            else
                EXPECT_EQ(stats.mesh_binds, 1u);
            if(frame == 1)
                EXPECT_EQ(stats.material_versions_created, 0u);
            if(frame == 2)
                EXPECT_GT(stats.material_versions_created, 0u);
        }
    }

    TEST_F(MaterialRenderingTest, SmallBatchesKeepCpuCullingWhenLargeScenesUseGpuVisibility) {
        constexpr AssetHandle mesh_handle{9180};
        auto& assets = engine->get_asset_registry();
        const auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-.5f, -.5f, -2}}, {{.5f, -.5f, -2}}, {{0, .5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(assets.register_asset(mesh_handle, mesh.value()));
        for(uint64_t i = 0; i < 4; ++i)
            ASSERT_TRUE(assets.register_asset(
                AssetHandle(9181 + i), std::make_shared<Material>("batch", "unlit_color")));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        for(uint32_t i = 0; i < 256; ++i)
            scene.render_items.push_back(
                {.model_matrix = Math::translate(Math::Mat4(1), {i ? 20.f : 0.f, 0, 0}),
                    .mesh_handle = mesh_handle,
                    .material_handle = AssetHandle(9181 + i % 4)});
        ASSERT_TRUE(draw(scene));
        auto statistics = engine->get_renderer().get_scene_renderer().get_material_statistics();
        EXPECT_EQ(statistics.culled_items, 255u);
        EXPECT_EQ(statistics.drawn_instances, 1u);
        EXPECT_EQ(statistics.gpu_candidate_instances, 0u);
        for(auto& item : scene.render_items)
            item.material_handle = AssetHandle(9181);
        ASSERT_TRUE(draw(scene));
        statistics = engine->get_renderer().get_scene_renderer().get_material_statistics();
        EXPECT_EQ(statistics.gpu_candidate_instances, 256u);
        EXPECT_EQ(statistics.gpu_indirect_draws, 1u);
        scene.render_items.resize(96);
        ASSERT_TRUE(draw(scene));
        statistics = engine->get_renderer().get_scene_renderer().get_material_statistics();
        EXPECT_EQ(statistics.culled_items, 95u);
        EXPECT_EQ(statistics.drawn_instances, 1u);
        EXPECT_EQ(statistics.gpu_candidate_instances, 0u);
    }

    TEST_F(MaterialRenderingTest, CullingKeepsResidentMaterialsAndUsesReloadedMeshBounds) {
        constexpr AssetHandle visible_mesh(9190), hidden_mesh(9191);
        constexpr AssetHandle visible_material(9192), hidden_material(9193);
        auto& assets = engine->get_asset_registry();
        const auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(assets.register_asset(visible_mesh, mesh.value()));
        ASSERT_TRUE(assets.register_asset(hidden_mesh, mesh.value()));
        const auto material = std::make_shared<Material>("hidden", "unlit_color");
        ASSERT_TRUE(assets.register_asset(
            visible_material, std::make_shared<Material>("visible", "unlit_color")));
        ASSERT_TRUE(assets.register_asset(hidden_material, material));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items = {{.mesh_handle = visible_mesh, .material_handle = visible_material},
            {.mesh_handle = hidden_mesh,
                .material_handle = hidden_material,
                .material_overrides = std::make_shared<const MaterialOverrides>(
                    MaterialOverrides{.instance_id = 41, .material = hidden_material})}};
        scene.render_items.push_back(scene.render_items.back());
        auto& renderer = engine->get_renderer();
        for(unsigned frame = 0; frame < 6; ++frame) {
            SCOPED_TRACE(frame);
            const bool hidden = frame % 2 == 0;
            for(size_t index = 1; index < scene.render_items.size(); ++index)
                scene.render_items[index].model_matrix =
                    Math::translate(Math::Mat4(1), {hidden ? 20.0f : 0.0f, 0, 0});
            if(frame == 4)
                ASSERT_TRUE(material->set_vector_property("color", {0, 0.5f, 0, 1}));
            ASSERT_TRUE(draw(scene));
            const auto stats = renderer.get_scene_renderer().get_material_statistics();
            EXPECT_EQ(stats.render_items, 3u);
            EXPECT_EQ(stats.culled_items, hidden ? 2u : 0u);
            EXPECT_EQ(stats.draw_calls, hidden ? 1u : 2u);
            EXPECT_EQ(stats.cached_material_versions, frame == 0 ? 1u : 2u);
            EXPECT_EQ(stats.material_versions_created, frame == 0 || frame == 1 || frame == 5);
        }
        for(size_t index = 1; index < scene.render_items.size(); ++index)
            scene.render_items[index].model_matrix = Math::translate(Math::Mat4(1), {20, 0, 0});
        const auto replacement = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-20.5f, -0.5f, -2}}, {{-19.5f, -0.5f, -2}}, {{-20, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(assets.replace_asset(hidden_mesh, replacement.value()));
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().culled_items, 0u);
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 2u);
        scene.render_items.resize(1);
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(
            renderer.get_scene_renderer().get_material_statistics().cached_material_versions, 1u);
    }

    TEST_F(RenderGpuTest, PublicationRemovalReleasesCurrentSubmissionWithAHiddenView) {
        constexpr AssetHandle mesh_handle{9210}, material_handle{9211};
        auto& assets = engine->get_asset_registry();
        const auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(assets.register_asset(mesh_handle, mesh.value()));
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(prepare_offscreen_host({64, 64}));
        const auto secondary = renderer.create_offscreen_view({32, 32});
        ASSERT_TRUE(secondary);
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back(
            {.mesh_handle = mesh_handle, .material_handle = material_handle});
        for(const bool hidden : {false, true}) {
            SCOPED_TRACE(hidden);
            ASSERT_TRUE(renderer.set_render_view({}));
            ASSERT_TRUE(renderer.set_render_view({}, secondary.value()));
            auto material = std::make_shared<Material>("retired", "unlit_color");
            ASSERT_TRUE(assets.register_asset(material_handle, material));
            auto snapshot = material->snapshot();
            const std::weak_ptr retired = snapshot;
            for(int frame = 0; frame < 2; ++frame) {
                const auto prepared = render_frontend->prepare_frame();
                ASSERT_TRUE(prepared);
                ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
                ASSERT_TRUE(render_frontend->render(scene));
            }
            EXPECT_EQ(
                renderer.get_scene_renderer().get_material_statistics().cached_material_versions,
                1u);
            EXPECT_EQ(renderer.get_scene_renderer(secondary.value())
                          .get_material_statistics()
                          .cached_material_versions,
                1u);
            ASSERT_TRUE(assets.unregister_asset(material_handle));
            snapshot.reset();
            material.reset();
            EXPECT_FALSE(retired.expired());
            ASSERT_TRUE(renderer.set_render_view({.visible = !hidden}));
            ASSERT_TRUE(renderer.set_render_view({.visible = !hidden}, secondary.value()));
            const auto prepared = render_frontend->prepare_frame();
            ASSERT_TRUE(prepared);
            EXPECT_TRUE(retired.expired());
            EXPECT_EQ(
                renderer.get_scene_renderer().get_material_statistics().cached_material_versions,
                0u);
            EXPECT_EQ(renderer.get_scene_renderer(secondary.value())
                          .get_material_statistics()
                          .cached_material_versions,
                0u);
            if(prepared.value() == Renderer::FramePreparation::Ready)
                ASSERT_TRUE(renderer.render_frame());
        }
    }

    TEST_F(MaterialRenderingTest, CullingFollowsPublishedVertexCodeAcrossReloadFailureAndRestore) {
        constexpr AssetHandle mesh_handle(9194), material_handle(9195);
        auto& assets = engine->get_asset_registry();
        const auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(assets.register_asset(mesh_handle, mesh.value()));
        ASSERT_TRUE(assets.register_asset(
            material_handle, std::make_shared<Material>("translated", "unlit_color")));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back({.model_matrix = Math::translate(Math::Mat4(1), {1000, 0, 0}),
            .mesh_handle = mesh_handle,
            .material_handle = material_handle});
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().culled_items, 1u);
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 0u);

        TemporaryDirectory directory;
        const auto source = directory.path() / "translated.vert";
        ASSERT_TRUE(write_text_file_atomic(source,
            "#version 450\n#extension GL_GOOGLE_include_directive : require\n"
            "#include \"frame.glsl\"\nlayout(location=0) in vec3 position;\n"
            "layout(push_constant) uniform ObjectData {mat4 model;vec4 vertex_bend;} object;\n"
            "void main(){vec4 world=object.model*vec4(position,1);world.x-=1000;"
            "gl_Position=frame.projection*frame.view*world;}\n"));
        const auto compiled = ShaderCompiler::compile({.source = source,
            .stage = ShaderStage::Vertex,
            .include_directories = {
                std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/common"}});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        auto shaders = default_material_shaders();
        shaders.at("unlit_color").vertex = compiled.words;
        ASSERT_TRUE(renderer.reload_material_shaders(shaders));
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().culled_items, 0u);
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 1u);

        shaders.at("unlit_color").vertex = {0};
        EXPECT_FALSE(renderer.reload_material_shaders(shaders));
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().culled_items, 0u);
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 1u);

        ASSERT_TRUE(renderer.reload_material_shaders(default_material_shaders()));
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().culled_items, 1u);
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 0u);

        constexpr AssetHandle program_handle(9196);
        auto program = std::make_shared<ShaderProgramArtifact>();
        program->handle = program_handle;
        program->vertex_words = compiled.words;
        program->fragment_words = default_material_shaders().at("unlit_color").fragment;
        ASSERT_TRUE(assets.register_asset(program_handle, program));
        ASSERT_TRUE(assets.replace_asset(
            material_handle, std::make_shared<Material>("project", "unlit_color", program_handle)));
        scene.render_items.front().material_overrides = std::make_shared<const MaterialOverrides>(
            MaterialOverrides{.instance_id = 47, .material = material_handle});
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 1u);

        ASSERT_TRUE(assets.replace_asset(
            material_handle, std::make_shared<Material>("builtin", "unlit_color")));
        scene.render_items.front().material_overrides =
            std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 47,
                .material = material_handle,
                .scalar_properties = {{"unknown", 1}}});
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().culled_items, 0u);
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 1u);

        scene.render_items.front().material_overrides = std::make_shared<const MaterialOverrides>(
            MaterialOverrides{.instance_id = 47, .material = material_handle});
        ASSERT_TRUE(draw(scene));
        ASSERT_TRUE(draw(scene));
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().culled_items, 1u);
        EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 0u);
    }

    TEST_F(MaterialRenderingTest, ProjectProgramHandleCreatesPipelineAndRejectsIncompatibleCode) {
        constexpr AssetHandle program_handle(9081);
        constexpr AssetHandle material_handle(9082);
        auto make_program = [&](const bool compatible) {
            auto program = std::make_shared<ShaderProgramArtifact>();
            program->handle = program_handle;
            program->vertex_words.assign(PBR_VERT.begin(), PBR_VERT.end());
            if(compatible)
                program->fragment_words.assign(PBR_FRAG.begin(), PBR_FRAG.end());
            else
                program->fragment_words.assign(UNLIT_COLOR_FRAG.begin(), UNLIT_COLOR_FRAG.end());
            return program;
        };
        auto& assets = engine->get_asset_registry();
        ASSERT_TRUE(assets.register_asset(program_handle, make_program(false)));
        auto material = std::make_shared<Material>("project", "pbr", program_handle);
        auto& renderer = engine->get_renderer();
        EXPECT_FALSE(prepare_material_update(material_handle, material));

        ASSERT_TRUE(assets.replace_asset(program_handle, make_program(true)));
        auto prepared = prepare_material_update(material_handle, material);
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_TRUE(std::move(prepared).value().publish());
        EXPECT_EQ(
            renderer.get_scene_renderer().get_material_statistics().cached_material_versions, 1u);

        ASSERT_TRUE(assets.replace_asset(program_handle, make_program(false)));
        auto retained = prepare_material_update(material_handle, material);
        EXPECT_FALSE(retained);
    }

    TEST_F(MaterialRenderingTest, ProjectProgramMetadataPreparesCustomScalarLayout) {
        constexpr AssetHandle program_handle(9083);
        constexpr AssetHandle material_handle(9084);
        TemporaryDirectory directory;
        const auto source = directory.path() / "custom.frag";
        ASSERT_TRUE(write_text_file_atomic(source,
            "#version 450\n"
            "layout(location=0) out vec4 color;\n"
            "layout(set=1,binding=0,std140) uniform MaterialData {\n"
            "  vec4 color; float intensity; float frequency;\n"
            "} material;\n"
            "void main(){ color=vec4(material.color.rgb * material.intensity "
            "* material.frequency, material.color.a); }\n"));
        auto compiled = ShaderCompiler::compile({.source = source, .stage = ShaderStage::Fragment});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        auto program = std::make_shared<ShaderProgramArtifact>();
        program->handle = program_handle;
        program->vertex_words.assign(UNLIT_COLOR_VERT.begin(), UNLIT_COLOR_VERT.end());
        program->fragment_words = std::move(compiled.words);
        program->material =
            ShaderProgramMaterial{.scalars = {{"intensity", "Intensity", 1.0f, 0.0f, 10.0f, 0.05f},
                                      {"frequency", "Frequency", 2.0f, 0.0f, 4.0f, 0.1f}},
                .vectors = {{"color", "Color", {1, 1, 1, 1}, true}}};
        ASSERT_TRUE(engine->get_asset_registry().register_asset(program_handle, program));
        auto material = std::make_shared<Material>("custom", "unlit_color", program_handle);
        ASSERT_TRUE(material->set_scalar_property("frequency", 3.0f));
        auto prepared = prepare_material_update(material_handle, material);
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_TRUE(std::move(prepared).value().publish());
        const auto* published = engine->get_renderer().get_material_program_feedback()->published(
            program_handle, "unlit_color");
        ASSERT_NE(published, nullptr);
        EXPECT_EQ(published->source, program);
        const auto first_layout = published->layout;
        auto duplicate = std::make_shared<ShaderProgramArtifact>(*program);
        ASSERT_TRUE(engine->get_asset_registry().replace_asset(program_handle, duplicate));
        auto unchanged = prepare_material_update(material_handle, material);
        ASSERT_TRUE(unchanged) << unchanged.error();
        published = engine->get_renderer().get_material_program_feedback()->published(
            program_handle, "unlit_color");
        ASSERT_NE(published, nullptr);
        EXPECT_EQ(published->source, duplicate);
        EXPECT_EQ(published->layout, first_layout);
        auto incompatible = std::make_shared<ShaderProgramArtifact>(*program);
        incompatible->material->scalars[1].name = "missing";
        ASSERT_TRUE(engine->get_asset_registry().replace_asset(program_handle, incompatible));
        auto rejected = prepare_material_update(material_handle, material);
        ASSERT_FALSE(rejected);
        published = engine->get_renderer().get_material_program_feedback()->published(
            program_handle, "unlit_color");
        ASSERT_NE(published, nullptr);
        EXPECT_EQ(published->source, duplicate);
        EXPECT_EQ(published->layout->get_scalars()[1].name, "frequency");
        ASSERT_TRUE(engine->get_renderer().enable_offscreen_rendering({64, 64}));
        EXPECT_FALSE(prepare_material_update(material_handle, material));
        ASSERT_TRUE(engine->get_asset_registry().replace_asset(program_handle, duplicate));
        auto rebuilt = prepare_material_update(material_handle, material);
        ASSERT_TRUE(rebuilt) << rebuilt.error();
        ASSERT_TRUE(std::move(rebuilt).value().publish());
        published = engine->get_renderer().get_material_program_feedback()->published(
            program_handle, "unlit_color");
        ASSERT_NE(published, nullptr);
        EXPECT_EQ(published->source, duplicate);
        EXPECT_EQ(engine->get_renderer()
                      .get_scene_renderer()
                      .get_material_statistics()
                      .cached_material_versions,
            1u);
    }

    TEST_F(
        MaterialRenderingTest, RuntimeOverrideChangesRetryRejectedProgramAndStopRestoresBaseline) {
        constexpr AssetHandle program_handle(9140);
        constexpr AssetHandle material_handle(9141);
        constexpr AssetHandle mesh_handle(9142);
        TemporaryDirectory directory;
        const auto source = directory.path() / "material.frag";
        const auto original = scalar_program(source, program_handle, "intensity", 10);
        const auto limited = scalar_program(source, program_handle, "intensity", 0.2f);
        const auto renamed = scalar_program(source, program_handle, "gain", 10);
        ASSERT_TRUE(original);
        ASSERT_TRUE(limited);
        ASSERT_TRUE(renamed);
        auto& assets = engine->get_asset_registry();
        ASSERT_TRUE(assets.register_asset(program_handle, original));
        const auto material = std::make_shared<Material>("runtime", "unlit_color", program_handle);
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        const auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(assets.register_asset(mesh_handle, mesh.value()));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back({.entity_id = 1,
            .mesh_handle = mesh_handle,
            .material_handle = material_handle,
            .material_overrides =
                std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 91,
                    .material = material_handle,
                    .scalar_properties = {{"intensity", 0.75f}}})});
        auto& renderer = engine->get_renderer();
        const auto draw = [&] {
            const auto prepared = render_frontend->prepare_frame();
            EXPECT_TRUE(prepared);
            if(!prepared || prepared.value() != Renderer::FramePreparation::Ready)
                return false;
            const auto rendered = render_frontend->render(scene);
            EXPECT_TRUE(rendered);
            if(!rendered)
                return false;
            EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 1u);
            return true;
        };
        const auto published = [&] {
            const auto* active =
                renderer.get_material_program_feedback()->published(program_handle, "unlit_color");
            return active ? active->source : nullptr;
        };
        ASSERT_TRUE(draw());
        EXPECT_EQ(published(), original);
        ASSERT_TRUE(assets.replace_asset(program_handle, limited));
        ASSERT_TRUE(draw());
        EXPECT_EQ(published(), original);
        EXPECT_EQ(
            renderer.get_scene_renderer().get_material_statistics().material_versions_created, 0u);
        // 输入不变时仍保留原程序；不会把失败候选部分发布。
        ASSERT_TRUE(draw());
        EXPECT_EQ(published(), original);
        scene.render_items.front().material_overrides =
            std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 91,
                .material = material_handle,
                .scalar_properties = {{"intensity", 0.1f}}});
        ASSERT_TRUE(draw());
        EXPECT_EQ(published(), limited);

        ASSERT_TRUE(assets.replace_asset(program_handle, renamed));
        ASSERT_TRUE(draw());
        EXPECT_EQ(published(), limited);
        // Stop 后提取的快照没有运行覆盖；同一失败源现在可以完整发布。
        scene.render_items.front().material_overrides.reset();
        ASSERT_TRUE(draw());
        EXPECT_EQ(published(), renamed);
        scene.render_items.front().material_overrides =
            std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 92,
                .material = material_handle,
                .scalar_properties = {{"gain", 0.5f}}});
        ASSERT_TRUE(draw());
        // 同 Handle 材质切换模板，旧覆盖不兼容时继续画完整旧版本。
        ASSERT_TRUE(assets.replace_asset(
            material_handle, std::make_shared<Material>("replacement", "pbr")));
        ASSERT_TRUE(draw());
        EXPECT_EQ(
            renderer.get_scene_renderer().get_material_statistics().material_versions_created, 0u);
        ASSERT_TRUE(draw());
        scene.render_items.front().material_overrides.reset();
        ASSERT_TRUE(draw());
        EXPECT_EQ(
            renderer.get_scene_renderer().get_material_statistics().material_versions_created, 1u);
        renderer.wait_idle();
    }

    TEST_F(MaterialRenderingTest, DuplicateRuntimeInputsUseLastSnapshotForReloadAndRemoval) {
        constexpr AssetHandle program_handle(9240), material_handle(9241), mesh_handle(9242);
        TemporaryDirectory directory;
        const auto source = directory.path() / "material.frag";
        const auto original = scalar_program(source, program_handle, "intensity", 10);
        const auto limited = scalar_program(source, program_handle, "intensity", 0.2f);
        ASSERT_TRUE(original);
        ASSERT_TRUE(limited);
        auto& assets = engine->get_asset_registry();
        ASSERT_TRUE(assets.register_asset(program_handle, original));
        ASSERT_TRUE(assets.register_asset(
            material_handle, std::make_shared<Material>("runtime", "unlit_color", program_handle)));
        const auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(assets.register_asset(mesh_handle, mesh.value()));
        const auto overrides = [&](uint64_t identity, float intensity) {
            return std::make_shared<const MaterialOverrides>(
                MaterialOverrides{.instance_id = identity,
                    .material = material_handle,
                    .scalar_properties = {{"intensity", intensity}}});
        };
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back({.mesh_handle = mesh_handle,
            .material_handle = material_handle,
            .material_overrides = overrides(91, 0.75f)});
        auto& device = engine->get_renderer().get_render_context().get_device();
        MaterialPrograms programs;
        auto owner = SceneRenderer::create(device, programs, engine->get_render_resources(),
            {.msaa_samples = SampleCount::Count1}, {}, {64, 32});
        ASSERT_TRUE(owner);
        auto& scene_renderer = *owner.value();
        SceneResolver resolver(assets);
        RenderCameraResolver camera_resolver;
        auto world = resolver.resolve(scene);
        const RenderSubmission submission{
            world, camera_resolver.resolve(world, {.render_size = {64, 32}})};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        ASSERT_TRUE(scene_renderer.prepare_material_programs(world));
        RenderGeometry geometry;
        geometry.prepare(world);
        auto rendered = scene_renderer.render(frames, submission, geometry);
        ASSERT_TRUE(rendered);
        frames.get_current_command_buffer().end();
        ASSERT_TRUE(frames.submit(rendered.value(), {}));
        frames.end_frame();
        frames.wait_for_all_slots();

        auto item = scene.render_items.front();
        item.material_overrides = overrides(92, 0.1f);
        scene.render_items.push_back(item);
        item.material_overrides = overrides(91, 0.1f);
        scene.render_items.push_back(item);
        resolver.resolve(scene, world);
        ASSERT_TRUE(scene_renderer.prepare_material_programs(world));
        ASSERT_TRUE(assets.replace_asset(program_handle, limited));
        resolver.resolve(scene, world);
        ASSERT_TRUE(scene_renderer.prepare_material_programs(world));
        const auto* active = programs.published(program_handle, "unlit_color");
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(active->source, limited);

        world.render_items.clear();
        ASSERT_TRUE(scene_renderer.prepare_material_programs(world));
        EXPECT_EQ(scene_renderer.get_material_statistics().cached_material_versions, 0u);
    }

    TEST_F(MaterialRenderingTest, PlayStartupAcceptsPublishedLayoutBeforeScriptValidation) {
        constexpr AssetHandle program_handle(9230), material_handle(9231), script_handle(9232);
        TemporaryDirectory sources;
        const auto program =
            scalar_program(sources.path() / "startup.frag", program_handle, "custom", 1);
        ASSERT_TRUE(program);
        auto& assets = engine->get_asset_registry();
        ASSERT_TRUE(assets.register_asset(program_handle, program));
        const auto material = std::make_shared<Material>("startup", "unlit_color", program_handle);
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        auto preview = prepare_material_update(material_handle, material);
        ASSERT_TRUE(preview) << preview.error();
        ASSERT_TRUE(std::move(preview).value().publish());
        const auto unavailable = std::make_shared<ShaderProgramArtifact>();
        unavailable->handle = program_handle;
        ASSERT_TRUE(assets.replace_asset(program_handle, unavailable));
        MaterialOverrides values{
            .material = material_handle, .scalar_properties = {{"custom", 0.5f}}};
        EXPECT_FALSE(engine->get_material_parameters().validate(values));

        const auto script = Script::create(R"(
            return { on_start = function()
                comet.set_material_scalar("custom", 0.5)
            end }
        )");
        ASSERT_TRUE(script) << script.error().message;
        ASSERT_TRUE(assets.register_asset(script_handle, script.value()));
        auto scene = std::make_unique<Scene>();
        auto entity = scene->create_entity();
        entity.add_component<MeshRendererComponent>(INVALID_ASSET_HANDLE, material_handle);
        entity.add_component<ScriptComponent>().asset = script_handle;
        engine->set_scene(std::move(scene));
        ASSERT_TRUE(engine->add_default_scene_systems());
        ASSERT_TRUE(engine->start_scene_runtime());
        EXPECT_TRUE(engine->get_material_parameters().validate(values));
        const auto overrides = engine->get_scene()->get_material_overrides(entity);
        ASSERT_TRUE(overrides);
        EXPECT_EQ(overrides->scalar_properties.at("custom"), 0.5f);
        ASSERT_TRUE(engine->stop_scene_runtime());
        EXPECT_FALSE(engine->get_scene()->get_material_overrides(entity));
    }

    class ProjectMaterialPublicationTest: public MaterialRenderingTest {
    protected:
        void SetUp() override {
            MaterialRenderingTest::SetUp();
            if(HasFatalFailure())
                return;
            auto& device = engine->get_renderer().get_render_context().get_device();
            auto created_pass = RenderPass::create(device,
                {Attachment::get_color_attachment(Format::R8G8B8A8_UNORM),
                    Attachment::get_depth_attachment(Format::D32_SFLOAT)},
                {RenderSubPass{
                    {}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}}},
                Format::R8G8B8A8_UNORM);
            ASSERT_TRUE(created_pass) << created_pass.error();
            pass = std::move(created_pass).value();
            pipelines = std::make_unique<PipelineManager>(device, *pass);
            programs = std::make_unique<MaterialPrograms>();
            auto created_materials = MaterialRenderer::create(device, *pipelines,
                engine->get_render_resources(), 2, SampleCount::Count1, nullptr, programs.get());
            ASSERT_TRUE(created_materials) << created_materials.error();
            materials = std::move(created_materials).value();
        }

        void TearDown() override {
            materials.reset();
            programs.reset();
            pipelines.reset();
            pass.reset();
            MaterialRenderingTest::TearDown();
        }

        std::shared_ptr<ShaderProgramArtifact> program(bool optional) {
            auto result = std::make_shared<ShaderProgramArtifact>();
            result->handle = program_handle;
            result->vertex_words.assign(PBR_VERT.begin(), PBR_VERT.end());
            result->fragment_words.assign(PBR_FRAG.begin(), PBR_FRAG.end());
            result->material.emplace();
            const auto layout = MaterialLayout::find_builtin("pbr");
            for(const auto& texture : layout->get_textures())
                result->material->textures.push_back({texture.name, texture.display_name,
                    texture.name == "base_color_texture" ? optional : true});
            for(const auto& scalar : layout->get_scalars())
                result->material->scalars.push_back({scalar.name, scalar.display_name,
                    scalar.default_value, scalar.min_value, scalar.max_value, scalar.step});
            for(const auto& vector : layout->get_vectors())
                result->material->vectors.push_back(
                    {vector.name, vector.display_name, vector.default_value,
                        vector.semantic == MaterialLayout::VectorProperty::Semantic::Color});
            return result;
        }

        void publish_material(AssetHandle handle, const std::shared_ptr<Material>& source) {
            auto update = materials->prepare_material_update(handle, source->snapshot(),
                engine->get_asset_registry().resolve<ShaderProgramArtifact>(
                    source->get_shader_program()));
            ASSERT_TRUE(update) << update.error();
            std::move(update).value().publish();
        }

        std::shared_ptr<const ShaderProgramArtifact> published() const {
            const auto* current = programs->published(program_handle, "pbr");
            return current ? current->source : nullptr;
        }

        Result<void, GraphicsError> prepare_current_programs(
            RenderWorld& world, std::span<MaterialRenderer* const> views = {}) {
            for(auto& item : world.render_items) {
                auto& binding = item.material;
                auto source =
                    engine->get_asset_registry().resolve<const Material>(binding.material_handle);
                if(!source)
                    source = binding.resource;
                binding.resource = source->snapshot();
                binding.program_source =
                    engine->get_asset_registry().resolve<ShaderProgramArtifact>(
                        source->get_shader_program());
            }
            if(views.empty())
                return materials->prepare_programs(world);
            return MaterialRenderer::prepare_programs(views, world);
        }

        static constexpr AssetHandle program_handle{9200};
        static constexpr AssetHandle first_handle{9201};
        static constexpr AssetHandle second_handle{9202};
        std::unique_ptr<RenderPass> pass;
        std::unique_ptr<PipelineManager> pipelines;
        std::unique_ptr<MaterialPrograms> programs;
        std::unique_ptr<MaterialRenderer> materials;
    };

    TEST_F(ProjectMaterialPublicationTest,
        ProgramPreparationConsumesCapturedSourceRatherThanLatestRegistry) {
        auto& assets = engine->get_asset_registry();
        const auto captured = program(true);
        ASSERT_TRUE(assets.register_asset(program_handle, captured));
        auto source = std::make_shared<Material>("captured", "pbr", program_handle);
        RenderWorld world{.render_items = {{.material = {.material_handle = first_handle,
                                                .resource = source->snapshot(),
                                                .program_source = captured}}}};
        auto invalid = program(true);
        invalid->material->textures.front().name = "unknown_texture";
        ASSERT_TRUE(assets.replace_asset(program_handle, invalid));
        ASSERT_TRUE(materials->prepare_programs(world));
        EXPECT_EQ(published(), captured);
        EXPECT_EQ(messages.str().find("Cannot publish project Shader"), std::string::npos);

        ASSERT_TRUE(prepare_current_programs(world));
        EXPECT_EQ(published(), captured);
        EXPECT_NE(messages.str().find("Cannot publish project Shader"), std::string::npos);
        const auto rejected = messages.str();
        ASSERT_TRUE(materials->prepare_programs(world));
        EXPECT_EQ(messages.str(), rejected);
    }

    TEST_F(ProjectMaterialPublicationTest, CapturedRemovalsReleaseEveryViewAfterReregistration) {
        auto& assets = engine->get_asset_registry();
        auto& device = engine->get_renderer().get_render_context().get_device();
        const auto shader = program(true);
        const auto source = std::make_shared<Material>("removed", "pbr", program_handle);
        ASSERT_TRUE(assets.register_asset(program_handle, shader));
        ASSERT_TRUE(assets.register_asset(first_handle, source));
        PipelineManager second_pipelines(device, *pass);
        auto created = MaterialRenderer::create(device, second_pipelines,
            engine->get_render_resources(), 2, SampleCount::Count1, nullptr, programs.get());
        ASSERT_TRUE(created) << created.error();
        const auto views = std::array{materials.get(), created.value().get()};
        auto candidates = MaterialRenderer::prepare_material_updates(
            views, first_handle, source->snapshot(), shader);
        ASSERT_TRUE(candidates) << candidates.error();
        for(auto& update : candidates.value())
            std::move(update).publish();
        const auto feedback = programs->feedback();
        for(const auto* view : views)
            EXPECT_EQ(view->get_statistics().cached_material_versions, 1u);

        const std::weak_ptr retired_source = source->snapshot();
        EXPECT_FALSE(retired_source.expired());
        ASSERT_TRUE(assets.unregister_asset(program_handle));
        const auto shader_removed = assets.take_removals();
        programs->remove_assets(shader_removed);
        for(auto* view : views) {
            view->remove_assets(shader_removed);
            EXPECT_EQ(view->get_statistics().cached_material_versions, 0u);
        }
        EXPECT_TRUE(retired_source.expired());
        EXPECT_TRUE(assets.contains<Material>(first_handle));
        ASSERT_TRUE(assets.register_asset(program_handle, shader));
        candidates = MaterialRenderer::prepare_material_updates(
            views, first_handle, source->snapshot(), shader);
        ASSERT_TRUE(candidates) << candidates.error();
        for(auto& update : candidates.value())
            std::move(update).publish();

        ASSERT_TRUE(assets.unregister_asset(first_handle));
        ASSERT_TRUE(assets.unregister_asset(program_handle));
        const auto removals = assets.take_removals();
        ASSERT_TRUE(assets.register_asset(first_handle, source));
        ASSERT_TRUE(assets.register_asset(program_handle, shader));
        programs->remove_assets(removals);
        for(auto* view : views) {
            view->remove_assets(removals);
            EXPECT_EQ(view->get_statistics().cached_material_versions, 0u);
        }
        EXPECT_FALSE(published());
        ASSERT_NE(feedback->published(program_handle, "pbr"), nullptr);
        EXPECT_EQ(feedback->published(program_handle, "pbr")->source, shader);
        EXPECT_TRUE(assets.take_removals().empty());
        candidates = MaterialRenderer::prepare_material_updates(
            views, first_handle, source->snapshot(), shader);
        ASSERT_TRUE(candidates) << candidates.error();
        for(auto& update : candidates.value())
            std::move(update).publish();
        for(const auto* view : views)
            EXPECT_EQ(view->get_statistics().cached_material_versions, 1u);
        EXPECT_EQ(published(), shader);
        assets.clear();
        const auto reset = assets.take_removals();
        ASSERT_TRUE(reset.all);
        programs->remove_assets(reset);
        for(auto* view : views) {
            view->remove_assets(reset);
            EXPECT_EQ(view->get_statistics().cached_material_versions, 0u);
        }
        EXPECT_FALSE(published());
        EXPECT_EQ(feedback->published(program_handle, "pbr")->source, shader);
    }

    TEST_F(ProjectMaterialPublicationTest, MaterialPreviewConsumesCapturedShaderSource) {
        auto& assets = engine->get_asset_registry();
        const auto captured = program(true);
        ASSERT_TRUE(assets.register_asset(program_handle, captured));
        auto source = std::make_shared<Material>("preview", "pbr", program_handle);
        auto invalid = program(true);
        invalid->material->textures.front().name = "unknown_texture";
        ASSERT_TRUE(assets.replace_asset(program_handle, invalid));

        auto update =
            materials->prepare_material_update(first_handle, source->snapshot(), captured);
        ASSERT_TRUE(update) << update.error();
        std::move(update).value().publish();
        const auto feedback = programs->feedback();
        EXPECT_EQ(published(), captured);
        EXPECT_EQ(messages.str().find("Cannot publish project Shader"), std::string::npos);

        const auto rejected =
            materials->prepare_material_update(first_handle, source->snapshot(), invalid);
        EXPECT_FALSE(rejected);
        EXPECT_EQ(programs->feedback(), feedback);
        EXPECT_EQ(published(), captured);
        EXPECT_NE(messages.str().find("Cannot publish project Shader"), std::string::npos);
    }

    TEST_F(ProjectMaterialPublicationTest, RendererMaterialPreparationUsesOnlyCapturedInputs) {
        auto& assets = engine->get_asset_registry();
        auto& renderer = engine->get_renderer();
        const auto second = renderer.create_offscreen_view({16, 16});
        ASSERT_TRUE(second) << second.error();
        const auto image = texture({255, 255, 255, 255});
        ASSERT_TRUE(image) << image.error();
        const auto captured_program = program(false);
        auto source = std::make_shared<Material>("preview", "pbr", program_handle);
        source->set_texture_property("base_color_texture", image.value());
        ASSERT_TRUE(assets.register_asset(program_handle, captured_program));
        ASSERT_TRUE(assets.register_asset(first_handle, source));
        const auto snapshot = source->snapshot();

        // Main 捕获后改变编辑源和注册版本，GPU 仍应消费这次完整输入。
        source->set_texture_property("base_color_texture", nullptr);
        auto invalid = program(false);
        invalid->material->textures.front().name = "unknown_texture";
        ASSERT_TRUE(assets.replace_asset(program_handle, invalid));
        auto update = renderer.prepare_material_update(first_handle, snapshot, captured_program);
        ASSERT_TRUE(update) << update.error();
        ASSERT_TRUE(std::move(update).value().publish());
        const auto feedback = renderer.get_material_program_feedback();
        const auto* published = feedback->published(program_handle, "pbr");
        ASSERT_NE(published, nullptr);
        EXPECT_EQ(published->source, captured_program);
        for(const auto view : {Renderer::DEFAULT_VIEW, second.value()})
            EXPECT_EQ(renderer.get_scene_renderer(view)
                          .get_material_statistics()
                          .cached_material_versions,
                1u);
        EXPECT_EQ(assets.resolve<Material>(first_handle), source);
        EXPECT_EQ(assets.resolve<ShaderProgramArtifact>(program_handle), invalid);
        EXPECT_EQ(snapshot->get_texture_property("base_color_texture"), image.value());
        EXPECT_FALSE(
            renderer.prepare_material_update(first_handle, source->snapshot(), captured_program));

        // 注册表仍有有效产物，也不能补用本次输入中缺失的 Shader。
        ASSERT_TRUE(assets.replace_asset(program_handle, captured_program));
        EXPECT_FALSE(renderer.prepare_material_update(first_handle, snapshot, {}));
        EXPECT_EQ(renderer.get_material_program_feedback(), feedback);
        EXPECT_TRUE(renderer.prepare_material_update(first_handle, snapshot, captured_program));
    }

    TEST_F(ProjectMaterialPublicationTest, ProjectProgramPublicationWaitsForEveryViewCandidate) {
        auto& assets = engine->get_asset_registry();
        auto& device = engine->get_renderer().get_render_context().get_device();
        TemporaryDirectory temporary;
        const auto path = temporary.path() / "relocated.frag";
        const auto shader_directory = std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders";
        auto source_text = material_fragment_source("pbr_surface.glsl");
        ASSERT_TRUE(source_text) << source_text.error();
        constexpr std::string_view binding = "layout(set = 1, binding = 1)";
        const auto offset = source_text.value().find(binding);
        ASSERT_NE(offset, std::string::npos);
        source_text.value().replace(offset, binding.size(), "layout(set = 1, binding = 7)");
        ASSERT_TRUE(write_text_file_atomic(path, source_text.value()));
        const auto compiled = ShaderCompiler::compile({.source = path,
            .stage = ShaderStage::Fragment,
            .include_directories = {shader_directory / "material"}});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        PipelineManager second_pipelines(device, *pass);
        const MaterialShaders relocated{
            {"pbr", {default_material_shaders().at("pbr").vertex, compiled.words}}};
        auto second_result = MaterialRenderer::create(device, second_pipelines,
            engine->get_render_resources(), 2, SampleCount::Count1, &relocated, programs.get());
        ASSERT_TRUE(second_result) << second_result.error();
        auto second = std::move(second_result).value();
        const auto prior = program(true);
        ASSERT_TRUE(assets.register_asset(program_handle, prior));
        auto source = std::make_shared<Material>("shared", "pbr", program_handle);
        ASSERT_TRUE(assets.register_asset(first_handle, source));
        RenderWorld world{.render_items = {{.material = {first_handle, source}}}};
        const std::array views{materials.get(), second.get()};
        ASSERT_TRUE(prepare_current_programs(world, views));
        ASSERT_EQ(published(), prior);
        auto keep_prior = materials->prepare_material_update(first_handle, source->snapshot(),
            assets.resolve<ShaderProgramArtifact>(source->get_shader_program()));
        ASSERT_TRUE(keep_prior) << keep_prior.error();
        pipelines->collect_unused();
        const auto initial_count = pipelines->get_cached_pipeline_count();

        auto candidate = program(true);
        candidate->material.reset();
        ASSERT_TRUE(assets.replace_asset(program_handle, candidate));
        ASSERT_TRUE(prepare_current_programs(world, views));
        EXPECT_EQ(published(), prior);
        EXPECT_NE(messages.str().find("property layout"), std::string::npos);
        pipelines->collect_unused();
        EXPECT_EQ(pipelines->get_cached_pipeline_count(), initial_count);
        const auto rejected = messages.str();
        ASSERT_TRUE(prepare_current_programs(world, views));
        EXPECT_EQ(messages.str(), rejected);

        // 新版本恢复共同布局后，两个目标再一起接受它。
        const auto compatible = program(true);
        compatible->material->scalars.front().display_name = "Updated";
        ASSERT_TRUE(assets.replace_asset(program_handle, compatible));
        ASSERT_TRUE(prepare_current_programs(world, views));
        EXPECT_EQ(published(), compatible);
    }

    TEST_F(ProjectMaterialPublicationTest, RemovedDependencyRetriesOnceButShaderErrorsStayCached) {
        auto& assets = engine->get_asset_registry();
        const auto optional = program(true);
        ASSERT_TRUE(assets.register_asset(program_handle, optional));
        auto compatible = std::make_shared<Material>("compatible", "pbr", program_handle);
        auto missing_texture = std::make_shared<Material>("missing_texture", "pbr", program_handle);
        auto image = texture({255, 255, 255, 255});
        ASSERT_TRUE(image) << image.error();
        compatible->set_texture_property("base_color_texture", image.value());
        ASSERT_TRUE(assets.register_asset(first_handle, compatible));
        ASSERT_TRUE(assets.register_asset(second_handle, missing_texture));
        ASSERT_NO_FATAL_FAILURE(publish_material(first_handle, compatible));
        ASSERT_NO_FATAL_FAILURE(publish_material(second_handle, missing_texture));
        RenderWorld world;
        world.render_items = {{.material = {first_handle, compatible}},
            {.material = {second_handle, missing_texture}}};
        ASSERT_TRUE(prepare_current_programs(world));
        const auto required = program(false);
        ASSERT_TRUE(assets.replace_asset(program_handle, required));
        messages.str({});
        ASSERT_TRUE(prepare_current_programs(world));
        EXPECT_EQ(published(), optional);
        const auto failure = messages.str();
        ASSERT_NE(failure.find("Missing texture property"), std::string::npos);
        for(int repeat = 0; repeat < 3; ++repeat)
            ASSERT_TRUE(prepare_current_programs(world));
        EXPECT_EQ(messages.str(), failure);

        ASSERT_TRUE(assets.unregister_asset(second_handle));
        materials->remove_assets(assets.take_removals());
        EXPECT_EQ(materials->get_statistics().cached_material_versions, 1u);
        EXPECT_EQ(published(), optional);
        auto recovered = materials->prepare_material_update(first_handle, compatible->snapshot(),
            assets.resolve<ShaderProgramArtifact>(compatible->get_shader_program()));
        ASSERT_TRUE(recovered) << recovered.error();
        EXPECT_EQ(published(), required);
        std::move(recovered).value().publish();
        world.render_items.pop_back();
        ASSERT_TRUE(prepare_current_programs(world));
        EXPECT_EQ(published(), required);
        EXPECT_EQ(messages.str(), failure);

        auto invalid = program(false);
        invalid->material->textures.front().name = "unknown_texture";
        ASSERT_TRUE(assets.replace_asset(program_handle, invalid));
        ASSERT_TRUE(prepare_current_programs(world));
        EXPECT_EQ(published(), required);
        const auto shader_failure = messages.str();
        ASSERT_GT(shader_failure.size(), failure.size());
        for(int revision = 0; revision < 3; ++revision) {
            ASSERT_TRUE(compatible->set_scalar_property("metallic", 0.1f * revision));
            world.render_items.front().material.overrides =
                std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 1,
                    .material = first_handle,
                    .scalar_properties = {{"roughness", 0.2f + 0.1f * revision}}});
            ASSERT_TRUE(prepare_current_programs(world));
        }
        EXPECT_EQ(published(), required);
        EXPECT_EQ(messages.str(), shader_failure);
        ASSERT_TRUE(assets.unregister_asset(first_handle));
        materials->remove_assets(assets.take_removals());
        auto retained = materials->prepare_material_update(first_handle, compatible->snapshot(),
            assets.resolve<ShaderProgramArtifact>(compatible->get_shader_program()));
        EXPECT_FALSE(retained);
        EXPECT_EQ(published(), required);
        EXPECT_EQ(messages.str(), shader_failure);
    }

    TEST_F(
        ProjectMaterialPublicationTest, ChangedMaterialSourceAndPublishedEditsRetryDependencies) {
        auto& assets = engine->get_asset_registry();
        auto optional = program(true);
        ASSERT_TRUE(assets.register_asset(program_handle, optional));
        auto source = std::make_shared<Material>("source", "pbr", program_handle);
        auto image = texture({255, 255, 255, 255});
        ASSERT_TRUE(image) << image.error();
        RenderWorld world;
        for(int change = 0; change < 3; ++change) {
            SCOPED_TRACE(change);
            optional = program(true);
            ASSERT_TRUE(assets.replace_asset(program_handle, optional));
            source->set_texture_property("base_color_texture", nullptr);
            ASSERT_NO_FATAL_FAILURE(publish_material(first_handle, source));
            world.render_items = {{.material = {first_handle, source}}};
            ASSERT_TRUE(prepare_current_programs(world));
            const auto required = program(false);
            ASSERT_TRUE(assets.replace_asset(program_handle, required));
            ASSERT_TRUE(prepare_current_programs(world));
            EXPECT_EQ(published(), optional);

            if(change != 0)
                source = std::make_shared<Material>("replacement", "pbr", program_handle);
            source->set_texture_property("base_color_texture", image.value());
            if(change == 2) {
                auto update = materials->prepare_material_update(first_handle, source->snapshot(),
                    assets.resolve<ShaderProgramArtifact>(source->get_shader_program()));
                ASSERT_TRUE(update) << update.error();
                EXPECT_EQ(published(), optional);
                std::move(update).value().publish();
                // 不依赖下一次场景提取，显式编辑发布也会更新待重试的源。
                auto next = materials->prepare_material_update(first_handle, source->snapshot(),
                    assets.resolve<ShaderProgramArtifact>(source->get_shader_program()));
                ASSERT_TRUE(next) << next.error();
                EXPECT_EQ(published(), required);
            }
            world.render_items.front().material.resource = source;
            ASSERT_TRUE(prepare_current_programs(world));
            EXPECT_EQ(published(), required);
        }
    }

    TEST_F(MaterialRenderingTest, FailedFramePreparationCannotReuseAcquiredFrame) {
        auto& renderer = engine->get_renderer();
        auto prepared = render_frontend->prepare_frame();
        ASSERT_TRUE(prepared);
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        RenderScene scene;
        scene.post_process.exposure = -1;
        const auto result = render_frontend->render(scene);
        ASSERT_FALSE(result);
        ASSERT_TRUE(renderer.get_frame_scheduler().is_recording_frame());
        renderer.wait_idle();
        renderer.prepare_shutdown();
        renderer.wait_idle();
        EXPECT_FALSE(render_frontend->prepare_frame());
        EXPECT_FALSE(renderer.render_frame({}));
    }

    TEST_F(MaterialRenderingTest, ShutdownRejectsResourceChangesWithoutAnActiveFrame) {
        auto& renderer = engine->get_renderer();
        auto& scene = renderer.get_scene_renderer();
        const auto* target = &scene.get_render_target();
        auto material = std::make_shared<Material>("shutdown test", "pbr");
        ASSERT_FALSE(renderer.get_frame_scheduler().is_frame_active());
        renderer.prepare_shutdown();
        const auto expect_shutdown = [](const auto& result) {
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error().message, "Renderer is shutting down");
        };
        expect_shutdown(renderer.enable_offscreen_rendering({160, 120}));
        expect_shutdown(renderer.reload_material_shaders({}));
        expect_shutdown(prepare_material_update(AssetHandle(72), material));
        EXPECT_EQ(&scene.get_render_target(), target);
        EXPECT_FALSE(scene.is_offscreen());
        EXPECT_EQ(scene.get_material_statistics().cached_material_versions, 0u);
        renderer.set_overlay({});
        renderer.cancel_viewport_pick();
        renderer.wait_idle();
        renderer.prepare_shutdown();
        renderer.wait_idle();
    }

    TEST_F(MaterialRenderingTest, RejectsMissingFrameSlotsAndCanCreateAfterFailure) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto& resources = engine->get_render_resources();
        MaterialPrograms programs;
        Config config;
        config.render.max_frames_in_flight = 0;
        auto scene = SceneRenderer::create(
            device, programs, resources, config.vulkan, config.render, Math::Vec2u{16, 16});
        ASSERT_FALSE(scene);
        config.render.max_frames_in_flight = 2;
        EXPECT_FALSE(SceneRenderer::create(
            device, programs, resources, config.vulkan, config.render, Math::Vec2u{0, 16}));
        scene = SceneRenderer::create(
            device, programs, resources, config.vulkan, config.render, Math::Vec2u{16, 16});
        ASSERT_TRUE(scene) << scene.error();
        EXPECT_EQ(scene.value()->get_render_target().get_size(), Math::Vec2u(16, 16));
        EXPECT_TRUE(scene.value()->is_offscreen());
        const auto color = Attachment::get_color_attachment(Format::R8G8B8A8_UNORM);
        auto pass_result = RenderPass::create(device,
            {color, Attachment::get_depth_attachment(Format::D32_SFLOAT)},
            {RenderSubPass{{}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}}},
            Format::R8G8B8A8_UNORM);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        PipelineManager pipelines(device, pass);
        auto materials =
            MaterialRenderer::create(device, pipelines, resources, 0, SampleCount::Count1);
        ASSERT_FALSE(materials);
        EXPECT_FALSE(materials.error().result.has_value());
        auto debug = DebugRenderer::create(device, pipelines, 0, SampleCount::Count1);
        ASSERT_FALSE(debug);
        EXPECT_FALSE(debug.error().result.has_value());
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);

        materials = MaterialRenderer::create(device, pipelines, resources, 2, SampleCount::Count1);
        ASSERT_TRUE(materials) << materials.error();
        debug = DebugRenderer::create(device, pipelines, 2, SampleCount::Count1);
        ASSERT_TRUE(debug) << debug.error();
        const auto initial_pipelines = pipelines.get_cached_pipeline_count();
        EXPECT_GT(initial_pipelines, 0u);
        MaterialShaders invalid{{"pbr", {{std::begin(PBR_VERT), std::end(PBR_VERT)},
                                            {std::begin(PBR_FRAG), std::end(PBR_FRAG)}}},
            {"unlit_color", {{std::begin(UNLIT_COLOR_VERT), std::end(UNLIT_COLOR_VERT)}, {0}}}};
        EXPECT_FALSE(materials.value()->reload_shaders(pipelines, invalid, SampleCount::Count1));
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), initial_pipelines);
        materials.value().reset();
        debug.value().reset();
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);
    }

    TEST_F(MaterialRenderingTest, MaterialPreparationRequiresFrameBoundary) {
        auto& renderer = engine->get_renderer();
        const auto material = std::make_shared<Material>("edit", "pbr");
        auto candidate = prepare_material_update(AssetHandle(1), material);
        ASSERT_TRUE(candidate) << candidate.error();
        auto frame = render_frontend->prepare_frame();
        ASSERT_TRUE(frame);
        ASSERT_EQ(frame.value(), Renderer::FramePreparation::Ready);
        auto rejected = prepare_material_update(AssetHandle(1), material);
        EXPECT_FALSE(rejected);
        EXPECT_FALSE(std::move(candidate).value().publish());
        const std::array failed_programs{std::string("pbr")};
        EXPECT_FALSE(renderer.invalidate_material_shaders(failed_programs));
        EXPECT_TRUE(renderer.render_frame({}));
        candidate = prepare_material_update(AssetHandle(1), material);
        ASSERT_TRUE(candidate) << candidate.error();
        ASSERT_TRUE(std::move(candidate).value().publish());
        EXPECT_EQ(
            renderer.get_scene_renderer().get_material_statistics().cached_material_versions, 1u);

        candidate = prepare_material_update(AssetHandle(1), material);
        ASSERT_TRUE(candidate) << candidate.error();
        engine->prepare_shutdown();
        EXPECT_FALSE(std::move(candidate).value().publish());
        EXPECT_FALSE(renderer.reload_material_shaders({}));
        EXPECT_FALSE(renderer.invalidate_material_shaders(failed_programs));
    }

    TEST_F(MaterialRenderingTest, InvalidOffscreenExtentKeepsCurrentTarget) {
        auto& renderer = engine->get_renderer();
        auto* previous = &renderer.get_scene_renderer().get_render_target();
        const auto size = previous->get_size();
        auto result = renderer.enable_offscreen_rendering({0, 32});
        ASSERT_FALSE(result);
        EXPECT_FALSE(result.error().result.has_value());
        EXPECT_EQ(&renderer.get_scene_renderer().get_render_target(), previous);
        EXPECT_EQ(previous->get_size(), size);
        {
            const auto preparation = render_frontend->prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_EQ(preparation.value(), Renderer::FramePreparation::Ready);
        }
        EXPECT_TRUE(renderer.render_frame({}));
    }

    TEST_F(MaterialRenderingTest, ShaderPublicationRejectsFixedContractChangesAndActiveFrames) {
        auto& renderer = engine->get_renderer();
        const MaterialShaders original{{"pbr", {{std::begin(PBR_VERT), std::end(PBR_VERT)},
                                                   {std::begin(PBR_FRAG), std::end(PBR_FRAG)}}},
            {"unlit_color", {{std::begin(UNLIT_COLOR_VERT), std::end(UNLIT_COLOR_VERT)},
                                {std::begin(UNLIT_COLOR_FRAG), std::end(UNLIT_COLOR_FRAG)}}}};
        auto source = read_text_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/common/mesh_vertex.glsl");
        ASSERT_TRUE(source) << source.error();
        const auto frame = read_text_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/common/frame.glsl");
        ASSERT_TRUE(frame) << frame.error();
        const std::string include = "#include \"frame.glsl\"";
        const auto include_offset = source.value().find(include);
        ASSERT_NE(include_offset, std::string::npos);
        source.value().replace(include_offset, include.size(), frame.value());
        const auto bend = read_text_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/common/vertex_bend.glsl");
        ASSERT_TRUE(bend) << bend.error();
        const std::string bend_include = "#include \"vertex_bend.glsl\"";
        const auto bend_offset = source.value().find(bend_include);
        ASSERT_NE(bend_offset, std::string::npos);
        source.value().replace(bend_offset, bend_include.size(), bend.value());
        TemporaryDirectory directory;
        for(const auto& [from, to] :
            {std::pair{"mat4 view;\n    mat4 projection;", "mat4 projection;\n    mat4 view;"},
                {"binding = 0, std140", "binding = 0, std140, row_major"},
                {"layout(push_constant)", "layout(push_constant, row_major)"}}) {
            SCOPED_TRACE(to);
            auto modified = source.value();
            const auto offset = modified.find(from);
            ASSERT_NE(offset, std::string::npos);
            modified.replace(offset, std::string_view(from).size(), to);
            const auto path = directory.path() / "modified.vert";
            ASSERT_TRUE(write_text_file_atomic(path, "#version 450\n" + modified));
            const auto compiled = ShaderCompiler::compile({.source = path});
            ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
            auto candidate = original;
            candidate.at("unlit_color").vertex = compiled.words;
            const auto rejected = renderer.reload_material_shaders(candidate);
            ASSERT_FALSE(rejected);
            EXPECT_FALSE(rejected.error().result.has_value());
            EXPECT_NE(rejected.error().message.find("fixed resource layout"), std::string::npos);

            auto& device = renderer.get_render_context().get_device();
            auto pass = RenderPass::create(device,
                {Attachment::get_color_attachment(Format::R8G8B8A8_UNORM),
                    Attachment::get_depth_attachment(Format::D32_SFLOAT)},
                {RenderSubPass{
                    {}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}}},
                Format::R8G8B8A8_UNORM);
            ASSERT_TRUE(pass) << pass.error();
            PipelineManager pipelines(device, *pass.value());
            auto rebuilt = MaterialRenderer::create(device, pipelines,
                engine->get_render_resources(), 2, SampleCount::Count1, &candidate);
            EXPECT_FALSE(rebuilt);
            EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);
        }
        ASSERT_TRUE(renderer.reload_material_shaders(original));
        {
            const auto preparation = render_frontend->prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_EQ(preparation.value(), Renderer::FramePreparation::Ready);
        }
        EXPECT_TRUE(renderer.get_frame_scheduler().is_frame_active());
        const auto rejected = renderer.reload_material_shaders(original);
        EXPECT_FALSE(rejected);
        if(!rejected)
            EXPECT_NE(rejected.error().message.find("frame boundary"), std::string::npos);
        EXPECT_TRUE(renderer.render_frame({}));
        EXPECT_FALSE(renderer.get_frame_scheduler().is_frame_active());
        ASSERT_TRUE(renderer.reload_material_shaders(original));
    }

    TEST_F(MaterialRenderingTest, OverlayRebuildFailureReturnsErrorWithoutThrowing) {
        auto& renderer = engine->get_renderer();
        bool released = false;
        renderer.set_overlay({.release = [&] { released = true; },
            .rebuild =
                [&](const SwapchainCompatibility&) {
                    EXPECT_TRUE(released);
                    return Result<void, GraphicsError>::failure({"overlay rebuild failed"});
                }});
        renderer.request_swapchain_recreation();
        EXPECT_FALSE(released);
        const auto preparation = render_frontend->prepare_frame();
        ASSERT_FALSE(preparation);
        EXPECT_EQ(preparation.error().message, "overlay rebuild failed");
        renderer.set_overlay({});
    }

    TEST_F(MaterialRenderingTest, ReadsPixelsFromTwoLayoutsAndIsolatedOverridesAcrossReloads) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto color = Attachment::get_color_attachment(Format::R8G8B8A8_UNORM);
        color.description.store_op = AttachmentStoreOp::Store;
        color.description.final_layout = ImageLayout::TransferSrcOptimal;
        color.usage |= ImageUsage::CopySrc;
        auto pass_result = RenderPass::create(device,
            {color, Attachment::get_depth_attachment(Format::D32_SFLOAT)},
            {RenderSubPass{{}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}}},
            Format::R8G8B8A8_UNORM);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        auto target_result = RenderTarget::try_create_multi_target(device, pass, {64, 32}, 2);
        ASSERT_TRUE(target_result) << target_result.error();
        auto target = std::move(target_result).value();
        target->set_clear_value(ClearValue(Math::Vec4(0, 0, 0, 1)));
        PipelineManager pipelines(device, pass);
        auto material_result = MaterialRenderer::create(
            device, pipelines, engine->get_render_resources(), 2, SampleCount::Count1);
        ASSERT_TRUE(material_result) << material_result.error();
        auto materials = std::move(material_result).value();
        const auto shadow_input = texture({255, 255, 255, 255});
        ASSERT_TRUE(shadow_input) << shadow_input.error();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        const MeshData mesh_data{
            .vertices = {{{-0.4f, -0.8f, 0.5f}, {}, {0, 0, 1}},
                {{0.4f, -0.8f, 0.5f}, {}, {0, 0, 1}}, {{0.4f, 0.8f, 0.5f}, {}, {0, 0, 1}},
                {{-0.4f, 0.8f, 0.5f}, {}, {0, 0, 1}}},
            .indices = {0, 1, 2, 2, 3, 0}};
        const auto mesh_result = engine->get_render_resources().try_create_mesh(mesh_data);
        ASSERT_TRUE(mesh_result) << mesh_result.error();
        const auto mesh = mesh_result.value();
        const auto textured = std::make_shared<Material>("textured", "pbr");
        {
            auto red = texture({255, 0, 0, 255});
            ASSERT_TRUE(red) << red.error();
            textured->set_texture_property("base_color_texture", std::move(red).value());
        }
        const std::weak_ptr<Texture> retired = textured->get_texture_property("base_color_texture");
        EXPECT_TRUE(textured->set_scalar_property("roughness", 1));
        EXPECT_TRUE(textured->set_vector_property("base_color", {1, 0.5f, 0.5f, 1}));
        const auto solid = std::make_shared<Material>("solid", "unlit_color");
        EXPECT_TRUE(solid->set_vector_property("color", {0.2f, 0.8f, 0.4f, 1}));
        EXPECT_TRUE(solid->set_scalar_property("intensity", 0.5f));
        const auto solid_revision = solid->get_revision();
        const auto overridden =
            std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 77,
                .material = AssetHandle{2},
                .scalar_properties = {{"intensity", 0.25f}},
                .vector_properties = {{"color", {1, 0.2f, 0.2f, 1}}}});
        std::vector<ResolvedRenderItem> items{
            {.model_matrix = Math::translate(Math::Mat4(1), {-0.5f, 0, 0}),
                .mesh = mesh,
                .material = {AssetHandle(1), textured}},
            {.model_matrix = Math::translate(Math::Mat4(1), {0.25f, 0, 0})
                             * Math::scale(Math::Mat4(1), {0.5f, 1, 1}),
                .mesh = mesh,
                .material = {AssetHandle(2), solid}},
            {.model_matrix = Math::translate(Math::Mat4(1), {0.75f, 0, 0})
                             * Math::scale(Math::Mat4(1), {0.5f, 1, 1}),
                .mesh = mesh,
                .material = {AssetHandle(2), solid, overridden}}};

        RenderGpuTest::Readback readback(
            device, context.get_context().get_physical_device(), 4 * 64 * 32 * 4);
        ASSERT_TRUE(readback.get());
        TemporaryDirectory sources;
        const auto shader_directory =
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/material";
        auto solid_source = material_fragment_source("unlit_surface.glsl");
        ASSERT_TRUE(solid_source) << solid_source.error();
        const auto expression = solid_source.value().rfind("material.intensity");
        ASSERT_NE(expression, std::string::npos);
        solid_source.value().insert(expression, "0.5 * ");
        ASSERT_TRUE(write_text_file_atomic(sources.path() / "solid.frag", solid_source.value()));
        const auto compiled = ShaderCompiler::compile({.source = sources.path() / "solid.frag",
            .stage = ShaderStage::Fragment,
            .include_directories = {shader_directory}});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        const MaterialShaders updated{{"pbr", {{std::begin(PBR_VERT), std::end(PBR_VERT)},
                                                  {std::begin(PBR_FRAG), std::end(PBR_FRAG)}}},
            {"unlit_color",
                {{std::begin(UNLIT_COLOR_VERT), std::end(UNLIT_COLOR_VERT)}, compiled.words}}};
        auto textured_source = material_fragment_source("pbr_surface.glsl");
        ASSERT_TRUE(textured_source) << textured_source.error();
        auto relocated_source = textured_source.value();
        const auto assignment = textured_source.value().find("color = ");
        ASSERT_NE(assignment, std::string::npos);
        textured_source.value().insert(assignment + std::string_view("color = ").size(), "0.5 * ");
        ASSERT_TRUE(
            write_text_file_atomic(sources.path() / "textured.frag", textured_source.value()));
        const auto changed_textured =
            ShaderCompiler::compile({.source = sources.path() / "textured.frag",
                .stage = ShaderStage::Fragment,
                .include_directories = {shader_directory}});
        ASSERT_TRUE(changed_textured.succeeded()) << changed_textured.diagnostics;
        auto broken_solid = solid_source.value();
        broken_solid.insert(
            broken_solid.find("void main"), "layout(location=4) in vec4 missing;\n");
        broken_solid.insert(
            broken_solid.find("color = ") + std::string_view("color = ").size(), "missing * ");
        ASSERT_TRUE(write_text_file_atomic(sources.path() / "broken.frag", broken_solid));
        const auto broken = ShaderCompiler::compile({.source = sources.path() / "broken.frag",
            .stage = ShaderStage::Fragment,
            .include_directories = {shader_directory}});
        ASSERT_TRUE(broken.succeeded()) << broken.diagnostics;
        EXPECT_FALSE(materials->reload_shaders(pipelines,
            {{"pbr", {updated.at("pbr").vertex, changed_textured.words}},
                {"unlit_color", {updated.at("unlit_color").vertex, broken.words}}},
            SampleCount::Count1));
        ASSERT_TRUE(write_text_file_atomic(sources.path() / "layout-solid.frag",
            "#version 450\nlayout(location=0) out vec4 color;"
            "layout(set=1,binding=5,std140) uniform MaterialData {float intensity;"
            "layout(offset=32) vec4 color;} material;"
            "void main(){color=vec4(material.color.rgb*material.intensity*0.5,material.color.a);}"));
        for(const auto& [from, to] : {std::pair{"binding = 0, std140", "binding = 6, std140"},
                {"vec4 base_color;\n    float metallic;\n    float roughness;",
                    "float metallic;\n    float roughness;\n    layout(offset=32) vec4 base_color;"},
                {"binding = 1) uniform sampler2D", "binding = 7) uniform sampler2D"}}) {
            const auto offset = relocated_source.find(from);
            ASSERT_NE(offset, std::string::npos);
            relocated_source.replace(offset, std::string_view(from).size(), to);
        }
        ASSERT_TRUE(
            write_text_file_atomic(sources.path() / "layout-textured.frag", relocated_source));
        const auto layout_solid = ShaderCompiler::compile(
            {.source = sources.path() / "layout-solid.frag", .stage = ShaderStage::Fragment});
        const auto layout_textured =
            ShaderCompiler::compile({.source = sources.path() / "layout-textured.frag",
                .stage = ShaderStage::Fragment,
                .include_directories = {shader_directory}});
        ASSERT_TRUE(layout_solid.succeeded()) << layout_solid.diagnostics;
        ASSERT_TRUE(layout_textured.succeeded()) << layout_textured.diagnostics;
        const MaterialShaders relocated{{"pbr", {updated.at("pbr").vertex, layout_textured.words}},
            {"unlit_color", {updated.at("unlit_color").vertex, layout_solid.words}}};
        // 第一条候选有效、第二条失败，随后读回的纹理材质仍应使用原 Shader。
        context.wait_idle();
        engine->get_render_resources().collect_completed_uploads();
        pipelines.collect_unused();
        const auto initial_pipelines = pipelines.get_cached_pipeline_count();
        const auto lighting = LightingData::prepare(std::array{RenderLight{.intensity = Math::PI}});
        const RenderGpuTest::FrameWait wait{device, frames};
        for(int iteration = 0; iteration < 4; ++iteration) {
            if(iteration == 1) {
                auto blue = texture({0, 0, 255, 255});
                ASSERT_TRUE(blue) << blue.error();
                textured->set_texture_property("base_color_texture", std::move(blue).value());
                items.back().material.overrides =
                    std::make_shared<const MaterialOverrides>(MaterialOverrides{.instance_id = 77,
                        .material = AssetHandle{2},
                        .scalar_properties = {{"intensity", 0.5f}},
                        .vector_properties = {{"color", {0, 0.4f, 1, 1}}}});
                // 共享资产不变；替换单实体覆盖和 Shader，旧槽位此时尚未回收。
                const auto published = materials->reload_shaders(
                    pipelines, {{"unlit_color", updated.at("unlit_color")}}, SampleCount::Count1);
                ASSERT_TRUE(published) << published.error();
                EXPECT_EQ(published.value().material_versions, 2u);
                EXPECT_EQ(published.value().material_bindings, 0u);
            }
            if(iteration == 2) {
                const auto published =
                    materials->reload_shaders(pipelines, relocated, SampleCount::Count1);
                ASSERT_TRUE(published) << published.error();
                EXPECT_EQ(published.value().material_versions, 3u);
                EXPECT_EQ(published.value().material_bindings, 3u);
                for(const auto& layout : materials->get_material_layouts()) {
                    if(layout->get_name() != "pbr" && layout->get_name() != "unlit_color") {
                        EXPECT_EQ(layout, MaterialLayout::find_builtin(layout->get_name()));
                        continue;
                    }
                    EXPECT_EQ(layout->get_parameter_size(),
                        MaterialLayout::find_builtin(layout->get_name())->get_parameter_size()
                            + (layout->get_name() == "pbr" ? 32u : 16u));
                    if(layout->get_name() == "pbr") {
                        ASSERT_EQ(layout->get_textures().size(), 5u);
                        EXPECT_EQ(layout->get_textures()[0].name, "base_color_texture");
                        EXPECT_EQ(layout->get_textures()[0].binding, 7u);
                        EXPECT_TRUE(layout->get_textures()[0].optional);
                    }
                }
            }
            if(iteration == 3) {
                const auto before = materials->get_material_layouts();
                const auto repeated =
                    materials->reload_shaders(pipelines, relocated, SampleCount::Count1);
                ASSERT_TRUE(repeated) << repeated.error();
                EXPECT_EQ(repeated.value().pipelines, 0u);
                EXPECT_EQ(repeated.value().material_versions, 0u);
                EXPECT_EQ(repeated.value().material_bindings, 0u);
                EXPECT_EQ(materials->get_material_layouts(), before);
                items.back().material.overrides.reset();
            }
            frames.wait_for_current_slot();
            const auto slot = frames.get_current_frame_slot_index();
            frames.begin_frame(slot);
            auto& command = frames.get_current_command_buffer();
            command.begin(Flags<CommandBuffer::Usage>(CommandBuffer::Usage::OneTimeSubmit));
            target->begin_render_target(command, slot);
            command.set_viewport(Graphics::get_viewport(64, 32));
            command.set_scissor(Graphics::get_scissor(64, 32));
            std::vector<ResolvedRenderItem> instances;
            for(const auto& item : items) {
                for(float y : {-0.38f, 0.38f}) {
                    auto copy = item;
                    copy.model_matrix *= Math::translate(Math::Mat4(1), {0, y, 0})
                                         * Math::scale(Math::Mat4(1), {1, 0.5f, 1.3f});
                    instances.push_back(std::move(copy));
                }
            }
            const RenderWorld draw_world{.render_items = std::move(instances)};
            const RenderSubmission draw_submission{
                draw_world, ViewProjectMatrix{.view = Math::Mat4(1), .projection = Math::Mat4(1)}};
            RenderGeometry geometry;
            geometry.prepare(draw_world);
            auto waits = materials->view_bindings().prepare(
                frames, draw_submission, lighting, shadow_input.value()->get_image_view());
            ASSERT_TRUE(waits) << waits.error();
            ASSERT_TRUE(materials->prepare_draws(frames, draw_submission, geometry));
            materials->record_opaque(frames, waits.value());
            materials->record_transparent(
                frames, 0, materials->transparent_depths().size(), waits.value());
            materials->clear_draws();
            EXPECT_EQ(materials->get_statistics().drawn_instances, 6u);
            EXPECT_EQ(materials->get_statistics().draw_calls, iteration == 3 ? 2u : 3u);
            EXPECT_EQ(materials->get_statistics().instanced_draw_calls,
                materials->get_statistics().draw_calls);
            target->end_render_target(command);
            vk::BufferImageCopy copy;
            copy.bufferOffset = iteration * 64 * 32 * 4;
            copy.imageSubresource =
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            copy.imageExtent = vk::Extent3D(64, 32, 1);
            command.get().copyImageToBuffer(target->get_color_view(slot)->get_image()->get(),
                vk::ImageLayout::eTransferSrcOptimal, readback.get(), copy);
            const vk::MemoryBarrier barrier(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead);
            command.get().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eHost, {}, barrier, {}, {});
            command.end();
            const auto submission = frames.submit(waits.value(), {});
            ASSERT_TRUE(submission) << submission.error();
            frames.end_frame();
            const std::array<uint32_t, 4> expected_creations{3, 2, 0, 0};
            EXPECT_EQ(materials->get_statistics().material_versions_created,
                expected_creations[iteration]);
            EXPECT_EQ(materials->get_statistics().material_bindings_created,
                expected_creations[iteration]);
            if(iteration == 1) {
                EXPECT_FALSE(retired.expired());
                pipelines.collect_unused();
                EXPECT_EQ(pipelines.get_cached_pipeline_count(), initial_pipelines + 2);
            }
            if(iteration == 2) {
                pipelines.collect_unused();
                // 固定错误材质仍持有初始 unlit 程序，独立于新的参数布局。
                EXPECT_EQ(pipelines.get_cached_pipeline_count(), initial_pipelines + 6);
            }
        }
        frames.wait_for_all_slots();
        EXPECT_TRUE(retired.expired());
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), initial_pipelines + 2);
        const auto readback_bytes = readback.read();
        const auto* all_pixels = reinterpret_cast<const uint8_t*>(readback_bytes.data());
        for(int iteration = 0; iteration < 4; ++iteration) {
            const auto* pixels = all_pixels + iteration * 64 * 32 * 4;
            const auto check = [&](uint32_t x, Math::Vec3i expected) {
                for(int channel = 0; channel < 3; ++channel) {
                    for(unsigned y : {8u, 16u, 24u})
                        EXPECT_NEAR(pixels[(y * 64 + x) * 4 + channel], expected[channel], 2)
                            << "iteration=" << iteration << " x=" << x << " y=" << y
                            << " channel=" << channel;
                }
            };
            if(iteration == 0) {
                check(16, {247, 3, 3});
                check(40, {26, 102, 51});
                check(56, {64, 13, 13});
            } else {
                check(16, {3, 3, 125});
                check(40, {13, 51, 26});
                check(56, iteration == 3 ? Math::Vec3i(13, 51, 26) : Math::Vec3i(0, 26, 64));
            }
        }
        EXPECT_EQ(solid->get_revision(), solid_revision);
        EXPECT_EQ(solid->get_vector_property("color"), Math::Vec4(0.2f, 0.8f, 0.4f, 1));
        EXPECT_EQ(solid->get_scalar_property("intensity"), 0.5f);
        EXPECT_FLOAT_EQ(overridden->scalar_properties.at("intensity"), 0.25f);
        EXPECT_EQ(overridden->vector_properties.at("color"), Math::Vec4(1, 0.2f, 0.2f, 1));
        auto rebuilt = MaterialRenderer::create(
            device, pipelines, engine->get_render_resources(), 2, SampleCount::Count1, &relocated);
        ASSERT_TRUE(rebuilt) << rebuilt.error();
        for(const auto& layout : rebuilt.value()->get_material_layouts()) {
            if(layout->get_name() == "pbr" || layout->get_name() == "unlit_color")
                EXPECT_EQ(layout->get_parameter_size(),
                    MaterialLayout::find_builtin(layout->get_name())->get_parameter_size()
                        + (layout->get_name() == "pbr" ? 32u : 16u));
            else
                EXPECT_EQ(layout, MaterialLayout::find_builtin(layout->get_name()));
        }
        rebuilt.value().reset();
        materials.reset();
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);
    }
}
