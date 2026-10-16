#include "assets/irradiance_bake.h"
#include "assets/editor_assets.h"
#include "asset/artifact/texture_artifact.h"
#include "asset/default_assets.h"
#include "asset/registry.h"
#include "asset/import/import_service.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/project.h"
#include "common/file_io.h"
#include "scene/command_history.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "support/blocked_worker.h"
#include "support/render_resource_factory.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <fstream>

namespace CometEditor::Tests {
    using namespace Comet;
    class IrradianceBakeWorkflowTest: public ::testing::Test {
    protected:
        Comet::Tests::TemporaryDirectory directory;
        ProjectPaths paths{directory.path()};
        Comet::Tests::FakeRenderResourceFactory factory;
        AssetRegistry registry;
        TaskScheduler scheduler{1};
        std::unique_ptr<EditorAssets> assets;
        Scene scene;
        CommandHistory history;
        ComponentRegistry components = create_scene_component_registry();
        EntityUuid volume, surface;
        std::unique_ptr<CometEditor::IrradianceBake> bake;

        void SetUp() override {
            std::filesystem::create_directories(paths.assets());
            const auto mesh_path = paths.assets() / "cube.gltf";
            std::filesystem::copy_file(
                std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
                mesh_path);
            ASSERT_TRUE(MetadataSerializer{}.save(
                {.handle = AssetHandle(70191), .type = AssetType::Mesh}, metadata_path(mesh_path)));
            ASSERT_TRUE(MaterialSerializer{}.save(
                {.template_name = "unlit_color", .vector_properties = {{"color", {4, 2, 1, 1}}}},
                paths.assets() / "emissive.mat"));
            assets = std::make_unique<EditorAssets>(paths, registry, factory, scheduler);
            ASSERT_TRUE(assets->refresh().succeeded());
            for(std::size_t i = 0; i <= assets->database().size(); ++i) {
                ASSERT_TRUE(assets->update());
                scheduler.wait_idle();
            }
            auto object = scene.create_entity("emissive wall");
            object.add_component<MeshRendererComponent>(MeshRendererComponent{
                AssetHandle(70191), assets->database().find("emissive.mat")->handle});
            object.edit_transform([](auto& value) { value.translation.z = -3; });
            surface = object.get_uuid();
            auto region = scene.create_entity("GI");
            region.add_component<IrradianceVolumeComponent>(IrradianceVolumeComponent{
                .half_extents = {1, 1, 1}, .spacing = 1, .ray_samples = 64});
            volume = region.get_uuid();
            history.bind_scene(&scene);
            bake = std::make_unique<CometEditor::IrradianceBake>(*assets, registry, scheduler);
        }
        void TearDown() override {
            bake.reset();
            scheduler.wait_idle();
            assets.reset();
        }
        auto& component() {
            return scene.find_entity(volume).get_component<IrradianceVolumeComponent>();
        }
        auto fingerprint() {
            auto input =
                CometEditor::IrradianceBake::describe_input(scene, volume, assets->database());
            EXPECT_TRUE(input) << input.error();
            return input ? input.value().fingerprint : std::string{};
        }
        void start() {
            auto started = bake->start(scene, volume, history);
            ASSERT_TRUE(started) << started.error();
            ASSERT_TRUE(bake->busy());
        }
    };

    TEST_F(IrradianceBakeWorkflowTest, PublishesRealLightingWithUndoAndSceneReopen) {
        start();
        scheduler.wait_idle();
        EXPECT_EQ(bake->progress().completed, 8u);
        auto updated = bake->update(scene, history);
        ASSERT_TRUE(updated) << updated.error();
        ASSERT_TRUE(updated.value());
        ASSERT_FALSE(bake->busy());
        const auto baked = component();
        ASSERT_TRUE(baked.irradiance);
        const auto* record = assets->database().find(baked.irradiance);
        ASSERT_TRUE(record);
        ASSERT_EQ(record->type, AssetType::Irradiance);
        auto data = IrradianceData::load(paths.assets() / record->path);
        ASSERT_TRUE(data) << data.error();
        const auto light = data.value().sample({0, 0, 0}, {0, 0, -1});
        ASSERT_TRUE(light);
        EXPECT_GT(light->r, 0.01f);
        EXPECT_GT(light->r, light->g);
        ASSERT_TRUE(bake->last_report());
        EXPECT_EQ(bake->last_report()->statistics.active_probes, 8u);
        EXPECT_GT(bake->last_report()->input_bytes, 0u);
        EXPECT_EQ(bake->last_report()->product_bytes, 8 * sizeof(IrradianceProbe));
        EXPECT_EQ(bake->status(scene, volume), "烘焙输入未变化");
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(component().irradiance);
        ASSERT_TRUE(std::filesystem::exists(paths.assets() / record->path));
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(component(), baked);
        auto serialized = SceneSerializer(components).serialize(scene);
        ASSERT_TRUE(serialized);
        auto reopened = SceneSerializer(components).deserialize(serialized.value());
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(bake->status(*reopened.value(), volume), "烘焙输入未变化");
        // 再烘焙生成独立产物，Undo 仍能回到上一版。
        start();
        scheduler.wait_idle();
        ASSERT_TRUE(bake->update(scene, history));
        EXPECT_NE(component().irradiance, baked.irradiance);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(component(), baked);
    }

    TEST_F(IrradianceBakeWorkflowTest, FingerprintTracksBakeInputsAndIgnoresRuntimePresentation) {
        const auto original = fingerprint();
        scene.find_entity(volume).edit_component<IrradianceVolumeComponent>([](auto& value) {
            value.intensity = 3;
            value.normal_bias = 0.2f;
            value.irradiance = AssetHandle(9921);
            value.enabled = false;
        });
        auto environment = scene.get_environment();
        environment.fog_enabled = true;
        environment.background_color = {4, 1, 2};
        ASSERT_TRUE(scene.set_environment(environment));
        auto dynamic = scene.create_entity("dynamic");
        dynamic.add_component<RigidBodyComponent>();
        auto child = scene.create_entity("dynamic geometry");
        child.add_component<MeshRendererComponent>(
            MeshRendererComponent{AssetHandle(70191), DEFAULT_PBR_MATERIAL});
        ASSERT_TRUE(scene.set_parent(child, dynamic));
        EXPECT_EQ(fingerprint(), original);
        auto input = CometEditor::IrradianceBake::describe_input(scene, volume, assets->database());
        ASSERT_TRUE(input);
        EXPECT_EQ(input.value().scene.render_items.size(), 1u);
        EXPECT_TRUE(input.value().scene.irradiance_volumes.empty());
        scene.find_entity(volume).edit_component<IrradianceVolumeComponent>(
            [](auto& value) { value.bounces = 3; });
        EXPECT_NE(fingerprint(), original);
        scene.find_entity(volume).edit_component<IrradianceVolumeComponent>(
            [](auto& value) { value.bounces = 2; });
        scene.find_entity(volume).edit_transform([](auto& value) { value.translation.x = 1; });
        EXPECT_NE(fingerprint(), original);
    }

    TEST_F(
        IrradianceBakeWorkflowTest, RejectsChangedInputBeforePublishingAndPreservesPreviousResult) {
        start();
        scheduler.wait_idle();
        ASSERT_TRUE(bake->update(scene, history));
        const auto before = component();
        const auto count = assets->database().size();
        start();
        scheduler.wait_idle();
        scene.find_entity(surface).edit_transform([](auto& value) { value.translation.x = 0.5f; });
        auto result = bake->update(scene, history);
        EXPECT_FALSE(result);
        EXPECT_NE(result.error().find("变化"), std::string::npos);
        EXPECT_FALSE(bake->busy());
        EXPECT_EQ(component(), before);
        EXPECT_EQ(assets->database().size(), count);
        EXPECT_NE(bake->status(scene, volume).find("过期"), std::string::npos);
    }

    TEST_F(IrradianceBakeWorkflowTest, CancelAndSceneReplacementLeaveNoPublishedAsset) {
        const auto count = assets->database().size();
        {
            Comet::Tests::BlockedWorker gate(scheduler);
            start();
            EXPECT_EQ(bake->progress().total, 8u);
            EXPECT_FALSE(bake->start(scene, volume, history));
            bake->cancel();
            EXPECT_FALSE(bake->busy());
        }
        scheduler.wait_idle();
        EXPECT_EQ(assets->database().size(), count);
        start();
        Scene replacement;
        history.bind_scene(&replacement);
        auto result = bake->update(replacement, history);
        ASSERT_TRUE(result);
        EXPECT_FALSE(result.value());
        EXPECT_FALSE(bake->busy());
        scheduler.wait_idle();
        EXPECT_EQ(assets->database().size(), count);
        EXPECT_FALSE(component().irradiance);
    }

    TEST_F(IrradianceBakeWorkflowTest, RejectsMissingCpuProductAndInvalidGridWithoutPublishing) {
        const auto count = assets->database().size();
        std::filesystem::remove(ImportService(paths).mesh_artifact_path(AssetHandle(70191)));
        start();
        scheduler.wait_idle();
        auto result = bake->update(scene, history);
        EXPECT_FALSE(result);
        EXPECT_FALSE(component().irradiance);
        EXPECT_EQ(assets->database().size(), count);
        scene.find_entity(volume).edit_component<IrradianceVolumeComponent>(
            [](auto& value) { value.spacing = 0.01f; });
        EXPECT_FALSE(bake->start(scene, volume, history));
        EXPECT_FALSE(bake->busy());
    }

    TEST_F(IrradianceBakeWorkflowTest, ReadsTexturedModelChildrenWithoutGpuCreation) {
        auto source = read_text_file(paths.assets() / "cube.gltf");
        ASSERT_TRUE(source);
        auto text = std::move(source).value();
        const auto indices = text.find("\"indices\"");
        ASSERT_NE(indices, std::string::npos);
        text.insert(indices, "\"material\":0,");
        text.insert(text.find_last_of('}'), R"(,
            "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0,
            "materials":[{"pbrMetallicRoughness":{"metallicFactor":0,"baseColorTexture":{"index":0}},
                "emissiveFactor":[1,0.5,0.25],"emissiveTexture":{"index":0}}],
            "images":[{"uri":"paint.png"}],"textures":[{"source":0,"sampler":0}],
            "samplers":[{"magFilter":9728,"minFilter":9728,"wrapS":33071,"wrapT":33071}]
        )");
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "textured.gltf", text));
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/textures/awesomeface.png",
            paths.assets() / "paint.png");
        ASSERT_TRUE(assets->refresh().succeeded());
        for(std::size_t i = 0; i < assets->database().size() + 3; ++i) {
            ASSERT_TRUE(assets->update());
            scheduler.wait_idle();
        }
        const auto* source_model = assets->database().find("textured.gltf");
        ASSERT_TRUE(source_model);
        ASSERT_EQ(source_model->type, AssetType::Model);
        AssetHandle mesh, material;
        for(const auto& record : assets->database().get_assets()) {
            if(record.source_asset != source_model->handle)
                continue;
            if(record.type == AssetType::Mesh)
                mesh = record.handle;
            if(record.type == AssetType::Material)
                material = record.handle;
        }
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(material);
        ASSERT_TRUE(
            scene.find_entity(surface).try_set_component(MeshRendererComponent{mesh, material}));
        const auto meshes = factory.mesh_creation_count(),
                   textures = factory.texture_creation_count();
        start();
        scheduler.wait_idle();
        auto updated = bake->update(scene, history);
        ASSERT_TRUE(updated) << updated.error();
        ASSERT_TRUE(updated.value());
        EXPECT_EQ(factory.mesh_creation_count(), meshes);
        EXPECT_EQ(factory.texture_creation_count(), textures);
        auto data = IrradianceData::load(
            paths.assets() / assets->database().find(component().irradiance)->path);
        ASSERT_TRUE(data) << data.error();
        const auto light = data.value().sample({0, 0, 0}, {0, 0, -1});
        ASSERT_TRUE(light);
        EXPECT_GT(light->r, 0.001f);
        EXPECT_GT(light->r, light->b);
        EXPECT_GT(bake->last_report()->input_bytes, 4096u);
    }

    TEST_F(IrradianceBakeWorkflowTest, DestroyingOwnerCancelsQueuedWorkWithoutBorrowedAssets) {
        {
            Comet::Tests::BlockedWorker gate(scheduler);
            start();
            bake.reset();
            assets.reset();
        }
        scheduler.wait_idle();
        EXPECT_FALSE(component().irradiance);
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "baked"));
    }

    TEST_F(IrradianceBakeWorkflowTest, RejectsChangedSourceBytesWithoutRequiringADatabaseScan) {
        start();
        scheduler.wait_idle();
        ASSERT_TRUE(MaterialSerializer{}.save(
            {.template_name = "unlit_color", .vector_properties = {{"color", {1, 4, 2, 1}}}},
            paths.assets() / "emissive.mat"));
        auto result = bake->update(scene, history);
        EXPECT_FALSE(result);
        EXPECT_NE(result.error().find("变化"), std::string::npos);
        EXPECT_FALSE(component().irradiance);
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "baked"));
    }

    TEST_F(IrradianceBakeWorkflowTest, SourceTransactionRejectsInvalidDataAndExistingDestination) {
        IrradianceData data;
        data.probes.resize(8);
        const auto first = assets->create_irradiance("baked/test.gi", data);
        ASSERT_TRUE(first.succeeded());
        const auto handle = assets->database().find("baked/test.gi")->handle;
        EXPECT_FALSE(assets->create_irradiance("baked/test.gi", data).succeeded());
        EXPECT_EQ(assets->database().find("baked/test.gi")->handle, handle);
        data.spacing.x = 0;
        EXPECT_FALSE(assets->create_irradiance("baked/invalid.gi", data).succeeded());
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "baked/invalid.gi"));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(paths.assets() / "baked/invalid.gi")));
    }
    TEST(IrradianceReferenceTest, AuthoredRoomsKeepCurrentInputsAndColoredIndirectLight) {
        const ProjectPaths paths(COMET_SAMPLE_PROJECT_DIRECTORY);
        AssetDatabase assets(paths);
        const auto scan = assets.scan();
        ASSERT_TRUE(scan.succeeded());
        const auto components = create_scene_component_registry();
        Math::Vec3 closed_floor{};
        for(const std::string kind : {"closed", "open"}) {
            const auto loaded =
                SceneSerializer(components)
                    .load((paths.assets() / "scenes" / ("gi_" + kind + "_room.scene")).string());
            ASSERT_TRUE(loaded) << loaded.error();
            auto& scene = *loaded.value();
            Entity region, moving;
            for(const auto entity : scene.get_entities()) {
                if(entity.has_component<IrradianceVolumeComponent>())
                    region = entity;
                if(entity.has_component<RigidBodyComponent>())
                    moving = entity;
            }
            ASSERT_TRUE(region);
            ASSERT_TRUE(moving);
            const auto& volume = region.get_component<IrradianceVolumeComponent>();
            const auto* record = assets.find(volume.irradiance);
            ASSERT_TRUE(record);
            const auto data = IrradianceData::load(paths.assets() / record->path);
            ASSERT_TRUE(data) << data.error();
            EXPECT_EQ(data.value().probes.size(), 216u);
            const auto described =
                CometEditor::IrradianceBake::describe_input(scene, region.get_uuid(), assets);
            ASSERT_TRUE(described) << described.error();
            EXPECT_EQ(
                described.value().fingerprint + ":" + std::to_string(volume.irradiance.value()),
                volume.bake_fingerprint);
            EXPECT_EQ(described.value().scene.render_items.size(), kind == "closed" ? 9u : 8u);
            EXPECT_TRUE(std::ranges::none_of(described.value().scene.render_items,
                [&](const auto& item) { return item.entity_id == moving.get_id(); }));
            const auto red = data.value().sample({-1.3f, .02f, 0}, {0, 1, 0});
            const auto blue = data.value().sample({1.3f, .02f, 0}, {0, 1, 0});
            const auto floor = data.value().sample({0, .02f, 0}, {0, 1, 0});
            ASSERT_TRUE(red);
            ASSERT_TRUE(blue);
            ASSERT_TRUE(floor);
            EXPECT_GT(red->r, red->b * 1.2f);
            EXPECT_GT(blue->b, blue->r * 1.15f);
            if(kind == "closed")
                closed_floor = *floor;
            else
                EXPECT_GT(closed_floor.r, floor->r * 1.05f);
        }
    }

}
