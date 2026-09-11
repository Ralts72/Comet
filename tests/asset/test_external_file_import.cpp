#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/task_scheduler.h"
#include "render/resource/resource_factory.h"
#include <gtest/gtest.h>
#include <array>
#include <fstream>
#include <iterator>

namespace Comet::Tests {
    class ExternalFileImportTest: public ::testing::Test {
    protected:
        class Factory: public RenderResourceFactory {
            GpuResourceResult<std::shared_ptr<Mesh>> try_create_mesh(
                const MeshData&) override {
                ADD_FAILURE() << "File import must not allocate GPU resources";
                return GpuResourceResult<std::shared_ptr<Mesh>>::failure(
                    vk::Result::eErrorUnknown);
            }
            GpuResourceResult<std::shared_ptr<Texture>> try_create_texture(
                const TextureData&) override {
                ADD_FAILURE() << "File import must not allocate GPU resources";
                return GpuResourceResult<std::shared_ptr<Texture>>::failure(
                    vk::Result::eErrorUnknown);
            }
        } factory;
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_external_import_"
                + std::to_string(AssetHandle::generate().value()));
        ProjectPaths paths{root / "project"};
        std::filesystem::path external = root / "external";
        AssetRegistry registry;
        TaskScheduler scheduler{1};
        AssetManager manager{paths, registry, factory, scheduler};

        void SetUp() override {
            std::filesystem::create_directories(paths.assets() / "folder");
            std::filesystem::create_directories(external);
        }
        void TearDown() override {
            scheduler.wait_idle();
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        std::filesystem::path texture(const std::filesystem::path& name = "texture.png") {
            const auto target = external / name;
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(std::filesystem::path(PROJECT_ROOT_DIR)
                                           / "assets/textures/awesomeface.png",
                target);
            return target;
        }
        std::filesystem::path mesh(const std::string& uri = "data/model.bin") {
            const auto source = external / "model.gltf";
            std::ofstream(source)
                << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":")"
                << uri
                << R"("}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"images":[{"uri":"images/albedo.png"}]})";
            std::filesystem::create_directories(external / "data");
            const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
            const std::array<std::uint16_t, 3> indices{0, 1, 2};
            std::ofstream buffer(external / "data/model.bin", std::ios::binary);
            buffer.write(
                reinterpret_cast<const char*>(positions.data()), sizeof(positions));
            buffer.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
            texture("images/albedo.png");
            return source;
        }
        AssetScanReport import(std::initializer_list<std::filesystem::path> files,
            const std::filesystem::path& directory = "folder") {
            return manager.import_files(
                std::span(files.begin(), files.size()), directory);
        }
        std::string read(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            return {std::istreambuf_iterator<char>(input), {}};
        }
        void expect_empty() {
            EXPECT_EQ(manager.get_database().size(), 0);
            EXPECT_TRUE(std::filesystem::is_empty(paths.assets() / "folder"));
            const auto staging = paths.cache() / "file-import";
            EXPECT_TRUE(
                !std::filesystem::exists(staging) || std::filesystem::is_empty(staging));
        }
    };

    TEST_F(ExternalFileImportTest, CopiesTextureWithoutExternalMetadataOrSceneMutation) {
        const auto source = texture("纹理.png");
        const AssetHandle external_handle{42};
        AssetMetadataSerializer{}.save(
            {external_handle, AssetType::Texture,
                make_default_import_settings(AssetType::Texture)},
            metadata_path(source));
        const auto report = import({source, metadata_path(source)});
        ASSERT_TRUE(report.succeeded());
        ASSERT_TRUE(report.snapshot_updated);
        const auto* record = manager.get_database().find("folder/纹理.png");
        ASSERT_NE(record, nullptr);
        EXPECT_NE(record->handle, external_handle);
        EXPECT_EQ(read(source), read(paths.assets() / record->path));
        EXPECT_EQ(AssetMetadataSerializer{}.load(metadata_path(source)).handle,
            external_handle);
        EXPECT_FALSE(registry.contains(record->handle));
        const auto imported_handle = record->handle;
        EXPECT_FALSE(import({source}).succeeded());
        EXPECT_EQ(
            manager.get_database().find("folder/纹理.png")->handle, imported_handle);
    }

    TEST_F(
        ExternalFileImportTest, GltfCopiesRelativeBufferAndImageAndDeduplicatesInputs) {
        const auto source = mesh();
        const auto report = import({source, source, external / "data/model.bin"});
        ASSERT_TRUE(report.succeeded())
            << (report.issues.empty() ? "" : report.issues.back().message);
        EXPECT_EQ(report.added_assets.size(), 2);
        EXPECT_TRUE(manager.get_database().find("folder/model.gltf"));
        EXPECT_TRUE(manager.get_database().find("folder/images/albedo.png"));
        EXPECT_EQ(read(external / "data/model.bin"),
            read(paths.assets() / "folder/data/model.bin"));
        EXPECT_FALSE(
            std::filesystem::exists(paths.assets() / "folder/data/model.bin.meta"));
        EXPECT_TRUE(std::filesystem::exists(source));
    }

    TEST_F(ExternalFileImportTest, ImportsGlbToRoot) {
        std::string json =
            read(std::filesystem::path(PROJECT_ROOT_DIR) / "assets/meshes/cube.gltf");
        while(json.size() % 4)
            json.push_back(' ');
        const auto source = external / "model.glb";
        {
            std::ofstream output(source, std::ios::binary);
            const std::array<std::uint32_t, 5> header{0x46546c67, 2,
                static_cast<std::uint32_t>(20 + json.size()),
                static_cast<std::uint32_t>(json.size()), 0x4e4f534a};
            output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
            output << json;
        }
        ASSERT_TRUE(import({source}, {}).succeeded());
        EXPECT_TRUE(manager.get_database().find("model.glb"));
    }

    TEST_F(ExternalFileImportTest, MissingDependencyRejectsWholeBatchBeforePublishing) {
        const auto source = mesh();
        const auto image = texture();
        std::filesystem::remove(external / "data/model.bin");
        EXPECT_FALSE(import({image, source}).succeeded());
        expect_empty();
        EXPECT_TRUE(std::filesystem::exists(image));
    }

    TEST_F(ExternalFileImportTest, MissingImageAndInvalidTextureAreRejected) {
        const auto source = mesh();
        std::filesystem::remove(external / "images/albedo.png");
        EXPECT_FALSE(import({source}).succeeded());
        std::ofstream(external / "broken.png") << "not an image";
        EXPECT_FALSE(import({external / "broken.png"}).succeeded());
        expect_empty();
    }

    TEST_F(ExternalFileImportTest, RejectsRemoteParentAndAbsoluteDependencies) {
        for(const std::string uri : {"https://example.invalid/buffer.bin",
                "../outside.bin", "/absolute.bin", "data/%2e%2e/model.bin"}) {
            const auto source = mesh(uri);
            EXPECT_FALSE(import({source}).succeeded()) << uri;
            expect_empty();
            std::filesystem::remove(external / "images/albedo.png");
        }
    }

    TEST_F(ExternalFileImportTest, RefusesExistingDependencyAndOrphanMetadata) {
        const auto source = mesh();
        std::filesystem::create_directories(paths.assets() / "folder/data");
        std::ofstream(paths.assets() / "folder/data/model.bin") << "existing";
        EXPECT_FALSE(import({source}).succeeded());
        EXPECT_EQ(read(paths.assets() / "folder/data/model.bin"), "existing");
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "folder/model.gltf"));
        const auto image = texture();
        std::ofstream(paths.assets() / "folder/texture.png.meta") << "orphan";
        EXPECT_FALSE(import({image}).succeeded());
        EXPECT_EQ(read(paths.assets() / "folder/texture.png.meta"), "orphan");
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "folder/texture.png"));
    }

    TEST_F(ExternalFileImportTest, RejectsUnknownInputsAndConflictingBatchNames) {
        const auto a = texture();
        const auto b = texture("other/texture.png");
        std::ofstream(external / "standalone.bin") << "buffer";
        EXPECT_FALSE(import({a, b}).succeeded());
        EXPECT_FALSE(import({a, external / "standalone.bin"}).succeeded());
        EXPECT_FALSE(import({external}).succeeded());
        EXPECT_FALSE(import({}).succeeded());
        expect_empty();
    }

    TEST_F(ExternalFileImportTest, RejectsEscapingDestinationAndSymlinkDependency) {
        const auto source = texture();
        EXPECT_FALSE(import({source}, "../outside").succeeded());
        EXPECT_FALSE(import({source}, external).succeeded());
        std::error_code error;
        std::filesystem::create_directory_symlink(
            external, paths.assets() / "link", error);
        if(error)
            GTEST_SKIP() << "Symlinks unavailable: " << error.message();
        EXPECT_FALSE(import({source}, "link").succeeded());
        const auto model = mesh();
        std::filesystem::remove(external / "data/model.bin");
        std::ofstream(root / "outside.bin") << "outside";
        std::filesystem::create_symlink(
            root / "outside.bin", external / "data/model.bin");
        EXPECT_FALSE(import({model}).succeeded());
        expect_empty();
    }

    TEST_F(ExternalFileImportTest, CopyFailureLeavesNoPublishedFilesOrStagingBatch) {
        const auto source = mesh("texture.png/model.bin");
        const auto image = texture("other/texture.png");
        std::filesystem::create_directories(external / "texture.png");
        std::filesystem::copy_file(
            external / "data/model.bin", external / "texture.png/model.bin");
        EXPECT_FALSE(import({source, image}).succeeded());
        expect_empty();
        EXPECT_TRUE(std::filesystem::exists(source));
        EXPECT_TRUE(std::filesystem::exists(image));
    }

    TEST_F(
        ExternalFileImportTest, IndexFailureRollsBackPublishedFilesAndGeneratedMetadata) {
        const auto source = mesh();
        std::ofstream(paths.assets() / "broken.png") << "existing";
        std::ofstream(paths.assets() / "broken.png.meta") << "invalid metadata";
        const auto report = import({source});
        EXPECT_FALSE(report.succeeded());
        EXPECT_FALSE(report.snapshot_updated);
        expect_empty();
        EXPECT_EQ(read(paths.assets() / "broken.png.meta"), "invalid metadata");
        EXPECT_EQ(read(paths.assets() / "broken.png"), "existing");
    }
}
