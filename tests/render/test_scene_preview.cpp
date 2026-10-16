#include "support/render_graph_gpu_fixture.h"
#include "render/scene/scene_preview.h"
#include "assets/reflection_bake.h"
#include "assets/irradiance_bake.h"
#include "render/resource/irradiance.h"
#include "assets/editor_assets.h"
#include "asset/serialization/metadata_serializer.h"
#include "asset/serialization/material_serializer.h"
#include "scene/command_history.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "asset/project_prepare.h"
#include "core/project.h"

#include <thread>

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, ReflectionBakePublishesAnUndoableEnvironmentAndRejectsStaleCapture) {
        TemporaryDirectory directory;
        ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto mesh_path = paths.assets() / "cube.gltf";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            mesh_path);
        const AssetHandle mesh(80301);
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = mesh, .type = AssetType::Mesh}, metadata_path(mesh_path)));
        ASSERT_TRUE(MaterialSerializer{}.save(
            {.template_name = "unlit_color", .vector_properties = {{"color", {4, 2, 1, 1}}}},
            paths.assets() / "wall.mat"));
        TaskScheduler scheduler(1);
        auto& registry = engine->get_asset_registry();
        CometEditor::EditorAssets assets(
            paths, registry, engine->get_render_resource_factory(), scheduler);
        ASSERT_TRUE(assets.refresh().succeeded());
        for(std::size_t i = 0; i <= assets.database().size(); ++i) {
            ASSERT_TRUE(assets.update());
            scheduler.wait_idle();
        }
        Scene scene;
        auto surface = scene.create_entity("surface");
        surface.add_component<MeshRendererComponent>(
            MeshRendererComponent{mesh, assets.database().find("wall.mat")->handle});
        surface.edit_transform([](auto& value) { value.translation.z = -3; });
        auto probe = scene.create_entity("probe");
        probe.add_component<ReflectionProbeComponent>(
            ReflectionProbeComponent{.capture_resolution = 16});
        auto environment = scene.get_environment();
        environment.background_color = {4, 2, 1};
        ASSERT_TRUE(scene.set_environment(environment));
        auto components = create_scene_component_registry();
        auto prepared = assets.prepare_scene(scene, components);
        ASSERT_TRUE(prepared) << prepared.error().message;
        CometEditor::CommandHistory history;
        history.bind_scene(&scene);
        CometEditor::ReflectionBake bake(engine->get_renderer(), assets, registry);
        auto started = bake.start(scene, probe.get_uuid(), history);
        ASSERT_TRUE(started) << started.error();
        ASSERT_TRUE(registry.register_asset(
            AssetHandle(883002), std::make_shared<Material>("unrelated", "unlit_color")));
        for(int i = 0; i < 10000 && bake.busy(); ++i) {
            auto updated = bake.update(scene, history);
            ASSERT_TRUE(updated) << updated.error();
            if(bake.busy())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_FALSE(bake.busy());
        const auto captured = probe.get_component<ReflectionProbeComponent>();
        ASSERT_TRUE(captured.environment);
        ASSERT_FALSE(captured.bake_fingerprint.empty());
        const auto* record = assets.database().find(captured.environment);
        ASSERT_TRUE(record);
        EXPECT_EQ(record->type, AssetType::Environment);
        EXPECT_EQ(record->path.parent_path(), "baked");
        auto imported = EnvironmentImporter{}.import(paths.assets() / record->path);
        ASSERT_TRUE(imported) << imported.error();
        EXPECT_EQ(imported.value().background.width, 16);
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_EQ(bake.status(scene, probe.get_uuid()), "烘焙输入未变化");
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(probe.get_component<ReflectionProbeComponent>().environment);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(probe.get_component<ReflectionProbeComponent>(), captured);
        ASSERT_TRUE(bake.start(scene, probe.get_uuid(), history));
        surface.edit_transform([](auto& value) { value.translation.x += 1; });
        bool rejected = false;
        for(int i = 0; i < 10000 && bake.busy(); ++i) {
            auto updated = bake.update(scene, history);
            if(!updated)
                rejected = true;
            if(bake.busy())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EXPECT_TRUE(rejected);
        EXPECT_EQ(probe.get_component<ReflectionProbeComponent>(), captured);
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_NE(bake.status(scene, probe.get_uuid()).find("过期"), std::string::npos);
        ASSERT_TRUE(bake.start(scene, probe.get_uuid(), history));
        Scene replacement;
        history.bind_scene(&replacement);
        ASSERT_TRUE(bake.update(replacement, history));
        EXPECT_FALSE(bake.busy());
        surface.edit_transform([](auto& value) { value.translation.x = 0; });
        ASSERT_TRUE(
            SceneSerializer(components).save(scene, (paths.assets() / "startup.scene").string()));
        ASSERT_TRUE(write_text_file_atomic(paths.root() / "project.json", R"({
            "version":2,"id":"00000000-0000-4000-8000-000000000001",
            "name":"Reflection export","startup_scene":"startup.scene",
            "input_contexts":[],"input_actions":[]})"));
        auto project = Project::load(paths.root());
        ASSERT_TRUE(project) << project.error();
        TemporaryDirectory exported;
        const ProjectPaths products(exported.path() / "game");
        auto published = export_project_assets(project.value(), products.root());
        ASSERT_TRUE(published) << published.error();
        std::filesystem::remove_all(paths.root());
        AssetRegistry product_registry;
        TaskScheduler product_scheduler(1);
        AssetManager product_assets(
            products, product_registry, engine->get_render_resource_factory(), product_scheduler);
        ASSERT_TRUE(product_assets.scan().succeeded());
        auto loaded = product_assets.load_environment(captured.environment);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_TRUE(product_assets.get_database().is_read_only());
        EXPECT_FALSE(std::filesystem::exists(products.cache()));
        ASSERT_TRUE(
            SceneSerializer(components).load((products.assets() / "startup.scene").string()));
    }

    TEST_F(RenderGraphGpuTest, EditorGiBakeExportsAndLightsPixelsWithoutSourceFiles) {
        TemporaryDirectory directory;
        ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto mesh_path = paths.assets() / "cube.gltf";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            mesh_path);
        const AssetHandle mesh(80321);
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = mesh, .type = AssetType::Mesh}, metadata_path(mesh_path)));
        ASSERT_TRUE(MaterialSerializer{}.save(
            {.template_name = "unlit_color", .vector_properties = {{"color", {4, 2, 1, 1}}}},
            paths.assets() / "wall.mat"));
        TaskScheduler scheduler(1);
        auto& registry = engine->get_asset_registry();
        CometEditor::EditorAssets assets(
            paths, registry, engine->get_render_resource_factory(), scheduler);
        ASSERT_TRUE(assets.refresh().succeeded());
        for(std::size_t i = 0; i <= assets.database().size(); ++i) {
            ASSERT_TRUE(assets.update());
            scheduler.wait_idle();
        }
        Scene scene;
        auto wall = scene.create_entity("emitter");
        wall.add_component<MeshRendererComponent>(
            MeshRendererComponent{mesh, assets.database().find("wall.mat")->handle});
        wall.edit_transform([](auto& value) { value.translation.z = 3; });
        auto region = scene.create_entity("GI");
        region.add_component<IrradianceVolumeComponent>(
            IrradianceVolumeComponent{.half_extents = {1, 1, 1}, .ray_samples = 512});
        CometEditor::CommandHistory history;
        history.bind_scene(&scene);
        CometEditor::IrradianceBake bake(assets, registry, scheduler);
        ASSERT_TRUE(bake.start(scene, region.get_uuid(), history));
        scheduler.wait_idle();
        auto updated = bake.update(scene, history);
        ASSERT_TRUE(updated) << updated.error();
        ASSERT_TRUE(updated.value());
        const auto handle = region.get_component<IrradianceVolumeComponent>().irradiance;
        auto data = IrradianceData::load(paths.assets() / assets.database().find(handle)->path);
        ASSERT_TRUE(data);
        const auto components = create_scene_component_registry();
        ASSERT_TRUE(
            SceneSerializer(components).save(scene, (paths.assets() / "startup.scene").string()));
        ASSERT_TRUE(write_text_file_atomic(paths.root() / "project.json", R"({
            "version":2,"id":"00000000-0000-4000-8000-000000000002",
            "name":"GI export","startup_scene":"startup.scene",
            "input_contexts":[],"input_actions":[]})"));
        auto project = Project::load(paths.root());
        ASSERT_TRUE(project);
        TemporaryDirectory exported;
        const ProjectPaths products(exported.path() / "game");
        auto published = export_project_assets(project.value(), products.root());
        ASSERT_TRUE(published) << published.error();
        std::filesystem::remove_all(paths.root());
        AssetRegistry product_registry;
        TaskScheduler product_scheduler(1);
        AssetManager product_assets(
            products, product_registry, engine->get_render_resource_factory(), product_scheduler);
        ASSERT_TRUE(product_assets.scan().succeeded());
        auto loaded = product_assets.load_irradiance(handle);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_FALSE(std::filesystem::exists(products.cache()));
        auto reopened =
            SceneSerializer(components).load((products.assets() / "startup.scene").string());
        ASSERT_TRUE(reopened);
        EXPECT_EQ(reopened.value()
                      ->find_entity(region.get_uuid())
                      .get_component<IrradianceVolumeComponent>()
                      .irradiance,
            handle);
        auto world = std::make_shared<RenderWorld>();
        auto material = std::make_shared<Material>("receiver", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {.8f, .8f, .8f, 1}));
        world->render_items.push_back(
            {.entity_id = 1, .mesh = lit_quad(), .material = {AssetHandle(80322), material}});
        ScenePreview preview(engine->get_renderer(), {33, 33});
        const RenderCamera camera{.view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
            .projection = RenderCamera::Projection::Orthographic,
            .orthographic_height = 2};
        const auto irradiance = data.value().sample({0, 0, .5f}, {0, 0, 1});
        ASSERT_TRUE(irradiance);
        EXPECT_GT(irradiance->r, .01f);
        for(int enabled = 0; enabled < 2; ++enabled) {
            if(enabled)
                world->irradiance_volumes.push_back({{2, handle}, loaded.value(), 1});
            ASSERT_TRUE(preview.submit(world, camera, ScenePreview::Output::LinearHdr));
            std::optional<TextureData> image;
            for(int i = 0; i < 10000 && !image; ++i) {
                auto polled = preview.poll();
                ASSERT_TRUE(polled) << polled.error().message;
                image = std::move(polled).value();
                if(!image)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(image);
            glm::u16vec4 packed;
            std::memcpy(&packed, image->pixels.data() + (16 * 33 + 16) * 8, 8);
            const auto pixel = glm::unpackHalf(packed);
            for(int channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(
                    pixel[channel], enabled ? (*irradiance)[channel] * .8f * .96f : 0, .003f);
        }
    }

    TEST_F(RenderGraphGpuTest, CubemapCaptureKeepsHdrFaceOrientationAndOneContentSnapshot) {
        ScenePreview capture(engine->get_renderer(), {32, 32});
        auto world = std::make_shared<RenderWorld>();
        world->environment.fog_enabled = true;
        world->environment.fog_start = 0;
        world->environment.fog_end = 1;
        world->environment.fog_color = {100, 100, 100};
        world->post_process.exposure = 0.1f;
        world->post_process.bloom_enabled = true;
        world->post_process.ao_enabled = true;
        const std::array<Math::Vec3, 6> direction{
            {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        const std::array<Math::Vec3, 6> up{
            {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}}};
        const auto mesh = lit_quad();
        for(std::size_t face = 0; face < 6; ++face) {
            const auto right = Math::cross(direction[face], up[face]);
            for(int y = 0; y < 2; ++y)
                for(int x = 0; x < 2; ++x) {
                    const auto index = face * 4 + y * 2 + x;
                    auto material = std::make_shared<Material>("capture", "unlit_color");
                    ASSERT_TRUE(material->set_surface({.double_sided = true}));
                    ASSERT_TRUE(material->set_vector_property(
                        "color", {float(face + 2), float(1 + 3 * x), float(1 + 5 * y), 1}));
                    Math::Mat4 transform(1);
                    transform[0] = Math::Vec4(right * 1.5f, 0);
                    transform[1] = Math::Vec4(up[face] * 1.5f, 0);
                    transform[2] = Math::Vec4(-direction[face], 0);
                    transform[3] = Math::Vec4(direction[face] * 3.5f + right * (x ? 1.5f : -1.5f)
                                                  + up[face] * (y ? 1.5f : -1.5f),
                        1);
                    world->render_items.push_back({.model_matrix = transform,
                        .mesh = mesh,
                        .material = {.material_handle = AssetHandle(900 + index),
                            .resource = material->snapshot()}});
                }
        }
        EXPECT_FALSE(capture.submit_cubemap(world, {0, 0, 0}, 1, 0.1f));
        EXPECT_FALSE(capture.busy());
        ASSERT_TRUE(capture.submit_cubemap(world, {0, 0, 0}, 0.1f, 10));
        EXPECT_TRUE(capture.busy());
        EXPECT_FALSE(capture.submit_cubemap(world, {0, 0, 0}));
        world->render_items.clear();
        const std::array failed{std::string("unlit_color")};
        ASSERT_TRUE(engine->get_renderer().invalidate_material_shaders(failed));
        std::optional<TextureData> image;
        for(int i = 0; i < 10000 && !image; ++i) {
            auto polled = capture.poll();
            ASSERT_TRUE(polled) << polled.error();
            image = std::move(polled).value();
            if(!image)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(image);
        EXPECT_FALSE(capture.busy());
        EXPECT_EQ(image->format, Format::R16G16B16A16_SFLOAT);
        EXPECT_TRUE(image->cubemap);
        EXPECT_EQ(image->mip_levels, 1u);
        ASSERT_EQ(image->pixels.size(), 6 * 32 * 32 * 8u);
        for(std::size_t face = 0; face < 6; ++face)
            for(int y = 0; y < 2; ++y)
                for(int x = 0; x < 2; ++x) {
                    const auto pixel = (face * 32 * 32 + (8 + y * 16) * 32 + 8 + x * 16) * 8;
                    glm::u16vec4 packed;
                    std::memcpy(&packed, image->pixels.data() + pixel, 8);
                    const auto value = glm::unpackHalf(packed);
                    EXPECT_NEAR(value.r, face + 2, 0.01) << face << ":" << x << "," << y;
                    EXPECT_NEAR(value.g, 1 + 3 * x, 0.01);
                    EXPECT_NEAR(value.b, 1 + 5 * y, 0.01);
                }
        auto encoded = EnvironmentImporter::encode_hdr(*image);
        ASSERT_TRUE(encoded) << encoded.error();
        TemporaryDirectory directory;
        const auto path = directory.path() / "captured.hdr";
        ASSERT_TRUE(write_text_file_atomic(path, encoded.value()));
        auto imported = EnvironmentImporter{}.import(path);
        ASSERT_TRUE(imported) << imported.error();
        EXPECT_EQ(imported.value().background.width, 32);
        EXPECT_FALSE(imported.value().specular.pixels.empty());
        // 同一个离屏任务可再次提交；关闭时等待自己的在途工作。
        ASSERT_TRUE(capture.submit_cubemap(std::make_shared<RenderWorld>(), {0, 0, 0}));
    }
}
