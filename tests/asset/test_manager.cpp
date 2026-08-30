#include "asset/manager.h"

#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/task_scheduler.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_factory.h"
#include "render/resource/texture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Comet::Tests {
    namespace {
        class TemporaryProject final {
        public:
            TemporaryProject() {
                m_root = std::filesystem::temp_directory_path()
                    / ("comet_asset_manager_test_"
                       + std::to_string(AssetHandle::generate().value()));
                std::filesystem::create_directories(paths().assets());
            }

            ~TemporaryProject() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] ProjectPaths paths() const {
                return ProjectPaths(m_root);
            }

            std::filesystem::path add_material(
                const AssetHandle handle,
                const std::string& template_name) const {
                const std::filesystem::path path =
                    paths().assets() / "materials/test.mat";
                std::filesystem::create_directories(path.parent_path());
                MaterialSerializer{}.save({
                    .template_name = template_name,
                    .texture_properties = {}
                }, path);
                AssetMetadataSerializer{}.save({
                    .handle = handle,
                    .type = AssetType::Material
                }, metadata_path(path));
                return path;
            }

            std::filesystem::path add_mesh(
                const AssetHandle handle,
                const std::string_view primitive =
                    R"({"attributes":{"POSITION":0},"indices":1})") const {
                const std::filesystem::path path =
                    paths().assets() / "meshes/test.gltf";
                std::filesystem::create_directories(path.parent_path());
                write_mesh(path, primitive);
                AssetMetadataSerializer{}.save({
                    .handle = handle,
                    .type = AssetType::Mesh
                }, metadata_path(path));
                return path;
            }

            std::filesystem::path add_texture(
                const AssetHandle handle) const {
                const std::filesystem::path path =
                    paths().assets() / "textures/test.png";
                std::filesystem::create_directories(path.parent_path());
                std::filesystem::copy_file(
                    std::filesystem::path(PROJECT_ROOT_DIR)
                        / "assets/textures/awesomeface.png",
                    path,
                    std::filesystem::copy_options::overwrite_existing);
                AssetMetadataSerializer{}.save({
                    .handle = handle,
                    .type = AssetType::Texture,
                    .import_settings = TextureImportSettings{}
                }, metadata_path(path));
                return path;
            }

            static void replace_texture(
                const std::filesystem::path& path,
                const bool restore_original = false) {
                std::filesystem::copy_file(
                    std::filesystem::path(PROJECT_ROOT_DIR)
                        / (restore_original
                            ? "assets/textures/awesomeface.png"
                            : "assets/textures/R-C.jpeg"),
                    path,
                    std::filesystem::copy_options::overwrite_existing);
            }

            static void corrupt_texture(
                const std::filesystem::path& path) {
                std::ofstream output(
                    path,
                    std::ios::binary | std::ios::trunc);
                output << "corrupted texture data";
            }

            std::filesystem::path add_external_mesh(
                const AssetHandle handle) const {
                const std::filesystem::path path =
                    paths().assets() / "meshes/external.gltf";
                const std::filesystem::path buffer =
                    paths().assets() / "meshes/external.bin";
                std::filesystem::create_directories(path.parent_path());
                write_external_mesh_buffer(buffer, false);
                std::ofstream output(path, std::ios::binary);
                output
                    << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"external.bin"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";
                output.close();
                AssetMetadataSerializer{}.save({
                    .handle = handle,
                    .type = AssetType::Mesh
                }, metadata_path(path));
                return buffer;
            }

            static void write_mesh(
                const std::filesystem::path& path,
                const std::string_view primitive) {
                std::ofstream output(path, std::ios::binary);
                output
                    << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[)"
                    << primitive << "]}]}";
            }

            static void write_external_mesh_buffer(
                const std::filesystem::path& path,
                const bool modified) {
                std::array<std::uint8_t, 42> data{};
                data[14] = modified ? 0x00 : 0x80;
                data[15] = modified ? 0x40 : 0x3f;
                data[30] = 0x80;
                data[31] = 0x3f;
                data[38] = 0x01;
                data[40] = 0x02;
                std::ofstream output(path, std::ios::binary);
                output.write(
                    reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
            }

        private:
            std::filesystem::path m_root;
        };

        class FakeRenderResourceFactory final : public RenderResourceFactory {
        public:
            GpuResourceResult<std::shared_ptr<Texture>> try_create_texture(
                const TextureData&) override {
                ++m_texture_creation_count;
                if(m_on_texture_creation) {
                    auto callback = std::move(m_on_texture_creation);
                    callback();
                }
                if(m_fail_texture_creation) {
                    return GpuResourceResult<std::shared_ptr<Texture>>::failure(
                        vk::Result::eErrorOutOfDeviceMemory);
                }

                auto owner = std::make_shared<std::uint8_t>(0);
                return GpuResourceResult<std::shared_ptr<Texture>>::success(
                    std::shared_ptr<Texture>(
                        owner, reinterpret_cast<Texture*>(owner.get())));
            }

            GpuResourceResult<std::shared_ptr<Mesh>> try_create_mesh(
                const MeshData& data) override {
                ++m_mesh_creation_count;
                m_last_mesh_vertex_count = data.vertices.size();
                if(m_on_mesh_creation) {
                    auto callback = std::move(m_on_mesh_creation);
                    callback();
                }
                if(m_fail_mesh_creation) {
                    return GpuResourceResult<std::shared_ptr<Mesh>>::failure(
                        vk::Result::eErrorOutOfDeviceMemory);
                }

                auto owner = std::make_shared<std::uint8_t>(0);
                return GpuResourceResult<std::shared_ptr<Mesh>>::success(
                    std::shared_ptr<Mesh>(
                        owner, reinterpret_cast<Mesh*>(owner.get())));
            }

            void fail_mesh_creation(const bool fail) {
                m_fail_mesh_creation = fail;
            }

            void fail_texture_creation(const bool fail) {
                m_fail_texture_creation = fail;
            }

            void on_next_mesh_creation(std::function<void()> callback) {
                m_on_mesh_creation = std::move(callback);
            }

            void on_next_texture_creation(std::function<void()> callback) {
                m_on_texture_creation = std::move(callback);
            }

            [[nodiscard]] std::size_t mesh_creation_count() const {
                return m_mesh_creation_count;
            }

            [[nodiscard]] std::size_t last_mesh_vertex_count() const {
                return m_last_mesh_vertex_count;
            }

            [[nodiscard]] std::size_t texture_creation_count() const {
                return m_texture_creation_count;
            }

        private:
            bool m_fail_mesh_creation = false;
            bool m_fail_texture_creation = false;
            std::size_t m_mesh_creation_count = 0;
            std::size_t m_last_mesh_vertex_count = 0;
            std::size_t m_texture_creation_count = 0;
            std::function<void()> m_on_mesh_creation;
            std::function<void()> m_on_texture_creation;
        };

        bool contains_handle(
            const std::vector<AssetHandle>& handles,
            const AssetHandle expected) {
            return std::ranges::find(handles, expected) != handles.end();
        }
    }

    TEST(AssetManagerTest, LoadsAndCachesMeshByAssetHandle) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Mesh> mesh = manager.load_mesh(handle);

        ASSERT_NE(mesh, nullptr);
        EXPECT_EQ(registry.resolve<Mesh>(handle), mesh);
        EXPECT_EQ(manager.load_mesh(handle), mesh);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 1);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 3);
        EXPECT_TRUE(std::filesystem::is_regular_file(
            project.paths().cache() / "imported" / "mesh" / "42.bin"));
    }

    TEST(AssetManagerTest, RebuildsCorruptedMeshImportCache) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        project.add_mesh(handle);
        const std::filesystem::path cache_path =
            project.paths().cache() / "imported" / "mesh" / "42.bin";
        {
            AssetRegistry registry;
            FakeRenderResourceFactory resource_factory;
            TaskScheduler task_scheduler(1);
            AssetManager manager(
                project.paths(), registry, resource_factory, task_scheduler);
            ASSERT_TRUE(manager.scan().snapshot_updated);
            ASSERT_NE(manager.load_mesh(handle), nullptr);
        }
        {
            std::ofstream output(
                cache_path,
                std::ios::binary | std::ios::trunc);
            output << "corrupted";
        }

        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().snapshot_updated);

        EXPECT_NE(manager.load_mesh(handle), nullptr);
        EXPECT_GT(std::filesystem::file_size(cache_path), 9u);
    }

    TEST(AssetManagerTest, RefreshesModifiedLoadedMeshAfterScan) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Mesh> original = manager.load_mesh(handle);
        ASSERT_NE(original, nullptr);
        TemporaryProject::write_mesh(
            mesh_path,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_EQ(registry.resolve<Mesh>(handle), original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 1);

        task_scheduler.wait_idle();
        manager.process_completions();

        const std::shared_ptr<Mesh> modified = registry.resolve<Mesh>(handle);
        ASSERT_NE(modified, nullptr);
        EXPECT_NE(modified, original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 2);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 6);
    }

    TEST(AssetManagerTest, RefreshesLoadedMeshWhenExternalBufferChanges) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path dependency =
            project.add_external_mesh(handle);
        {
            AssetRegistry registry;
            FakeRenderResourceFactory resource_factory;
            TaskScheduler task_scheduler(1);
            AssetManager manager(
                project.paths(), registry, resource_factory, task_scheduler);
            ASSERT_TRUE(manager.scan().succeeded());
            ASSERT_NE(manager.load_mesh(handle), nullptr);
        }

        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const std::shared_ptr<Mesh> original = manager.load_mesh(handle);
        ASSERT_NE(original, nullptr);
        EXPECT_EQ(
            std::vector<std::filesystem::path>(
                manager.get_database()
                    .get_import_dependencies(handle).begin(),
                manager.get_database()
                    .get_import_dependencies(handle).end()),
            (std::vector<std::filesystem::path>{"meshes/external.bin"}));

        const auto previous_write_time =
            std::filesystem::last_write_time(dependency);
        TemporaryProject::write_external_mesh_buffer(dependency, true);
        std::filesystem::last_write_time(
            dependency,
            previous_write_time + std::chrono::seconds(1));

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_EQ(registry.resolve<Mesh>(handle), original);
        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_NE(registry.resolve<Mesh>(handle), original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 2);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 3);
    }

    TEST(AssetManagerTest, ReschedulesMeshWhenInputsChangeDuringPublication) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path dependency =
            project.add_external_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().succeeded());
        const std::shared_ptr<Mesh> original = manager.load_mesh(handle);
        ASSERT_NE(original, nullptr);
        TemporaryProject::write_external_mesh_buffer(dependency, true);
        const AssetScanReport refresh = manager.scan();
        ASSERT_TRUE(contains_handle(refresh.modified_assets, handle));
        const AssetRevision requested_revision =
                manager.get_database().get_revision(handle);
        resource_factory.on_next_mesh_creation([&] {
            TemporaryProject::write_external_mesh_buffer(dependency, false);
        });

        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_EQ(registry.resolve<Mesh>(handle), original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 2u);
        EXPECT_GT(
            manager.get_database().get_revision(handle),
            requested_revision);

        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_NE(registry.resolve<Mesh>(handle), original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 3u);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 3u);
    }

    TEST(AssetManagerTest, DiscardsMeshCandidateWhenRevisionChangesBeforePublication) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const AssetRevision requested_revision =
            manager.get_database().get_revision(handle);
        bool rescan_detected_change = false;
        resource_factory.on_next_mesh_creation([&] {
            TemporaryProject::write_mesh(
                mesh_path,
                R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
            const AssetScanReport report = manager.scan();
            rescan_detected_change = contains_handle(
                report.modified_assets,
                handle);
        });

        const std::shared_ptr<Mesh> stale_candidate = manager.load_mesh(handle);

        EXPECT_TRUE(rescan_detected_change);
        EXPECT_EQ(stale_candidate, nullptr);
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_GT(
            manager.get_database().get_revision(handle),
            requested_revision);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 1);
    }

    TEST(AssetManagerTest, PublishesOnlyLatestBackgroundMeshRevision) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Mesh> original = manager.load_mesh(handle);
        ASSERT_NE(original, nullptr);

        std::promise<void> release_worker;
        const std::shared_future<void> worker_gate =
            release_worker.get_future().share();
        std::future<void> blocker = task_scheduler.submit([worker_gate] {
            worker_gate.wait();
        });

        TemporaryProject::write_mesh(
            mesh_path,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
        const AssetScanReport first_refresh = manager.scan();
        ASSERT_TRUE(contains_handle(first_refresh.modified_assets, handle));
        const AssetRevision first_revision =
            manager.get_database().get_revision(handle);

        TemporaryProject::write_mesh(
            mesh_path,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
        const AssetScanReport second_refresh = manager.scan();
        ASSERT_TRUE(contains_handle(second_refresh.modified_assets, handle));
        EXPECT_GT(manager.get_database().get_revision(handle), first_revision);
        EXPECT_EQ(registry.resolve<Mesh>(handle), original);

        release_worker.set_value();
        task_scheduler.wait_idle();
        blocker.get();
        manager.process_completions();

        EXPECT_NE(registry.resolve<Mesh>(handle), original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 2);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 9);
    }

    TEST(AssetManagerTest, KeepsPreviousMeshWhenImportFails) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Mesh> original = manager.load_mesh(handle);
        ASSERT_NE(original, nullptr);
        {
            std::ofstream output(mesh_path, std::ios::binary);
            output << "corrupted glTF with a different file size";
        }

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        task_scheduler.wait_idle();
        manager.process_completions();
        EXPECT_EQ(registry.resolve<Mesh>(handle), original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 1);
    }

    TEST(AssetManagerTest, KeepsPreviousMeshWhenRuntimeCreationFails) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Mesh> original = manager.load_mesh(handle);
        ASSERT_NE(original, nullptr);
        resource_factory.fail_mesh_creation(true);
        TemporaryProject::write_mesh(
            mesh_path,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_EQ(registry.resolve<Mesh>(handle), original);

        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_EQ(registry.resolve<Mesh>(handle), original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 2);
    }

    TEST(AssetManagerTest, LoadsAndCachesTextureByAssetHandle) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> texture = manager.load_texture(handle);

        ASSERT_NE(texture, nullptr);
        EXPECT_EQ(registry.resolve<Texture>(handle), texture);
        EXPECT_EQ(manager.load_texture(handle), texture);
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);
    }

    TEST(AssetManagerTest, KeepsPreviousTextureWhenRuntimeCreationFails) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const std::filesystem::path texture_path =
            project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = manager.load_texture(handle);
        ASSERT_NE(original, nullptr);
        resource_factory.fail_texture_creation(true);
        TemporaryProject::replace_texture(texture_path);

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_EQ(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);

        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_EQ(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 2);
    }

    TEST(AssetManagerTest, RefreshesModifiedTextureOnOwnerThread) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const std::filesystem::path texture_path =
            project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = manager.load_texture(handle);
        ASSERT_NE(original, nullptr);
        TemporaryProject::replace_texture(texture_path);

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_EQ(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);

        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_NE(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 2);
    }

    TEST(AssetManagerTest, PublishesOnlyLatestBackgroundTextureRevision) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const std::filesystem::path texture_path =
            project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = manager.load_texture(handle);
        ASSERT_NE(original, nullptr);

        std::promise<void> release_worker;
        const std::shared_future<void> worker_gate =
            release_worker.get_future().share();
        std::future<void> blocker = task_scheduler.submit([worker_gate] {
            worker_gate.wait();
        });

        TemporaryProject::replace_texture(texture_path);
        const AssetScanReport first_refresh = manager.scan();
        ASSERT_TRUE(contains_handle(first_refresh.modified_assets, handle));
        const AssetRevision first_revision =
            manager.get_database().get_revision(handle);

        TemporaryProject::replace_texture(texture_path, true);
        const AssetScanReport second_refresh = manager.scan();
        ASSERT_TRUE(contains_handle(second_refresh.modified_assets, handle));
        EXPECT_GT(manager.get_database().get_revision(handle), first_revision);
        EXPECT_EQ(registry.resolve<Texture>(handle), original);

        release_worker.set_value();
        task_scheduler.wait_idle();
        blocker.get();
        manager.process_completions();

        EXPECT_NE(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 2);
    }

    TEST(AssetManagerTest, ReschedulesTextureWhenInputChangesDuringPublication) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().succeeded());
        const std::shared_ptr<Texture> original = manager.load_texture(handle);
        ASSERT_NE(original, nullptr);
        TemporaryProject::replace_texture(texture_path);
        const AssetScanReport refresh = manager.scan();
        ASSERT_TRUE(contains_handle(refresh.modified_assets, handle));
        const AssetRevision requested_revision =
                manager.get_database().get_revision(handle);
        resource_factory.on_next_texture_creation([&] {
            TemporaryProject::replace_texture(texture_path, true);
        });

        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_EQ(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 2u);
        EXPECT_GT(
            manager.get_database().get_revision(handle),
            requested_revision);

        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_NE(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 3u);
    }

    TEST(AssetManagerTest, KeepsPreviousTextureWhenBackgroundImportFails) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const std::filesystem::path texture_path =
            project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = manager.load_texture(handle);
        ASSERT_NE(original, nullptr);
        TemporaryProject::corrupt_texture(texture_path);

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        task_scheduler.wait_idle();
        manager.process_completions();

        EXPECT_EQ(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);
    }

    TEST(AssetManagerTest, ReloadsModifiedLoadedMaterialAfterScan) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path material_path = project.add_material(
            handle, "original_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        const AssetScanReport initial_scan = manager.scan();
        ASSERT_TRUE(initial_scan.snapshot_updated);
        EXPECT_TRUE(contains_handle(initial_scan.added_assets, handle));
        const std::shared_ptr<Material> original = manager.load_material(handle);
        ASSERT_NE(original, nullptr);
        EXPECT_EQ(original->get_template_name(), "original_template");

        MaterialSerializer{}.save({
            .template_name = "modified_template_with_different_size",
            .texture_properties = {}
        }, material_path);
        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        const std::shared_ptr<Material> modified = registry.resolve<Material>(handle);
        ASSERT_NE(modified, nullptr);
        EXPECT_NE(modified, original);
        EXPECT_EQ(
            modified->get_template_name(),
            "modified_template_with_different_size");
    }

    TEST(AssetManagerTest, KeepsRuntimeMaterialAndDependenciesWhenFileIsInvalid) {
        const TemporaryProject project;
        constexpr AssetHandle texture_handle(42);
        constexpr AssetHandle material_handle(100);
        project.add_texture(texture_handle);
        const std::filesystem::path material_path = project.add_material(
            material_handle, "original_template");
        MaterialSerializer{}.save({
            .template_name = "original_template",
            .texture_properties = {{"albedo", texture_handle}}
        }, material_path);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().succeeded());
        const std::shared_ptr<Material> original =
                manager.load_material(material_handle);
        ASSERT_NE(original, nullptr);
        std::ofstream(material_path, std::ios::trunc)
            << "invalid material";

        const AssetScanReport refresh = manager.scan();

        EXPECT_TRUE(refresh.snapshot_updated);
        EXPECT_FALSE(refresh.succeeded());
        EXPECT_TRUE(contains_handle(
            refresh.modified_assets, material_handle));
        EXPECT_EQ(registry.resolve<Material>(material_handle), original);
        EXPECT_EQ(
            std::vector<AssetHandle>(
                manager.get_database().get_dependencies(material_handle).begin(),
                manager.get_database().get_dependencies(material_handle).end()),
            (std::vector{texture_handle}));
        EXPECT_EQ(
            std::vector<AssetHandle>(
                manager.get_database().get_dependents(texture_handle).begin(),
                manager.get_database().get_dependents(texture_handle).end()),
            (std::vector{material_handle}));
    }

    TEST(AssetManagerTest, UnregistersRemovedRuntimeAssetsAfterScan) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path material_path = project.add_material(
            handle, "test_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_NE(manager.load_material(handle), nullptr);
        ASSERT_TRUE(registry.contains(handle));
        std::filesystem::remove(material_path);
        std::filesystem::remove(metadata_path(material_path));

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.removed_assets, handle));
        EXPECT_FALSE(registry.contains(handle));
    }

    TEST(AssetManagerTest, UnloadsRuntimeAssetWhenIndexedTypeChanges) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_NE(manager.load_texture(handle), nullptr);
        ASSERT_NE(registry.resolve<Texture>(handle), nullptr);

        const std::filesystem::path material_path =
                texture_path.parent_path() / "test.mat";
        std::filesystem::rename(texture_path, material_path);
        std::filesystem::remove(metadata_path(texture_path));
        MaterialSerializer{}.save({
            .template_name = "changed_type",
            .texture_properties = {}
        }, material_path);
        AssetMetadataSerializer{}.save({
            .handle = handle,
            .type = AssetType::Material
        }, metadata_path(material_path));

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_FALSE(registry.contains(handle));
        const std::shared_ptr<Material> material = manager.load_material(handle);
        ASSERT_NE(material, nullptr);
        EXPECT_EQ(material->get_template_name(), "changed_type");
    }

    TEST(AssetManagerTest, KeepsRuntimeAssetsWhenRescanCannotCommitSnapshot) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        project.add_material(handle, "test_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Material> original = manager.load_material(handle);
        ASSERT_NE(original, nullptr);
        std::filesystem::remove_all(project.paths().assets());

        const AssetScanReport failed_refresh = manager.scan();

        EXPECT_FALSE(failed_refresh.snapshot_updated);
        EXPECT_EQ(registry.resolve<Material>(handle), original);
        ASSERT_NE(manager.get_database().find(handle), nullptr);
    }

    TEST(AssetManagerTest, KeepsRuntimeAssetWhenMetadataIsMalformed) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = manager.load_texture(handle);
        ASSERT_NE(original, nullptr);
        const AssetRevision revision =
                manager.get_database().get_revision(handle);
        std::ofstream(metadata_path(texture_path), std::ios::trunc)
            << "version: invalid\n";

        const AssetScanReport refresh = manager.scan();

        EXPECT_TRUE(refresh.snapshot_updated);
        EXPECT_FALSE(refresh.succeeded());
        EXPECT_TRUE(refresh.removed_assets.empty());
        EXPECT_TRUE(refresh.modified_assets.empty());
        EXPECT_EQ(registry.resolve<Texture>(handle), original);
        EXPECT_EQ(manager.get_database().get_revision(handle), revision);
    }

    TEST(AssetManagerTest, RejectsInvalidMaterialBeforeUpdatingFileAndRuntime) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path material_path = project.add_material(
            handle, "original_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(
            project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Material> original = manager.load_material(handle);
        ASSERT_NE(original, nullptr);

        const std::shared_ptr<Material> updated = manager.update_material(
            handle,
            {
                .template_name = "updated_template",
                .texture_properties = {}
            });

        ASSERT_NE(updated, nullptr);
        EXPECT_NE(updated, original);
        EXPECT_EQ(registry.resolve<Material>(handle), updated);
        EXPECT_EQ(
            MaterialSerializer{}.load(material_path).template_name,
            "updated_template");

        const std::shared_ptr<Material> before_invalid_update =
            registry.resolve<Material>(handle);
        EXPECT_EQ(
            manager.update_material(handle, {
                .template_name = "",
                .texture_properties = {}
            }),
            nullptr);
        EXPECT_EQ(registry.resolve<Material>(handle), before_invalid_update);
        EXPECT_EQ(
            MaterialSerializer{}.load(material_path).template_name,
            "updated_template");
    }
}
