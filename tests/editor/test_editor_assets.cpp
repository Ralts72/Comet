#include "assets/editor_assets.h"
#include "render/resource/mesh_data.h"
#include "render/resource/texture_data.h"
#include "asset/artifact/mesh_artifact.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "core/project.h"
#include "render/resource/resource_factory.h"
#include "render/resource/mesh.h"
#include "render/material.h"
#include "scene/scene_document.h"
#include "scene/editor_scene_session.h"
#include "editor_state.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "asset/serialization/material_serializer.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <chrono>
#include <array>
#include <fstream>
#include <thread>

namespace CometEditor::Tests {
    class EditorAssetsTest: public ::testing::Test {
    protected:
        class Factory: public Comet::RenderResourceFactory {
        public:
            int mesh_creations = 0;
            bool fail = false;
            bool fail_texture = true;
            Comet::GpuResourceResult<std::shared_ptr<Comet::Mesh>> try_create_mesh(
                const Comet::MeshData&) override {
                ++mesh_creations;
                if(fail)
                    return Comet::GpuResourceResult<std::shared_ptr<Comet::Mesh>>::
                        failure(vk::Result::eErrorOutOfDeviceMemory);
                // 只验证加载编排，不解引用替代的 GPU 对象。
                auto owner = std::make_shared<int>(0);
                return Comet::GpuResourceResult<std::shared_ptr<Comet::Mesh>>::success(
                    std::shared_ptr<Comet::Mesh>(
                        owner, reinterpret_cast<Comet::Mesh*>(owner.get())));
            }
            Comet::GpuResourceResult<std::shared_ptr<Comet::Texture>> try_create_texture(
                const Comet::TextureData&) override {
                if(!fail_texture) {
                    auto owner = std::make_shared<int>(0);
                    return Comet::GpuResourceResult<std::shared_ptr<Comet::Texture>>::
                        success(std::shared_ptr<Comet::Texture>(
                            owner, reinterpret_cast<Comet::Texture*>(owner.get())));
                }
                return Comet::GpuResourceResult<std::shared_ptr<Comet::Texture>>::failure(
                    vk::Result::eErrorOutOfDeviceMemory);
            }
        } factory;
        Comet::Tests::TemporaryDirectory directory;
        const std::filesystem::path root = directory.path();
        Comet::AssetRegistry runtime;
        Comet::TaskScheduler scheduler{1};
        std::unique_ptr<EditorAssets> assets;
        Comet::AssetHandle mesh;
        void SetUp() override {
            const auto directory = Comet::ProjectPaths(root).assets();
            std::filesystem::create_directories(directory);
            std::filesystem::copy_file(
                std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                    / "assets/meshes/cube.gltf",
                directory / "model.gltf");
            assets = std::make_unique<EditorAssets>(
                Comet::ProjectPaths(root), runtime, factory, scheduler);
            ASSERT_TRUE(assets->refresh().succeeded());
            mesh = assets->database().find("model.gltf")->handle;
        }
        void TearDown() override {
            assets.reset();
            runtime.clear();
        }
        std::filesystem::path artifact_path() const {
            return Comet::ProjectPaths(root).cache() / "imported/mesh"
                   / (std::to_string(mesh.value()) + ".bin");
        }
        void complete_imports() {
            static_cast<void>(assets->update());
            scheduler.wait_idle();
            static_cast<void>(assets->update());
        }

        Comet::AssetHandle add_material() {
            std::ofstream(Comet::ProjectPaths(root).assets() / "placement.mat")
                << R"({"version": 2, "template": "test", "properties": {}})";
            EXPECT_TRUE(assets->refresh().succeeded());
            const auto* record = assets->database().find("placement.mat");
            if(!record)
                return {};
            EXPECT_TRUE(runtime.register_asset(
                record->handle, std::make_shared<Comet::Material>("Placement", "test")));
            return record->handle;
        }
    };

    TEST_F(
        EditorAssetsTest, ExternalProjectOpensStartupSceneAndRecoversAfterInitialImport) {
        const auto directory = Comet::ProjectPaths(root).assets();
        std::filesystem::copy(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets", directory,
            std::filesystem::copy_options::recursive);
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "project.json",
            root / "project.json");
        const auto project = Comet::Project::load(root);
        assets =
            std::make_unique<EditorAssets>(project.paths(), runtime, factory, scheduler);
        ASSERT_TRUE(assets->refresh().succeeded());
        factory.fail_texture = false;
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        std::unique_ptr<Comet::Scene> active;
        std::size_t missing = 0;
        SceneDocument document(
            serializer, project.paths(), [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                missing = assets->prepare_scene(*replacement, components);
                active.swap(replacement);
                return replacement;
            });

        const auto path =
            project.paths().resolve_asset_path(project.startup_scene()).string();
        ASSERT_TRUE(document.open(project.startup_scene().string()));
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(document.get_path(), path);
        EXPECT_EQ(missing, 1U);
        EXPECT_EQ(factory.mesh_creations, 0);
        EXPECT_EQ(active->entity_count(), 2U);
        const auto references = components.collect_asset_references(*active);
        ASSERT_EQ(references.size(), 2U);
        for(const auto& reference : references) {
            const auto* record = assets->database().find(reference.handle);
            ASSERT_NE(record, nullptr);
            EXPECT_EQ(record->type, reference.type);
        }
        const auto original = serializer.serialize(*active);
        complete_imports();
        ASSERT_TRUE(assets->take_reference_refresh_request());
        EXPECT_EQ(assets->prepare_scene(*active, components), 0U);
        EXPECT_EQ(factory.mesh_creations, 1);
        EXPECT_EQ(serializer.serialize(*active), original);
        for(const auto& reference : references)
            EXPECT_TRUE(runtime.contains(reference.handle));

        active->create_entity("Saved startup edit");
        ASSERT_TRUE(document.save(document.get_path()));
        active.reset();
        ASSERT_TRUE(document.open(path));
        EXPECT_EQ(active->entity_count(), 3U);
        EXPECT_EQ(missing, 0U);
        EXPECT_EQ(factory.mesh_creations, 1);
    }

    TEST_F(EditorAssetsTest, ReopenedScenePreparesSharedReferencesFromArtifacts) {
        const auto material = add_material();
        complete_imports();
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        auto active = std::make_unique<Comet::Scene>();
        auto entity = active->create_entity("Saved");
        const auto uuid = entity.get_uuid();
        entity.add_component<Comet::MeshRendererComponent>(mesh, material);
        active->create_entity("Shared").add_component<Comet::MeshRendererComponent>(
            mesh, material);
        const auto path = (Comet::ProjectPaths(root).assets() / "saved.scene").string();
        serializer.save(*active, path);
        assets.reset();
        runtime.clear();
        std::ofstream(Comet::ProjectPaths(root).assets() / "model.gltf")
            << "invalid gltf";
        assets = std::make_unique<EditorAssets>(
            Comet::ProjectPaths(root), runtime, factory, scheduler);
        ASSERT_TRUE(assets->refresh().succeeded());
        SceneDocument document(
            serializer, Comet::ProjectPaths(root), [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                EXPECT_EQ(assets->prepare_scene(*replacement, components), 0);
                active.swap(replacement);
                return replacement;
            });
        ASSERT_TRUE(document.open(path));
        EXPECT_TRUE(runtime.resolve<Comet::Mesh>(mesh));
        EXPECT_TRUE(runtime.resolve<Comet::Material>(material));
        EXPECT_EQ(
            active->find_entity(uuid).get_component<Comet::MeshRendererComponent>().mesh,
            mesh);
        EXPECT_EQ(factory.mesh_creations, 1);
        EXPECT_FALSE(assets->take_reference_refresh_request());
        ASSERT_TRUE(document.open(path));
        EXPECT_EQ(factory.mesh_creations, 1);
    }

    TEST_F(EditorAssetsTest, MissingArtifactRepairsAfterPublicationWithoutChangingScene) {
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        auto active = std::make_unique<Comet::Scene>();
        active->create_entity("Missing").add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle(999));
        const auto before = serializer.serialize(*active);
        const auto path = (Comet::ProjectPaths(root).assets() / "missing.scene").string();
        serializer.save(*active, path);
        SceneDocument document(
            serializer, Comet::ProjectPaths(root), [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                EXPECT_EQ(assets->prepare_scene(*replacement, components), 2);
                active.swap(replacement);
                return replacement;
            });
        ASSERT_TRUE(document.open(path));
        EXPECT_TRUE(document.get_last_error().empty());
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        EXPECT_EQ(factory.mesh_creations, 0);
        complete_imports();
        ASSERT_TRUE(assets->take_reference_refresh_request());
        EXPECT_FALSE(assets->take_reference_refresh_request());
        EXPECT_FALSE(runtime.contains(mesh));
        EXPECT_EQ(assets->prepare_scene(*active, components), 1);
        EXPECT_TRUE(runtime.contains(mesh));
        EXPECT_EQ(serializer.serialize(*active), before);
        for(int frame = 0; frame < 3; ++frame) {
            static_cast<void>(assets->update());
            EXPECT_FALSE(assets->take_reference_refresh_request());
        }
        EXPECT_EQ(factory.mesh_creations, 1);
    }

    TEST_F(EditorAssetsTest, SceneRefreshCoalescesSuccessfulScansButNotFailedImports) {
        const auto components = Comet::create_scene_component_registry();
        Comet::Scene scene;
        scene.create_entity().add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle{});
        std::ofstream(Comet::ProjectPaths(root).assets() / "model.gltf")
            << "invalid gltf";
        ASSERT_TRUE(assets->refresh().succeeded());
        ASSERT_TRUE(assets->refresh().succeeded());
        EXPECT_TRUE(assets->take_reference_refresh_request());
        EXPECT_FALSE(assets->take_reference_refresh_request());
        EXPECT_EQ(assets->prepare_scene(scene, components), 1);
        complete_imports();
        EXPECT_FALSE(assets->take_reference_refresh_request());
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/meshes/cube.gltf",
            Comet::ProjectPaths(root).assets() / "model.gltf",
            std::filesystem::copy_options::overwrite_existing);
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_TRUE(assets->take_reference_refresh_request());
        EXPECT_EQ(assets->prepare_scene(scene, components), 0);
        EXPECT_TRUE(runtime.contains(mesh));
    }

    TEST_F(EditorAssetsTest, SuccessfulTextureRepairRequestsSceneMaterialPreparation) {
        const auto directory = Comet::ProjectPaths(root).assets();
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/textures/awesomeface.png",
            directory / "texture.png");
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto texture = assets->database().find("texture.png")->handle;
        EXPECT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "test", .texture_properties = {{"albedo", texture}}},
            directory / "textured.mat"));
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto material = assets->database().find("textured.mat")->handle;
        const auto components = Comet::create_scene_component_registry();
        Comet::Scene scene;
        scene.create_entity().add_component<Comet::MeshRendererComponent>(
            Comet::AssetHandle{}, material);
        EXPECT_EQ(assets->prepare_scene(scene, components), 1);
        EXPECT_FALSE(assets->reimport_texture(texture, {}));
        EXPECT_FALSE(assets->take_reference_refresh_request());
        factory.fail_texture = false;
        ASSERT_TRUE(assets->reimport_texture(texture, {}));
        ASSERT_TRUE(assets->take_reference_refresh_request());
        EXPECT_FALSE(runtime.contains(material));
        EXPECT_EQ(assets->prepare_scene(scene, components), 0);
        EXPECT_TRUE(runtime.contains(material));
    }

    TEST_F(EditorAssetsTest, PlayAndStopPrepareTheSceneBeingActivated) {
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        auto active = std::make_unique<Comet::Scene>();
        active->create_entity().add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle{});
        complete_imports();
        EditorState state;
        int preparations = 0;
        EditorSceneSession session(
            state, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                ++preparations;
                EXPECT_EQ(assets->prepare_scene(*replacement, components), 0);
                active.swap(replacement);
                return replacement;
            });
        session.request_mode(EditorMode::Play);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_TRUE(runtime.contains(mesh));
        runtime.clear();
        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_TRUE(runtime.contains(mesh));
        EXPECT_EQ(preparations, 2);
    }

    TEST_F(
        EditorAssetsTest, ExternalFileImportQueuesArtifactWithoutGpuOrExplicitRefresh) {
        const auto source = root / "external.gltf";
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/meshes/cube.gltf",
            source);
        const std::array files{source};
        const auto report = assets->import_files(files, {});
        ASSERT_TRUE(report.succeeded());
        const auto* record = assets->database().find("external.gltf");
        ASSERT_NE(record, nullptr);
        const auto handle = record->handle;
        const auto artifact =
            artifact_path().parent_path() / (std::to_string(handle.value()) + ".bin");
        EXPECT_FALSE(std::filesystem::exists(artifact));
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact, handle));
        EXPECT_FALSE(runtime.contains(handle));
        EXPECT_EQ(factory.mesh_creations, 0);
        EXPECT_TRUE(std::filesystem::exists(source));
    }

    TEST_F(EditorAssetsTest, ReferenceLoadingDoesNotImportAndRejectsStaleRevisions) {
        const auto revision = assets->database().get_revision(mesh);
        EXPECT_TRUE(assets->load_reference({}, Comet::AssetType::Mesh, 0));
        EXPECT_FALSE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision));
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        EXPECT_EQ(factory.mesh_creations, 0);
        complete_imports();
        EXPECT_FALSE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision + 1));
        EXPECT_FALSE(assets->load_reference(mesh, Comet::AssetType::Material, revision));
        EXPECT_EQ(factory.mesh_creations, 0);
        std::ofstream(Comet::ProjectPaths(root).assets() / "model.gltf") << "invalid";
        factory.fail = true;
        EXPECT_FALSE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision));
        EXPECT_FALSE(runtime.contains(mesh));
        factory.fail = false;
        EXPECT_TRUE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision));
        EXPECT_TRUE(assets->load_reference(mesh, Comet::AssetType::Mesh, revision));
        EXPECT_EQ(factory.mesh_creations, 2);
    }

    TEST_F(EditorAssetsTest, MissingReferenceCannotBeLoadedButEmptyReferenceIsAllowed) {
        const auto missing = Comet::AssetHandle::generate();
        EXPECT_FALSE(assets->load_reference(
            missing, Comet::AssetType::Mesh, assets->database().get_revision(missing)));
        EXPECT_TRUE(assets->load_reference({}, Comet::AssetType::Mesh, {}));
        EXPECT_EQ(factory.mesh_creations, 0);
    }

    TEST_F(EditorAssetsTest, ScannedMeshesImportAutomaticallyWithoutSelectionOrGpu) {
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
        EXPECT_FALSE(runtime.contains(mesh));
        EXPECT_EQ(factory.mesh_creations, 0);
        // 无扫描事件时，不自动补建手工删除的缓存。
        std::filesystem::remove(artifact_path());
        complete_imports();
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
    }

    TEST_F(EditorAssetsTest, AutomaticImportReusesCacheUntilExplicitReimport) {
        complete_imports();
        ASSERT_TRUE(assets->load_reference(
            mesh, Comet::AssetType::Mesh, assets->database().get_revision(mesh)));
        EXPECT_EQ(factory.mesh_creations, 1);
        const auto stamp =
            std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
        std::filesystem::last_write_time(artifact_path(), stamp);
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_EQ(std::filesystem::last_write_time(artifact_path()), stamp);
        EXPECT_EQ(factory.mesh_creations, 1);
        assets->request_mesh_reimport(mesh);
        complete_imports();
        EXPECT_EQ(factory.mesh_creations, 2);
        EXPECT_NE(std::filesystem::last_write_time(artifact_path()), stamp);
    }

    TEST_F(EditorAssetsTest, SourceMonitorImportsNewAndModifiedMeshesWithoutRefresh) {
        complete_imports();
        const auto directory = Comet::ProjectPaths(root).assets();
        std::filesystem::copy_file(directory / "model.gltf", directory / "second.gltf");
        std::ofstream(directory / "model.gltf", std::ios::app) << "\n";
        const auto stamp =
            std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
        std::filesystem::last_write_time(artifact_path(), stamp);
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        const auto report = assets->update();
        ASSERT_TRUE(report);
        ASSERT_TRUE(report->succeeded());
        complete_imports();
        const auto* added = assets->database().find("second.gltf");
        ASSERT_NE(added, nullptr);
        const auto second_artifact = artifact_path().parent_path()
                                     / (std::to_string(added->handle.value()) + ".bin");
        EXPECT_TRUE(Comet::MeshArtifact::load(second_artifact, added->handle));
        EXPECT_NE(std::filesystem::last_write_time(artifact_path()), stamp);
        EXPECT_EQ(factory.mesh_creations, 0);
    }

    TEST_F(EditorAssetsTest, FailedImportWaitsForNewScanOrExplicitRetry) {
        const auto source = Comet::ProjectPaths(root).assets() / "model.gltf";
        std::ofstream(source, std::ios::trunc) << "invalid";
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/meshes/cube.gltf",
            source, std::filesystem::copy_options::overwrite_existing);
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
        EXPECT_EQ(factory.mesh_creations, 0);
    }

    TEST_F(EditorAssetsTest, AddingPreviouslyMissingBufferRetriesUnindexedDependency) {
        const auto directory = Comet::ProjectPaths(root).assets();
        std::ofstream(directory / "model.gltf", std::ios::trunc)
            << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"mesh.bin"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";
        ASSERT_TRUE(assets->refresh().succeeded());
        complete_imports();
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        EXPECT_TRUE(assets->database().get_import_dependencies(mesh).empty());
        const std::array<float, 9> vertices{0, 0, 0, 1, 0, 0, 0, 1, 0};
        const std::array<std::uint16_t, 3> indices{0, 1, 2};
        {
            std::ofstream buffer(directory / "mesh.bin", std::ios::binary);
            buffer.write(
                reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
            buffer.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        const auto report = assets->update();
        ASSERT_TRUE(report);
        ASSERT_TRUE(report->succeeded());
        EXPECT_TRUE(report->added_assets.empty());
        EXPECT_TRUE(report->modified_assets.empty());
        complete_imports();
        EXPECT_TRUE(Comet::MeshArtifact::load(artifact_path(), mesh));
        EXPECT_FALSE(assets->database().get_import_dependencies(mesh).empty());
        EXPECT_EQ(factory.mesh_creations, 0);
    }

    TEST_F(EditorAssetsTest, MovePreservesIdentityAndAcknowledgesEditorWrite) {
        ASSERT_TRUE(assets->move(mesh, "renamed.gltf").succeeded());
        EXPECT_EQ(assets->database().find("renamed.gltf")->handle, mesh);
        const auto report = assets->refresh();
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_TRUE(report.removed_assets.empty());
        EXPECT_TRUE(report.modified_assets.empty());
        complete_imports();
        const auto artifact = Comet::MeshArtifact::load(artifact_path(), mesh);
        ASSERT_TRUE(artifact);
        EXPECT_EQ(artifact->source_inputs.files.front().relative_path, "renamed.gltf");
    }
}
