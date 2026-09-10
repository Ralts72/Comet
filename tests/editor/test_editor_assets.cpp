#include "editor_assets.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "render/resource/resource_factory.h"
#include <gtest/gtest.h>

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
    };

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

    TEST_F(EditorAssetsTest, MovePreservesIdentityAndAcknowledgesEditorWrite) {
        ASSERT_TRUE(assets->move(mesh, "renamed.gltf").succeeded());
        EXPECT_EQ(assets->database().find("renamed.gltf")->handle, mesh);
        // 强制轮询后扫描不应再次报告编辑器自己的移动或生成的 meta。
        const auto report = assets->refresh();
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_TRUE(report.removed_assets.empty());
        EXPECT_TRUE(report.modified_assets.empty());
    }
}
