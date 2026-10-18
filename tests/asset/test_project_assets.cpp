#include "asset/default_assets.h"
#include "asset/runtime/prefab_loader.h"
#include "asset/database.h"
#include "asset/import/environment_importer.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/model_importer.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "scene/scene_runtime.h"
#include "scripting/script_instance.h"
#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "support/render_resource_factory.h"
#include "scene/systems/physics_system.h"

#include <gtest/gtest.h>
#include <set>
#include <map>

namespace Comet::Tests {
    TEST(ProjectAssetsTest, JourneyCoversEverySceneAndCarriesCompletionAroundTheWholeRoute) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        AssetRegistry assets;
        FakeRenderResourceFactory factory;
        TaskScheduler workers(1);
        AssetManager manager(project.value().paths(), assets, factory, workers);
        ASSERT_TRUE(manager.scan().succeeded());
        auto components = std::make_shared<ComponentRegistry>(create_scene_component_registry());
        const SceneSerializer serializer(*components, manager.scene_sources(*components));
        ASSERT_TRUE(manager.load_data(AssetHandle(6729301842000001200ull)));
        const auto script = manager.load_script(AssetHandle(6729301842000001201ull));
        ASSERT_TRUE(script) << script.error().message;
        const auto& chapters = project.value().included_scenes();
        EXPECT_EQ(
            chapters.size(), std::ranges::count_if(manager.get_database().get_assets(),
                                 [](const auto& asset) { return asset.type == AssetType::Scene; }));
        ParameterMap progress;
        InputState input;
        project.value().input_actions().evaluate({}, input);
        for(size_t index = 0; index < chapters.size(); ++index) {
            SCOPED_TRACE(chapters[index].string());
            auto loaded =
                serializer.load((project.value().paths().assets() / chapters[index]).string());
            ASSERT_TRUE(loaded) << loaded.error();
            auto& scene = *loaded.value();
            Entity guide;
            scene.each<const ScriptComponent>([&](Entity entity, const auto& binding) {
                if(binding.asset == AssetHandle(6729301842000001201ull))
                    guide = entity;
            });
            ASSERT_TRUE(guide);
            const auto& parameters = guide.get_component<ScriptComponent>().parameters;
            const auto player = scene.find_entity(std::get<EntityUuid>(parameters.at("player")));
            ASSERT_TRUE(player && player.has_component<CharacterControllerComponent>());
            std::set<AssetHandle> collision_meshes;
            scene.each<const ColliderComponent>([&](Entity, const auto& collider) {
                if(collider.mesh)
                    collision_meshes.insert(collider.mesh);
            });
            for(const auto handle : collision_meshes)
                ASSERT_TRUE(manager.import_mesh(handle));
            PhysicsService physics(
                {.read = [&](AssetHandle handle) { return manager.read_mesh_data(handle); },
                    .revision =
                        [&](AssetHandle handle) { return manager.mesh_data_revision(handle); }});
            SceneRuntime runtime;
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
            ASSERT_TRUE(runtime.start(scene, SceneRuntime::State::Running,
                SceneRuntime::InputStart::Fresh, std::move(progress)));
            for(int step = 0; step < 240; ++step)
                ASSERT_TRUE(runtime.advance(1.0 / 60));
            EXPECT_GT(player.get_component<TransformComponent>().translation.y, -5);
            auto instance = ScriptInstance::create(*script.value());
            ASSERT_TRUE(instance);
            ScriptInstance::Invocation context{.delta_time = .01,
                .scene = &scene,
                .session = &runtime.get_session(),
                .input = &input,
                .assets = &assets,
                .components = components};
            const auto invoke = [&](ScriptInstance::Phase phase, std::string_view event = "") {
                context.event_handler = event;
                return instance.value()->invoke(phase, guide, parameters, context);
            };
            auto started = invoke(ScriptInstance::Phase::Start);
            ASSERT_TRUE(started) << started.error().message;
            EXPECT_EQ(std::get<float>(*runtime.get_session().get_value("tour.number")), index + 1);
            EXPECT_FALSE(
                std::get<std::string>(*runtime.get_session().get_value("tour.task")).empty());
            const auto player_camera =
                scene.find_entity(std::get<EntityUuid>(parameters.at("camera")));
            ASSERT_TRUE(player_camera);
            EXPECT_TRUE(player_camera.get_component<CameraControllerComponent>().capture_mouse);
            if(std::get<bool>(parameters.at("intro"))) {
                ASSERT_TRUE(guide.has_component<SplineFollowerComponent>());
                EXPECT_EQ(guide.get_component<SplineFollowerComponent>().finished_event,
                    "cutscene.finished");
                EXPECT_TRUE(guide.get_component<CameraComponent>().primary);
                ASSERT_TRUE(invoke(ScriptInstance::Phase::Event, "finish_intro"));
            }
            EXPECT_TRUE(player_camera.get_component<CameraComponent>().primary);
            if(parameters.contains("reference_camera")) {
                ASSERT_TRUE(invoke(ScriptInstance::Phase::Event, "reference_view"));
                EXPECT_FALSE(player_camera.get_component<CameraComponent>().primary);
                // 延迟到达的开场事件不能覆盖用户手动选择的参考视角。
                ASSERT_TRUE(invoke(ScriptInstance::Phase::Event, "finish_intro"));
                EXPECT_FALSE(player_camera.get_component<CameraComponent>().primary);
                ASSERT_TRUE(invoke(ScriptInstance::Phase::Event, "reference_view"));
                EXPECT_TRUE(player_camera.get_component<CameraComponent>().primary);
            }
            if(!parameters.contains("reference_camera")) {
                EXPECT_FALSE(
                    std::get<bool>(*runtime.get_session().get_value("tour.has_reference")));
                ASSERT_TRUE(invoke(ScriptInstance::Phase::Event, "reference_view"));
                EXPECT_TRUE(player_camera.get_component<CameraComponent>().primary);
            }
            ASSERT_TRUE(invoke(ScriptInstance::Phase::Update));
            EXPECT_FALSE(runtime.take_checkpoint_request());
            ASSERT_TRUE(invoke(ScriptInstance::Phase::Event, "complete"));
            auto updated = invoke(ScriptInstance::Phase::Update);
            ASSERT_TRUE(updated) << updated.error().message;
            const auto saved = runtime.take_checkpoint_request();
            ASSERT_TRUE(saved);
            EXPECT_EQ(saved->marker, "journey_start");
            const auto& bits = std::get<std::string>(saved->values.at("tour.completed"));
            EXPECT_EQ(std::ranges::count(bits, '1'), index + 1);
            ASSERT_TRUE(invoke(ScriptInstance::Phase::Event, "next_chapter"));
            const auto next = runtime.take_scene_request();
            ASSERT_TRUE(next);
            EXPECT_EQ(next->path, chapters[(index + 1) % chapters.size()].generic_string());
            progress = next->values;
            ASSERT_TRUE(invoke(ScriptInstance::Phase::Stop));
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST(ProjectAssetsTest, DemoCubeNormalsPointOutwards) {
        const auto mesh = MeshImporter{}.import(
            ProjectPaths(COMET_SAMPLE_PROJECT_DIRECTORY).assets() / "meshes/cube.gltf");
        ASSERT_TRUE(mesh) << mesh.error();
        ASSERT_FALSE(mesh.value().vertices.empty());
        for(const auto& vertex : mesh.value().vertices) {
            EXPECT_GT(Math::dot(vertex.position, vertex.normal), 0);
            EXPECT_NEAR(Math::length(vertex.normal), 1, 1e-5f);
        }
    }

    TEST(ProjectAssetsTest, DemoReferencesHaveUniqueMetadataAndValidMaterialDependencies) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        const auto root = project.value().paths().assets();
        std::set<AssetHandle> identities;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if(entry.path().extension() != ".meta")
                continue;
            const auto metadata = MetadataSerializer{}.load(entry.path());
            ASSERT_TRUE(metadata) << metadata.error();
            EXPECT_TRUE(metadata.value().handle);
            EXPECT_TRUE(identities.insert(metadata.value().handle).second);
            for(const auto& child : metadata.value().subresources) {
                EXPECT_TRUE(child.handle);
                EXPECT_TRUE(identities.insert(child.handle).second);
            }
        }
        AssetDatabase database(project.value().paths());
        const auto scan = database.scan();
        for(const auto& issue : scan.issues)
            ADD_FAILURE() << issue.message;
        ASSERT_TRUE(scan.succeeded());
        const auto registry = create_scene_component_registry();
        const auto scene_assets = database.get_assets();
        for(const auto& asset : scene_assets) {
            if(asset.type != AssetType::Scene)
                continue;
            SCOPED_TRACE(asset.path.string());
            const auto scene = SceneSerializer(registry, project_scene_sources(database, registry))
                                   .load((root / asset.path).string());
            ASSERT_TRUE(scene) << scene.error();
            for(const auto& reference : registry.collect_asset_references(*scene.value())) {
                if(const auto* builtin = find_default_asset(reference.handle)) {
                    EXPECT_EQ(reference.type, builtin->type);
                    EXPECT_FALSE(database.find(reference.handle));
                    continue;
                }
                const auto* record = database.find(reference.handle);
                if(reference.required || record)
                    EXPECT_TRUE(identities.contains(reference.handle));
                if(reference.required)
                    ASSERT_NE(record, nullptr);
                if(record)
                    EXPECT_EQ(record->type, reference.type);
            }
        }
        std::map<AssetHandle, MaterialData> model_materials;
        for(const auto& record : database.get_assets()) {
            if(record.type != AssetType::Model)
                continue;
            const auto metadata = MetadataSerializer{}.load(metadata_path(root / record.path));
            ASSERT_TRUE(metadata);
            auto imported = ModelImporter{}.import(root / record.path);
            ASSERT_TRUE(imported) << imported.error();
            auto bound = ModelImporter::bind(std::move(imported).value(), metadata.value());
            ASSERT_TRUE(bound) << bound.error();
            for(auto& material : bound.value().materials)
                EXPECT_TRUE(
                    model_materials.emplace(material.handle, std::move(material.data)).second);
        }
        for(const auto& record : database.get_assets()) {
            if(record.type != AssetType::Material)
                continue;
            MaterialData data;
            if(record.source_asset) {
                const auto found = model_materials.find(record.handle);
                ASSERT_NE(found, model_materials.end());
                data = found->second;
            } else {
                const auto material = MaterialSerializer{}.load(root / record.path);
                ASSERT_TRUE(material) << material.error();
                data = material.value();
            }
            EXPECT_FALSE(data.template_name.empty());
            for(const auto dependency : get_asset_dependencies(data)) {
                const auto* asset = database.find(dependency);
                ASSERT_NE(asset, nullptr);
                auto expected = AssetType::Texture;
                if(dependency == data.shader_program)
                    expected = AssetType::ShaderProgram;
                EXPECT_EQ(asset->type, expected);
            }
        }
    }

    TEST(ProjectAssetsTest, DemoSceneRetainsAudioConfigurationWhenCloned) {
        const auto registry = create_scene_component_registry();
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        AssetDatabase database(project.value().paths());
        ASSERT_TRUE(database.scan().succeeded());
        const SceneSerializer serializer(registry, project_scene_sources(database, registry));
        for(const auto* path : {"scenes/default.scene", "scenes/character.scene"}) {
            auto loaded = serializer.load((project.value().paths().assets() / path).string());
            ASSERT_TRUE(loaded) << loaded.error();
            const auto count = loaded.value()->component_count<AudioSourceComponent>();
            ASSERT_GT(count, 0u);
            auto clone = serializer.clone(*loaded.value());
            ASSERT_TRUE(clone) << clone.error();
            EXPECT_EQ(clone.value()->component_count<AudioSourceComponent>(), count);
            loaded.value()->each<const AudioSourceComponent>(
                [&](Entity entity, const auto& source) {
                    const auto copied = clone.value()->find_entity(entity.get_uuid());
                    ASSERT_TRUE(copied.has_component<AudioSourceComponent>());
                    const auto& audio = copied.get_component<AudioSourceComponent>();
                    EXPECT_EQ(audio.clip, source.clip);
                    EXPECT_EQ(audio.play_on_start, source.play_on_start);
                    EXPECT_EQ(audio.loop, source.loop);
                    EXPECT_EQ(audio.volume, source.volume);
                    EXPECT_EQ(audio.category, source.category);
                });
        }
    }

    class ProjectAssetIntegrationTest: public ::testing::TestWithParam<int> {};

    TEST_P(ProjectAssetIntegrationTest, DownloadedEnvironmentImportsAsCubemap) {
        const auto source =
            ProjectPaths(COMET_SAMPLE_PROJECT_DIRECTORY).assets()
            / ("environments/small_hangar_01_" + std::to_string(GetParam()) + "k.hdr");
        if(!std::filesystem::exists(source))
            GTEST_SKIP() << "Optional HDR is absent; run ./tools/download_assets.sh";
        const auto imported = EnvironmentImporter{}.import(source);
        ASSERT_TRUE(imported) << imported.error();
        EXPECT_TRUE(imported.value().background.cubemap);
        EXPECT_EQ(imported.value().background.format, Format::R16G16B16A16_SFLOAT);
        EXPECT_EQ(imported.value().background.width, imported.value().background.height);
        EXPECT_GT(imported.value().background.mip_levels, 1u);
        EXPECT_FALSE(imported.value().background.pixels.empty());
    }

    INSTANTIATE_TEST_SUITE_P(
        Resolutions, ProjectAssetIntegrationTest, ::testing::Values(1, 2, 4, 8));
}
