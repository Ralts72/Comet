#include "editor_assets.h"
#include "asset/artifact/mesh_artifact.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "render/resource/resource_factory.h"
#include "render/material.h"
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
                return Comet::GpuResourceResult<std::shared_ptr<Comet::Texture>>::failure(
                    vk::Result::eErrorOutOfDeviceMemory);
            }
        } factory;
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_editor_assets_"
                + std::to_string(Comet::AssetHandle::generate().value()));
        Comet::AssetRegistry runtime;
        Comet::TaskScheduler scheduler{1};
        std::unique_ptr<EditorAssets> assets;
        Comet::AssetHandle mesh;
        void SetUp() override {
            const auto directory = Comet::ProjectPaths(root).assets();
            std::filesystem::create_directories(directory);
            std::filesystem::copy_file(
                std::filesystem::path(PROJECT_ROOT_DIR) / "assets/meshes/cube.gltf",
                directory / "model.gltf");
            assets = std::make_unique<EditorAssets>(
                Comet::ProjectPaths(root), runtime, factory, scheduler);
            ASSERT_TRUE(assets->refresh().succeeded());
            mesh = assets->database().find("model.gltf")->handle;
        }
        void TearDown() override {
            assets.reset();
            runtime.clear();
            std::error_code error;
            std::filesystem::remove_all(root, error);
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
                << "version: 1\ntemplate: test\nproperties: {}\n";
            EXPECT_TRUE(assets->refresh().succeeded());
            const auto* record = assets->database().find("placement.mat");
            if(!record)
                return {};
            EXPECT_TRUE(runtime.register_asset(
                record->handle, std::make_shared<Comet::Material>("Placement", "test")));
            return record->handle;
        }
    };

    TEST_F(EditorAssetsTest, PlacementLoadsPublishedArtifactWithoutImportingSource) {
        const auto material = add_material();
        ASSERT_TRUE(material);
        const auto revision = assets->database().get_revision(mesh);
        EXPECT_FALSE(assets->prepare_mesh_placement(mesh, revision, material));
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        EXPECT_EQ(factory.mesh_creations, 0);

        complete_imports();
        ASSERT_TRUE(std::filesystem::exists(artifact_path()));
        std::ofstream(Comet::ProjectPaths(root).assets() / "model.gltf") << "invalid";
        ASSERT_TRUE(assets->prepare_mesh_placement(mesh, revision, material));
        EXPECT_EQ(factory.mesh_creations, 1);
        EXPECT_TRUE(assets->prepare_mesh_placement(mesh, revision, material));
        EXPECT_EQ(factory.mesh_creations, 1);
    }

    TEST_F(EditorAssetsTest, PlacementRejectsStaleOrInvalidReferencesBeforeLoading) {
        const auto material = add_material();
        ASSERT_TRUE(material);
        complete_imports();
        const auto revision = assets->database().get_revision(mesh);
        EXPECT_FALSE(assets->prepare_mesh_placement(mesh, revision + 1, material));
        EXPECT_FALSE(assets->prepare_mesh_placement(mesh, revision, {}));
        EXPECT_FALSE(assets->prepare_mesh_placement(mesh, revision, mesh));
        EXPECT_FALSE(assets->prepare_mesh_placement(
            material, assets->database().get_revision(material), material));
        EXPECT_EQ(factory.mesh_creations, 0);
        factory.fail = true;
        EXPECT_FALSE(assets->prepare_mesh_placement(mesh, revision, material));
        EXPECT_FALSE(runtime.contains(mesh));
        factory.fail = false;
        EXPECT_TRUE(assets->prepare_mesh_placement(mesh, revision, material));
    }

    TEST_F(EditorAssetsTest, RepeatedReferencePreparationReusesRuntimeResource) {
        ASSERT_TRUE(assets->prepare_reference(mesh, Comet::AssetType::Mesh));
        EXPECT_TRUE(runtime.contains(mesh));
        EXPECT_EQ(factory.mesh_creations, 1);
        ASSERT_TRUE(assets->prepare_reference(mesh, Comet::AssetType::Mesh));
        EXPECT_EQ(factory.mesh_creations, 1);
        EXPECT_FALSE(assets->prepare_reference(mesh, Comet::AssetType::Material));
    }

    TEST_F(EditorAssetsTest, FailedReferencePreparationDoesNotPublishAndCanBeRetried) {
        factory.fail = true;
        EXPECT_FALSE(assets->prepare_reference(mesh, Comet::AssetType::Mesh));
        EXPECT_FALSE(runtime.contains(mesh));
        factory.fail = false;
        ASSERT_TRUE(assets->prepare_reference(mesh, Comet::AssetType::Mesh));
        EXPECT_TRUE(runtime.contains(mesh));
    }

    TEST_F(EditorAssetsTest, MissingReferenceCannotBePreparedButEmptyReferenceIsAllowed) {
        const auto missing = Comet::AssetHandle::generate();
        EXPECT_FALSE(assets->prepare_reference(missing, Comet::AssetType::Mesh));
        EXPECT_TRUE(assets->prepare_reference({}, Comet::AssetType::Mesh));
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

    TEST_F(EditorAssetsTest,
        DeferredImportDoesNotRaceSynchronousBootstrapOrRebuildCurrentCache) {
        ASSERT_TRUE(assets->prepare_reference(mesh, Comet::AssetType::Mesh));
        complete_imports();
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
        std::filesystem::copy_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "assets/meshes/cube.gltf", source,
            std::filesystem::copy_options::overwrite_existing);
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
