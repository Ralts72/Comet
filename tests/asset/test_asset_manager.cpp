#include "asset/asset_manager.h"
#include "asset/data/texture_data.h"
#include "asset/data/mesh_data.h"

#include "asset/artifact/mesh_artifact.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/task_scheduler.h"
#include "support/blocked_worker.h"
#include "support/hdr_image.h"
#include "render/material/material.h"
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
    std::vector<AssetHandle> completed_handles(Result<std::vector<AssetHandle>, Error> result) {
        if(!result) {
            ADD_FAILURE() << result.error().message;
            return {};
        }
        return std::move(result).value();
    }

    template<typename T> std::shared_ptr<T> loaded_asset(Result<std::shared_ptr<T>, Error> result) {
        if(!result) {
            ADD_FAILURE() << result.error().message;
            return {};
        }
        return std::move(result).value();
    }

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

            [[nodiscard]] ProjectPaths paths() const { return ProjectPaths(m_root); }

            std::filesystem::path add_material(
                const AssetHandle handle, const std::string& template_name) const {
                const std::filesystem::path path = paths().assets() / "materials/test.mat";
                std::filesystem::create_directories(path.parent_path());
                EXPECT_TRUE(MaterialSerializer{}.save(
                    {.template_name = template_name, .texture_properties = {}}, path));
                EXPECT_TRUE(MetadataSerializer{}.save(
                    {.handle = handle, .type = AssetType::Material}, metadata_path(path)));
                return path;
            }

            std::filesystem::path add_texture(const AssetHandle handle) const {
                const std::filesystem::path path = paths().assets() / "textures/test.png";
                std::filesystem::create_directories(path.parent_path());
                std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                               / "assets/textures/awesomeface.png",
                    path, std::filesystem::copy_options::overwrite_existing);
                EXPECT_TRUE(
                    MetadataSerializer{}.save({.handle = handle,
                                                  .type = AssetType::Texture,
                                                  .import_settings = TextureImportSettings{}},
                        metadata_path(path)));
                return path;
            }

            void add_textured_material(
                const AssetHandle handle, const AssetHandle texture_handle) const {
                const auto path =
                    paths().assets() / "materials" / (std::to_string(handle.value()) + ".mat");
                std::filesystem::create_directories(path.parent_path());
                EXPECT_TRUE(MaterialSerializer{}.save(
                    {.template_name = "test_template",
                        .texture_properties = {{"u_Texture0", texture_handle}}},
                    path));
                EXPECT_TRUE(MetadataSerializer{}.save(
                    {.handle = handle, .type = AssetType::Material}, metadata_path(path)));
            }

            static void replace_texture(
                const std::filesystem::path& path, const bool restore_original = false) {
                std::filesystem::path source = "assets/textures/R-C.jpeg";
                if(restore_original) {
                    source = "assets/textures/awesomeface.png";
                }
                std::filesystem::copy_file(
                    std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / source, path,
                    std::filesystem::copy_options::overwrite_existing);
            }

            static void corrupt_texture(const std::filesystem::path& path) {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                output << "corrupted texture data";
            }

            std::filesystem::path add_mesh(const AssetHandle handle,
                const std::string_view primitive = R"({"attributes":{"POSITION":0},"indices":1})",
                const std::filesystem::path& relative_path = "meshes/test.gltf") const {
                const std::filesystem::path path = paths().assets() / relative_path;
                std::filesystem::create_directories(path.parent_path());
                write_mesh(path, primitive);
                EXPECT_TRUE(MetadataSerializer{}.save(
                    {.handle = handle, .type = AssetType::Mesh}, metadata_path(path)));
                return path;
            }

            std::filesystem::path add_external_mesh(const AssetHandle handle) const {
                const std::filesystem::path path = paths().assets() / "meshes/external.gltf";
                const std::filesystem::path buffer = paths().assets() / "meshes/external.bin";
                std::filesystem::create_directories(path.parent_path());
                write_external_mesh_buffer(buffer, false);
                std::ofstream output(path, std::ios::binary);
                output
                    << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"external.bin"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";
                output.close();
                EXPECT_TRUE(MetadataSerializer{}.save(
                    {.handle = handle, .type = AssetType::Mesh}, metadata_path(path)));
                return buffer;
            }

            static void write_mesh(
                const std::filesystem::path& path, const std::string_view primitive) {
                std::ofstream output(path, std::ios::binary);
                output
                    << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[)"
                    << primitive << "]}]}";
            }

            static void write_external_mesh_buffer(
                const std::filesystem::path& path, const bool modified) {
                std::array<std::uint8_t, 42> data{};
                data[14] = modified ? 0x00 : 0x80;
                data[15] = modified ? 0x40 : 0x3f;
                data[30] = 0x80;
                data[31] = 0x3f;
                data[38] = 0x01;
                data[40] = 0x02;
                std::ofstream output(path, std::ios::binary);
                output.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
            }

        private:
            std::filesystem::path m_root;
        };

        // Mesh／Texture 是身份占位符，只能比较身份，不能解引用或打印对象内容。
        class FakeRenderResourceFactory final: public RenderResourceFactory {
        public:
            GpuResourceResult<std::shared_ptr<Texture>> try_create_texture(
                const TextureData&) override {
                ++m_texture_creation_count;
                if(m_on_texture_creation) {
                    auto callback = std::exchange(m_on_texture_creation, {});
                    callback();
                }
                if(m_fail_texture_creation) {
                    return GpuResourceResult<std::shared_ptr<Texture>>::failure(m_failure_result);
                }

                auto owner = std::make_shared<std::uint8_t>(0);
                return GpuResourceResult<std::shared_ptr<Texture>>::success(
                    std::shared_ptr<Texture>(owner, reinterpret_cast<Texture*>(owner.get())));
            }

            GpuResourceResult<std::shared_ptr<Mesh>> try_create_mesh(
                const MeshData& data) override {
                ++m_mesh_creation_count;
                m_last_mesh_vertex_count = data.vertices.size();
                if(m_on_mesh_creation) {
                    auto callback = std::exchange(m_on_mesh_creation, {});
                    callback();
                }
                if(m_fail_mesh_creation) {
                    return GpuResourceResult<std::shared_ptr<Mesh>>::failure(m_failure_result);
                }

                auto owner = std::make_shared<std::uint8_t>(0);
                return GpuResourceResult<std::shared_ptr<Mesh>>::success(
                    std::shared_ptr<Mesh>(owner, reinterpret_cast<Mesh*>(owner.get())));
            }

            void fail_mesh_creation(const bool fail) { m_fail_mesh_creation = fail; }

            void fail_texture_creation(const bool fail) { m_fail_texture_creation = fail; }

            void set_failure_result(vk::Result result) { m_failure_result = result; }

            void on_next_mesh_creation(std::function<void()> callback) {
                m_on_mesh_creation = std::move(callback);
            }
            void on_next_texture_creation(std::function<void()> callback) {
                m_on_texture_creation = std::move(callback);
            }

            [[nodiscard]] std::size_t mesh_creation_count() const { return m_mesh_creation_count; }

            [[nodiscard]] std::size_t last_mesh_vertex_count() const {
                return m_last_mesh_vertex_count;
            }

            [[nodiscard]] std::size_t texture_creation_count() const {
                return m_texture_creation_count;
            }

        private:
            vk::Result m_failure_result = vk::Result::eErrorOutOfDeviceMemory;
            bool m_fail_mesh_creation = false;
            bool m_fail_texture_creation = false;
            std::size_t m_mesh_creation_count = 0;
            std::size_t m_last_mesh_vertex_count = 0;
            std::size_t m_texture_creation_count = 0;
            std::function<void()> m_on_mesh_creation;
            std::function<void()> m_on_texture_creation;
        };

        bool contains_handle(const std::vector<AssetHandle>& handles, const AssetHandle expected) {
            return std::ranges::find(handles, expected) != handles.end();
        }

        bool has_issue_containing(const AssetScanReport& report, const std::string_view text) {
            return std::ranges::any_of(report.issues, [&](const AssetScanIssue& issue) {
                return issue.message.find(text) != std::string::npos;
            });
        }
    }

    class AssetBackpressureTest: public ::testing::Test {
    protected:
        TemporaryProject project;
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler;
        AssetManager manager;

        AssetBackpressureTest(
            AssetAsyncLimits limits = {1, 1}, const std::size_t scheduler_capacity = 1)
            : scheduler(1, scheduler_capacity),
              manager(project.paths(), registry, factory, scheduler, limits) {}
        std::array<AssetHandle, 3> handles{AssetHandle(41), AssetHandle(42), AssetHandle(43)};

        void SetUp() override {
            for(const auto handle : handles)
                project.add_mesh(handle, R"({"attributes":{"POSITION":0},"indices":1})",
                    "meshes/" + std::to_string(handle.value()) + ".gltf");
            ASSERT_TRUE(manager.scan().succeeded());
        }

        std::vector<AssetHandle> drain() {
            std::vector<AssetHandle> published;
            for(int i = 0; i < 16; ++i) {
                scheduler.wait_idle();
                const auto batch = completed_handles(manager.process_completions());
                published.insert(published.end(), batch.begin(), batch.end());
                const auto status = manager.get_async_status();
                if(status.in_flight == 0 && status.queued == 0)
                    return published;
            }
            ADD_FAILURE() << "Asset queue did not drain within 16 owner cycles";
            return published;
        }

        void change_mesh(const int primitive_count) {
            const auto source = project.paths().assets() / "meshes/41.gltf";
            std::string primitives;
            for(int i = 0; i < primitive_count; ++i) {
                if(i != 0)
                    primitives += ',';
                primitives += R"({"attributes":{"POSITION":0},"indices":1})";
            }
            TemporaryProject::write_mesh(source, primitives);
            ASSERT_TRUE(manager.scan().succeeded());
        }

        std::filesystem::path artifact_path(const AssetHandle handle) const {
            return project.paths().cache() / "imported/mesh"
                   / (std::to_string(handle.value()) + ".bin");
        }
    };

    TEST_F(AssetBackpressureTest, BoundsInFlightAndQueueThenAllowsRejectedRetry) {
        BlockedWorker blocker(scheduler);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        ASSERT_TRUE(manager.import_mesh_async(handles[1]));
        EXPECT_FALSE(manager.import_mesh_async(handles[2]));
        EXPECT_EQ(manager.get_async_status().in_flight, 1);
        EXPECT_EQ(manager.get_async_status().queued, 1);
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_EQ(manager.get_async_status().queued, 1);
        blocker.release();
        const auto published = drain();
        ASSERT_EQ(published.size(), 2);
        EXPECT_EQ(published[0], handles[0]);
        EXPECT_EQ(published[1], handles[1]);
        ASSERT_TRUE(manager.import_mesh_async(handles[2]));
        EXPECT_EQ(drain(), std::vector<AssetHandle>{handles[2]});
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(AssetBackpressureTest, ModifiedResidentAssetsRetryAfterQueuePressureWithoutAnotherScan) {
        std::array<std::shared_ptr<Mesh>, 3> previous;
        for(std::size_t i = 0; i < handles.size(); ++i) {
            ASSERT_TRUE(manager.import_mesh(handles[i]));
            auto loaded = manager.load_mesh(handles[i]);
            ASSERT_TRUE(loaded);
            previous[i] = loaded.value();
        }
        BlockedWorker blocker(scheduler);
        for(const auto handle : handles)
            TemporaryProject::write_mesh(
                project.paths().assets() / ("meshes/" + std::to_string(handle.value()) + ".gltf"),
                R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
        const auto scan = manager.scan();
        ASSERT_EQ(scan.modified_assets.size(), handles.size());
        blocker.release();
        EXPECT_EQ(drain().size(), handles.size());
        for(std::size_t i = 0; i < handles.size(); ++i)
            EXPECT_NE(registry.resolve<Mesh>(handles[i]), previous[i]);
    }

    TEST_F(AssetBackpressureTest, RejectsInvalidAsyncLimits) {
        const auto previous_style = GTEST_FLAG_GET(death_test_style);
        GTEST_FLAG_SET(death_test_style, "threadsafe");
        EXPECT_DEATH((AssetManager{project.paths(), registry, factory, scheduler, {0, 1}}), "");
        EXPECT_DEATH((AssetManager{project.paths(), registry, factory, scheduler, {1, 0}}), "");
        GTEST_FLAG_SET(death_test_style, previous_style);
    }

    TEST_F(AssetBackpressureTest, TextureAndMaterialRefreshRetryLatestRevisionAfterPressure) {
        const AssetHandle texture(71), material(72);
        const auto texture_path = project.add_texture(texture);
        const auto material_path = project.add_material(material, "before");
        ASSERT_TRUE(manager.scan().succeeded());
        auto old_texture = loaded_asset(manager.load_texture(texture));
        auto old_material = loaded_asset(manager.load_material(material));
        ASSERT_TRUE(old_texture);
        ASSERT_TRUE(old_material);
        BlockedWorker blocker(scheduler);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        ASSERT_TRUE(manager.import_mesh_async(handles[1]));
        TemporaryProject::replace_texture(texture_path);
        ASSERT_TRUE(MaterialSerializer{}.save({.template_name = "intermediate"}, material_path));
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(MaterialSerializer{}.save({.template_name = "latest"}, material_path));
        ASSERT_TRUE(manager.scan().succeeded());
        blocker.release();
        const auto published = drain();
        EXPECT_NE(registry.resolve<Texture>(texture), old_texture);
        EXPECT_NE(registry.resolve<Material>(material), old_material);
        EXPECT_EQ(registry.resolve<Material>(material)->get_template_name(), "latest");
        EXPECT_NE(std::ranges::find(published, texture), published.end());
        EXPECT_NE(std::ranges::find(published, material), published.end());
    }

    TEST_F(AssetBackpressureTest, GlobalQueuePressureDefersAndCoalescesLatestRequest) {
        BlockedWorker blocker(scheduler);
        auto filler = scheduler.try_submit([] {});
        ASSERT_TRUE(filler);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        EXPECT_EQ(manager.get_async_status().in_flight, 0);
        EXPECT_EQ(manager.get_async_status().queued, 1);
        change_mesh(2);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        change_mesh(3);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        EXPECT_EQ(manager.get_async_status().queued, 1);
        for(int attempt = 0; attempt < 3; ++attempt) {
            EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
            EXPECT_EQ(manager.get_async_status().in_flight, 0);
            EXPECT_EQ(manager.get_async_status().queued, 1);
            EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[0])));
        }
        blocker.release();
        filler->get();
        EXPECT_EQ(drain(), std::vector<AssetHandle>{handles[0]});
        const auto artifact = MeshArtifact::load(artifact_path(handles[0]), handles[0]);
        ASSERT_TRUE(artifact);
        EXPECT_EQ(artifact->data.vertices.size(), 9);
    }

    TEST_F(AssetBackpressureTest, InFlightRevisionHasOnlyOneLatestQueuedSuccessor) {
        BlockedWorker blocker(scheduler);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        change_mesh(2);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        change_mesh(3);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        EXPECT_EQ(manager.get_async_status().in_flight, 1);
        EXPECT_EQ(manager.get_async_status().queued, 1);
        blocker.release();
        EXPECT_EQ(drain(), std::vector<AssetHandle>{handles[0]});
        const auto artifact = MeshArtifact::load(artifact_path(handles[0]), handles[0]);
        ASSERT_TRUE(artifact);
        EXPECT_EQ(artifact->data.vertices.size(), 9);
    }

    TEST_F(AssetBackpressureTest, ForceRequestDuringQueuedCacheCheckIsPreserved) {
        ASSERT_TRUE(manager.import_mesh(handles[0]));
        BlockedWorker blocker(scheduler);
        auto filler = scheduler.try_submit([] {});
        ASSERT_TRUE(filler);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        ASSERT_TRUE(manager.import_mesh_async(handles[0], MeshImportMode::Force));
        EXPECT_EQ(manager.get_async_status().queued, 1);
        EXPECT_EQ(manager.get_async_status().in_flight, 0);
        blocker.release();
        filler->get();
        EXPECT_EQ(drain(), std::vector<AssetHandle>{handles[0]});
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(AssetBackpressureTest, SameHandleSuccessorDoesNotBlockOtherHandles) {
        TaskScheduler roomy_scheduler(1, 4);
        AssetManager concurrent(project.paths(), registry, factory, roomy_scheduler, {2, 2});
        BlockedWorker blocker(roomy_scheduler);
        ASSERT_TRUE(concurrent.scan().succeeded());
        ASSERT_TRUE(concurrent.import_mesh_async(handles[0]));
        change_mesh(2);
        ASSERT_TRUE(concurrent.scan().succeeded());
        ASSERT_TRUE(concurrent.import_mesh_async(handles[0]));
        ASSERT_TRUE(concurrent.import_mesh_async(handles[1]));
        EXPECT_EQ(concurrent.get_async_status().in_flight, 2);
        EXPECT_EQ(concurrent.get_async_status().queued, 1);
        blocker.release();
        roomy_scheduler.wait_idle();
        EXPECT_EQ(completed_handles(concurrent.process_completions()),
            std::vector<AssetHandle>{handles[1]});
        roomy_scheduler.wait_idle();
        EXPECT_EQ(completed_handles(concurrent.process_completions()),
            std::vector<AssetHandle>{handles[0]});
        EXPECT_EQ(concurrent.get_async_status().in_flight, 0);
        EXPECT_EQ(concurrent.get_async_status().queued, 0);
        EXPECT_TRUE(std::filesystem::exists(artifact_path(handles[1])));
    }

    TEST_F(AssetBackpressureTest, ForceSuccessorRequiresCapacityAndCanBeRetried) {
        ASSERT_TRUE(manager.import_mesh(handles[0]));
        BlockedWorker blocker(scheduler);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        ASSERT_TRUE(manager.import_mesh_async(handles[1]));
        EXPECT_FALSE(manager.import_mesh_async(handles[0], MeshImportMode::Force));
        EXPECT_EQ(manager.get_async_status().in_flight, 1);
        EXPECT_EQ(manager.get_async_status().queued, 1);
        blocker.release();
        EXPECT_EQ(drain(), std::vector<AssetHandle>{handles[1]});
        ASSERT_TRUE(manager.import_mesh_async(handles[0], MeshImportMode::Force));
        EXPECT_EQ(drain(), std::vector<AssetHandle>{handles[0]});
    }

    TEST_F(AssetBackpressureTest, RemovedQueuedAssetIsDiscardedBeforeDispatch) {
        BlockedWorker blocker(scheduler);
        auto filler = scheduler.try_submit([] {});
        ASSERT_TRUE(filler);
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        const auto path = project.paths().assets() / "meshes/41.gltf";
        std::filesystem::remove(path);
        std::filesystem::remove(metadata_path(path));
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_EQ(manager.get_async_status().queued, 0);
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[0])));
        blocker.release();
        filler->get();
        EXPECT_TRUE(drain().empty());
    }

    TEST_F(AssetBackpressureTest, DestructionCancelsUndispatchedWorkWithoutWaitingForRoom) {
        BlockedWorker blocker(scheduler);
        auto filler = scheduler.try_submit([] {});
        ASSERT_TRUE(filler);
        {
            AssetManager temporary(project.paths(), registry, factory, scheduler, {1, 1});
            ASSERT_TRUE(temporary.scan().succeeded());
            ASSERT_TRUE(temporary.import_mesh_async(handles[0]));
            EXPECT_EQ(temporary.get_async_status().queued, 1);
            EXPECT_EQ(temporary.get_async_status().in_flight, 0);
        }
        blocker.release();
        filler->get();
        scheduler.wait_idle();
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[0])));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    class AssetCompletionBudgetTest: public AssetBackpressureTest {
    protected:
        AssetCompletionBudgetTest() : AssetBackpressureTest({3, 4}, 8) {}

        static constexpr AssetCompletionBudget one_result{
            .max_results = 1, .max_time = std::chrono::seconds(1)};
    };

    TEST_F(AssetCompletionBudgetTest, UnpublishedResultsRetainSlotsAndCountBudget) {
        constexpr AssetHandle fourth(44);
        project.add_mesh(fourth, R"({"attributes":{"POSITION":0},"indices":1})", "meshes/44.gltf");
        ASSERT_TRUE(manager.scan().succeeded());
        for(const auto handle : handles)
            ASSERT_TRUE(manager.import_mesh_async(handle));
        ASSERT_TRUE(manager.import_mesh_async(fourth));
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions({.max_results = 0})).empty());
        EXPECT_TRUE(completed_handles(
            manager.process_completions({.max_time = std::chrono::nanoseconds(0)}))
                .empty());
        EXPECT_EQ(manager.get_async_status().in_flight, 3);
        EXPECT_EQ(manager.get_async_status().queued, 1);
        for(const auto handle : handles)
            EXPECT_FALSE(std::filesystem::exists(artifact_path(handle)));
        EXPECT_EQ(completed_handles(manager.process_completions(one_result)),
            std::vector<AssetHandle>{handles[0]});
        EXPECT_EQ(manager.get_async_status().in_flight, 3);
        EXPECT_EQ(manager.get_async_status().queued, 0);
        scheduler.wait_idle();
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[1])));
        EXPECT_FALSE(std::filesystem::exists(artifact_path(fourth)));
        for(const auto handle : {handles[1], handles[2], fourth})
            EXPECT_EQ(completed_handles(manager.process_completions(one_result)),
                std::vector<AssetHandle>{handle});
        EXPECT_EQ(manager.get_async_status().in_flight, 0);
    }

    TEST_F(AssetCompletionBudgetTest, TinyPositiveTimeBudgetStillMakesBoundedProgress) {
        for(const auto handle : handles)
            ASSERT_TRUE(manager.import_mesh_async(handle));
        scheduler.wait_idle();
        for(const auto handle : handles) {
            const auto published = completed_handles(manager.process_completions(
                {.max_results = 3, .max_time = std::chrono::nanoseconds(1)}));
            EXPECT_EQ(published, std::vector<AssetHandle>{handle});
        }
        EXPECT_EQ(manager.get_async_status().in_flight, 0);
    }

    TEST_F(AssetCompletionBudgetTest, ZeroBudgetCanDispatchWithoutConsumingResults) {
        BlockedWorker blocker(scheduler);
        for(std::size_t i = 0; i < scheduler.get_queue_capacity(); ++i)
            ASSERT_TRUE(scheduler.try_submit([] {}));
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        EXPECT_EQ(manager.get_async_status().in_flight, 0);
        EXPECT_EQ(manager.get_async_status().queued, 1);
        blocker.release();
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions({.max_results = 0})).empty());
        EXPECT_EQ(manager.get_async_status().in_flight, 1);
        EXPECT_EQ(manager.get_async_status().queued, 0);
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(
            manager.process_completions({.max_time = std::chrono::nanoseconds(-1)}))
                .empty());
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[0])));
        EXPECT_EQ(manager.get_async_status().in_flight, 1);
        EXPECT_EQ(completed_handles(manager.process_completions(one_result)),
            std::vector<AssetHandle>{handles[0]});
    }

    TEST_F(AssetCompletionBudgetTest, BudgetRetainsForceSuccessorAndDeduplication) {
        ASSERT_TRUE(manager.import_mesh(handles[0]));
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions({.max_results = 0})).empty());
        ASSERT_TRUE(manager.import_mesh_async(handles[0], MeshImportMode::Force));
        ASSERT_TRUE(manager.import_mesh_async(handles[0], MeshImportMode::Force));
        EXPECT_EQ(manager.get_async_status().in_flight, 1);
        EXPECT_EQ(manager.get_async_status().queued, 1);
        EXPECT_TRUE(completed_handles(manager.process_completions(one_result)).empty());
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions({.max_results = 0})).empty());
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        ASSERT_TRUE(manager.import_mesh_async(handles[0], MeshImportMode::Force));
        EXPECT_EQ(manager.get_async_status().in_flight, 1);
        EXPECT_EQ(manager.get_async_status().queued, 0);
        EXPECT_EQ(completed_handles(manager.process_completions(one_result)),
            std::vector<AssetHandle>{handles[0]});
        EXPECT_EQ(manager.get_async_status().in_flight, 0);
    }

    TEST_F(AssetCompletionBudgetTest, StaleFailedAndInspectionResultsEachConsumeBudget) {
        const auto bad_path = project.paths().assets() / "meshes/42.gltf";
        std::ofstream(bad_path, std::ios::trunc) << "invalid mesh";
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        ASSERT_TRUE(manager.import_mesh_async(handles[1]));
        ASSERT_TRUE(manager.import_mesh(handles[2]));
        ASSERT_TRUE(manager.import_mesh_async(handles[2]));
        scheduler.wait_idle();
        const auto removed = project.paths().assets() / "meshes/41.gltf";
        std::filesystem::remove(removed);
        std::filesystem::remove(metadata_path(removed));
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_TRUE(completed_handles(manager.process_completions(one_result)).empty());
        EXPECT_EQ(manager.get_async_status().in_flight, 2);
        EXPECT_TRUE(completed_handles(manager.process_completions(one_result)).empty());
        EXPECT_EQ(manager.get_async_status().in_flight, 1);
        EXPECT_TRUE(completed_handles(manager.process_completions(one_result)).empty());
        EXPECT_EQ(manager.get_async_status().in_flight, 0);
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[0])));
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[1])));
        EXPECT_TRUE(std::filesystem::exists(artifact_path(handles[2])));
    }

    TEST_F(AssetCompletionBudgetTest, MeshAndTextureShareOnePublicationBudget) {
        constexpr AssetHandle texture_handle(84);
        const auto texture_path = project.add_texture(texture_handle);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto original = loaded_asset(manager.load_texture(texture_handle));
        ASSERT_TRUE(original);
        TemporaryProject::replace_texture(texture_path);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh_async(handles[0]));
        ASSERT_TRUE(manager.import_mesh_async(handles[1]));
        scheduler.wait_idle();
        EXPECT_EQ(completed_handles(manager.process_completions(one_result)),
            std::vector<AssetHandle>{texture_handle});
        EXPECT_TRUE(registry.resolve<Texture>(texture_handle) != original);
        EXPECT_EQ(factory.texture_creation_count(), 2);
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[0])));
        EXPECT_EQ(completed_handles(manager.process_completions(one_result)),
            std::vector<AssetHandle>{handles[0]});
        EXPECT_EQ(completed_handles(manager.process_completions(one_result)),
            std::vector<AssetHandle>{handles[1]});
    }

    TEST_F(AssetCompletionBudgetTest, ReentrantPublicationIsRejectedAndNewRevisionWaits) {
        ASSERT_TRUE(manager.import_mesh(handles[0]));
        const auto original = loaded_asset(manager.load_mesh(handles[0]));
        ASSERT_TRUE(original);
        change_mesh(2);
        ASSERT_TRUE(manager.import_mesh_async(handles[1]));
        ASSERT_TRUE(manager.import_mesh_async(handles[2]));
        scheduler.wait_idle();
        factory.on_next_mesh_creation([&] {
            EXPECT_TRUE(completed_handles(manager.process_completions(one_result)).empty());
            change_mesh(3);
            EXPECT_EQ(manager.get_async_status().in_flight, 3);
            EXPECT_EQ(manager.get_async_status().queued, 1);
        });
        EXPECT_EQ(completed_handles(manager.process_completions(one_result)),
            std::vector<AssetHandle>{handles[0]});
        EXPECT_TRUE(registry.resolve<Mesh>(handles[0]) == original);
        EXPECT_EQ(manager.get_async_status().in_flight, 3);
        const auto published = drain();
        EXPECT_EQ(published.size(), 3);
        EXPECT_TRUE(registry.resolve<Mesh>(handles[0]) != original);
        EXPECT_EQ(factory.last_mesh_vertex_count(), 9);
        EXPECT_EQ(factory.mesh_creation_count(), 3);
    }

    TEST_F(AssetCompletionBudgetTest, DeviceLossStopsBatchAndReleasesOnlyConsumedTask) {
        ASSERT_TRUE(manager.import_mesh(handles[0]));
        const auto original = loaded_asset(manager.load_mesh(handles[0]));
        ASSERT_TRUE(original);
        for(const auto handle : handles)
            ASSERT_TRUE(manager.import_mesh_async(handle, MeshImportMode::Force));
        scheduler.wait_idle();
        factory.fail_mesh_creation(true);
        factory.set_failure_result(vk::Result::eErrorDeviceLost);

        const auto result =
            manager.process_completions({.max_results = 3, .max_time = std::chrono::seconds(1)});
        ASSERT_FALSE(result);
        EXPECT_EQ(
            result.error().code, (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        EXPECT_EQ(manager.get_async_status().in_flight, 2);
        EXPECT_EQ(registry.resolve<Mesh>(handles[0]), original);
        EXPECT_TRUE(std::filesystem::exists(artifact_path(handles[0])));
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[1])));
        EXPECT_FALSE(std::filesystem::exists(artifact_path(handles[2])));
    }

    TEST_F(AssetCompletionBudgetTest, UnexpectedGpuExceptionPropagatesAndReleasesTaskSlot) {
        ASSERT_TRUE(manager.import_mesh(handles[0]));
        const auto original = loaded_asset(manager.load_mesh(handles[0]));
        ASSERT_TRUE(original);
        change_mesh(2);
        scheduler.wait_idle();
        factory.on_next_mesh_creation([] { throw 7; });
        EXPECT_THROW(completed_handles(manager.process_completions(one_result)), int);
        EXPECT_EQ(manager.get_async_status().in_flight, 0);
        EXPECT_TRUE(registry.resolve<Mesh>(handles[0]) == original);
        ASSERT_TRUE(manager.import_mesh_async(handles[0], MeshImportMode::Force));
        EXPECT_EQ(drain(), std::vector<AssetHandle>{handles[0]});
        EXPECT_TRUE(registry.resolve<Mesh>(handles[0]) != original);
    }

    TEST_F(AssetCompletionBudgetTest, DestructionDiscardsCompletedUnpublishedCandidate) {
        ASSERT_TRUE(manager.import_mesh(handles[0]));
        const auto original = loaded_asset(manager.load_mesh(handles[0]));
        ASSERT_TRUE(original);
        {
            AssetManager temporary(project.paths(), registry, factory, scheduler);
            ASSERT_TRUE(temporary.scan().succeeded());
            TemporaryProject::write_mesh(project.paths().assets() / "meshes/41.gltf",
                R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
            ASSERT_TRUE(temporary.scan().succeeded());
            scheduler.wait_idle();
            EXPECT_EQ(temporary.get_async_status().in_flight, 1);
            EXPECT_TRUE(
                completed_handles(temporary.process_completions({.max_results = 0})).empty());
        }
        EXPECT_TRUE(registry.resolve<Mesh>(handles[0]) == original);
        EXPECT_EQ(factory.mesh_creation_count(), 1);
        const auto artifact = MeshArtifact::load(artifact_path(handles[0]), handles[0]);
        ASSERT_TRUE(artifact);
        EXPECT_EQ(artifact->data.vertices.size(), 3);
    }

    class MeshAsyncImportTest: public ::testing::Test {
    protected:
        using Mode = MeshImportMode;
        static constexpr AssetHandle handle{42};
        TemporaryProject project;
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler{1};
        AssetManager manager{project.paths(), registry, factory, scheduler};
        std::filesystem::path source;

        void SetUp() override {
            source = project.add_mesh(handle);
            ASSERT_TRUE(manager.scan().succeeded());
        }
        std::vector<AssetHandle> complete() {
            scheduler.wait_idle();
            return completed_handles(manager.process_completions());
        }
        std::filesystem::path artifact_path() const {
            return project.paths().cache() / "imported/mesh/42.bin";
        }
    };

    TEST_F(MeshAsyncImportTest, EnsureLoadedValidatesTypeAndPropagatesUnexpectedFactoryExceptions) {
        EXPECT_FALSE(manager.ensure_loaded({}, AssetType::Mesh));
        EXPECT_FALSE(manager.ensure_loaded(AssetHandle(999), AssetType::Mesh));
        EXPECT_FALSE(manager.ensure_loaded(handle, AssetType::Material));
        EXPECT_FALSE(manager.ensure_loaded(handle, AssetType::Unknown));
        EXPECT_FALSE(manager.ensure_loaded(handle, AssetType::Mesh));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        ASSERT_TRUE(manager.import_mesh(handle));
        factory.on_next_mesh_creation([] { throw std::runtime_error("test allocation failure"); });
        EXPECT_THROW(
            static_cast<void>(manager.ensure_loaded(handle, AssetType::Mesh)), std::runtime_error);
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_TRUE(manager.ensure_loaded(handle, AssetType::Mesh));
        EXPECT_EQ(factory.mesh_creation_count(), 2);
        EXPECT_TRUE(manager.ensure_loaded(handle, AssetType::Mesh));
        EXPECT_FALSE(manager.ensure_loaded(handle, AssetType::Texture));
        EXPECT_EQ(factory.mesh_creation_count(), 2);
    }

    TEST_F(MeshAsyncImportTest, DeviceLostEscapesLoadAndCompletionBoundaries) {
        ASSERT_TRUE(manager.import_mesh(handle));
        factory.fail_mesh_creation(true);
        factory.set_failure_result(vk::Result::eErrorDeviceLost);
        {
            const auto result = manager.ensure_loaded(handle, AssetType::Mesh);
            ASSERT_FALSE(result);
            EXPECT_TRUE(is_device_lost(result.error()));
            EXPECT_EQ(result.error().code,
                (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        }
        EXPECT_FALSE(registry.contains(handle));

        factory.fail_mesh_creation(false);
        const auto original = loaded_asset(manager.load_mesh(handle));
        ASSERT_TRUE(original);
        factory.fail_mesh_creation(true);
        TemporaryProject::write_mesh(source,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        const auto failed_completion = manager.process_completions();
        ASSERT_FALSE(failed_completion);
        EXPECT_EQ(failed_completion.error().code,
            (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        EXPECT_EQ(manager.get_async_status().in_flight, 0u);
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == original);
        TemporaryProject::write_mesh(source, R"({"attributes":{"POSITION":0},"indices":1})");
        const auto failed_import = manager.import_mesh(handle);
        ASSERT_FALSE(failed_import);
        EXPECT_EQ(failed_import.error().code,
            (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
    }

    TEST(AssetManagerTest, DeviceLostInMaterialTextureIsNotConvertedToMissingAsset) {
        const TemporaryProject project;
        constexpr AssetHandle texture_handle(84);
        constexpr AssetHandle material_handle(85);
        project.add_texture(texture_handle);
        const auto path = project.add_material(material_handle, "test_template");
        MaterialData data{
            .template_name = "test_template", .texture_properties = {{"albedo", texture_handle}}};
        ASSERT_TRUE(MaterialSerializer{}.save(data, path));
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        factory.fail_texture_creation(true);
        factory.set_failure_result(vk::Result::eErrorDeviceLost);
        {
            const auto result = manager.ensure_loaded(texture_handle, AssetType::Texture);
            ASSERT_FALSE(result);
            EXPECT_TRUE(is_device_lost(result.error()));
            EXPECT_EQ(result.error().code,
                (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        }
        {
            const auto result = manager.ensure_loaded(material_handle, AssetType::Material);
            ASSERT_FALSE(result);
            EXPECT_TRUE(is_device_lost(result.error()));
            EXPECT_EQ(result.error().code,
                (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        }
        {
            const auto result = manager.reload_material(material_handle);
            ASSERT_FALSE(result);
            EXPECT_TRUE(is_device_lost(result.error()));
            EXPECT_EQ(result.error().code,
                (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        }
        {
            const auto result = manager.update_material(material_handle, data);
            ASSERT_FALSE(result);
            EXPECT_TRUE(is_device_lost(result.error()));
            EXPECT_EQ(result.error().code,
                (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        }
        EXPECT_FALSE(registry.contains(material_handle));

        factory.fail_texture_creation(false);
        const auto original = loaded_asset(manager.load_texture(texture_handle));
        ASSERT_TRUE(original);
        factory.fail_texture_creation(true);
        TemporaryProject::replace_texture(project.paths().assets() / "textures/test.png");
        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        const auto failed_completion = manager.process_completions();
        ASSERT_FALSE(failed_completion);
        EXPECT_EQ(failed_completion.error().code,
            (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        EXPECT_EQ(manager.get_async_status().in_flight, 0u);
        EXPECT_TRUE(registry.resolve<Texture>(texture_handle) == original);
    }

    TEST_F(MeshAsyncImportTest, ReportsOnlyPublishedArtifactsNotReuseFailureOrStaleWork) {
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        ASSERT_TRUE(manager.import_mesh_async(handle));
        EXPECT_EQ(complete(), std::vector<AssetHandle>{handle});
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        ASSERT_TRUE(manager.import_mesh_async(handle));
        EXPECT_TRUE(complete().empty());
        std::ofstream(source, std::ios::trunc) << "invalid gltf";
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh_async(handle));
        EXPECT_TRUE(complete().empty());
        TemporaryProject::write_mesh(source, R"({"attributes":{"POSITION":0},"indices":1})");
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh_async(handle, Mode::Force));
        scheduler.wait_idle();
        std::ofstream(source, std::ios::app) << " ";
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
    }

    TEST_F(MeshAsyncImportTest, DeduplicatesAndPublishesUnloadedMeshOnlyOnOwner) {
        ASSERT_TRUE(manager.import_mesh_async(handle));
        EXPECT_TRUE(manager.import_mesh_async(handle));
        EXPECT_FALSE(manager.import_mesh(handle));
        scheduler.wait_idle();
        EXPECT_FALSE(std::filesystem::exists(artifact_path()));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        completed_handles(manager.process_completions());
        EXPECT_TRUE(MeshArtifact::load(artifact_path(), handle));
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        ASSERT_TRUE(loaded_asset(manager.load_mesh(handle)));
        EXPECT_EQ(factory.mesh_creation_count(), 1);
    }

    TEST_F(MeshAsyncImportTest, ReusesCurrentCacheAndRebuildsCorruptedOrMissingCache) {
        ASSERT_TRUE(manager.import_mesh(handle));
        const auto previous = loaded_asset(manager.load_mesh(handle));
        ASSERT_TRUE(previous);
        const auto stamp = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
        std::filesystem::last_write_time(artifact_path(), stamp);
        ASSERT_TRUE(manager.import_mesh_async(handle));
        complete();
        EXPECT_EQ(std::filesystem::last_write_time(artifact_path()), stamp);
        EXPECT_EQ(factory.mesh_creation_count(), 1);
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == previous);
        std::ofstream(artifact_path(), std::ios::trunc) << "broken";
        ASSERT_TRUE(manager.import_mesh_async(handle));
        complete();
        EXPECT_TRUE(MeshArtifact::load(artifact_path(), handle));
        std::filesystem::remove(artifact_path());
        ASSERT_TRUE(manager.import_mesh_async(handle));
        complete();
        EXPECT_TRUE(MeshArtifact::load(artifact_path(), handle));
    }

    TEST_F(MeshAsyncImportTest, ChangedSourceRebuildsWithoutAllocatingUnusedRuntime) {
        ASSERT_TRUE(manager.import_mesh(handle));
        TemporaryProject::write_mesh(source,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
        ASSERT_TRUE(contains_handle(manager.scan().modified_assets, handle));
        ASSERT_TRUE(manager.import_mesh_async(handle));
        complete();
        auto artifact = MeshArtifact::load(artifact_path(), handle);
        ASSERT_TRUE(artifact);
        EXPECT_EQ(artifact->data.vertices.size(), 6);
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST_F(MeshAsyncImportTest, FailedReimportPreservesArtifactAndCanBeRetried) {
        ASSERT_TRUE(manager.import_mesh(handle));
        std::ofstream(source, std::ios::trunc) << "invalid gltf";
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh_async(handle));
        complete();
        EXPECT_TRUE(MeshArtifact::load(artifact_path(), handle));
        EXPECT_EQ(factory.mesh_creation_count(), 0);
        TemporaryProject::write_mesh(source, R"({"attributes":{"POSITION":0},"indices":1})");
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh_async(handle, Mode::Force));
        complete();
        EXPECT_TRUE(MeshArtifact::load(artifact_path(), handle));
        EXPECT_FALSE(manager.import_mesh_async(AssetHandle(999)));
    }

    TEST_F(MeshAsyncImportTest, ForceRebuildKeepsOldRuntimeOnGpuFailure) {
        ASSERT_TRUE(manager.import_mesh(handle));
        const auto previous = loaded_asset(manager.load_mesh(handle));
        ASSERT_TRUE(previous);
        factory.fail_mesh_creation(true);
        ASSERT_TRUE(manager.import_mesh_async(handle, Mode::Force));
        EXPECT_EQ(complete(), std::vector<AssetHandle>{handle});
        EXPECT_TRUE(MeshArtifact::load(artifact_path(), handle));
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == previous);
        EXPECT_EQ(factory.mesh_creation_count(), 2);
    }

    TEST_F(MeshAsyncImportTest, ForceRequestDuringCacheCheckIsNotLost) {
        ASSERT_TRUE(manager.import_mesh(handle));
        ASSERT_TRUE(loaded_asset(manager.load_mesh(handle)));
        ASSERT_TRUE(manager.import_mesh_async(handle));
        scheduler.wait_idle();
        // Worker 已复用缓存，但 owner 尚未消费，此时仍需兑现强制重建。
        ASSERT_TRUE(manager.import_mesh_async(handle, Mode::Force));
        ASSERT_TRUE(manager.import_mesh_async(handle, Mode::Force));
        completed_handles(manager.process_completions());
        complete();
        EXPECT_EQ(factory.mesh_creation_count(), 2);
    }

    TEST_F(MeshAsyncImportTest, OldRequestCannotOverwriteNewRevisionAfterMove) {
        std::promise<void> release;
        const auto gate = release.get_future().share();
        auto blocker = scheduler.try_submit([gate] { gate.wait(); });
        ASSERT_TRUE(blocker);
        EXPECT_TRUE(manager.import_mesh_async(handle));
        const auto before = manager.get_database().get_revision(handle);
        const auto report = manager.move_asset(handle, "moved/new.gltf");
        EXPECT_TRUE(report.succeeded());
        EXPECT_NE(manager.get_database().get_revision(handle), before);
        EXPECT_TRUE(manager.import_mesh_async(handle));
        release.set_value();
        complete();
        complete();
        blocker->get();
        const auto artifact = MeshArtifact::load(artifact_path(), handle);
        ASSERT_TRUE(artifact);
        EXPECT_EQ(artifact->source_inputs.files.front().relative_path, "moved/new.gltf");
        EXPECT_EQ(factory.mesh_creation_count(), 0);
    }

    TEST(AssetManagerTest, ImportsAndLoadsMeshArtifactByAssetHandle) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        EXPECT_FALSE(manager.load_mesh(handle));
        ASSERT_TRUE(manager.import_mesh(handle));
        const std::shared_ptr<Mesh> mesh = loaded_asset(manager.load_mesh(handle));

        ASSERT_NE(mesh, nullptr);
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == mesh);
        EXPECT_TRUE(loaded_asset(manager.load_mesh(handle)) == mesh);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 1);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 3);
        EXPECT_TRUE(std::filesystem::is_regular_file(
            project.paths().cache() / "imported" / "mesh" / "42.bin"));
    }

    TEST(AssetManagerTest, MovesSourceAndMetadataWithoutChangingHandle) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path source = project.add_material(handle, "move_test");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().succeeded());
        const std::shared_ptr<Material> original = loaded_asset(manager.load_material(handle));
        ASSERT_NE(original, nullptr);

        const AssetScanReport report = manager.move_asset(handle, "renamed/moved.mat");

        EXPECT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_TRUE(contains_handle(report.modified_assets, handle));
        EXPECT_FALSE(std::filesystem::exists(source));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        const std::filesystem::path moved = project.paths().assets() / "renamed/moved.mat";
        EXPECT_TRUE(std::filesystem::is_regular_file(moved));
        EXPECT_TRUE(std::filesystem::is_regular_file(metadata_path(moved)));
        EXPECT_EQ(MetadataSerializer{}.load(metadata_path(moved)).value().handle, handle);
        ASSERT_NE(manager.get_database().find(handle), nullptr);
        EXPECT_EQ(manager.get_database().find(handle)->path, "renamed/moved.mat");
        EXPECT_EQ(registry.resolve<Material>(handle), original);
        task_scheduler.wait_idle();
        EXPECT_EQ(registry.resolve<Material>(handle), original);
        EXPECT_EQ(
            completed_handles(manager.process_completions()), std::vector<AssetHandle>{handle});
        EXPECT_NE(registry.resolve<Material>(handle), original);
    }

    TEST(AssetManagerTest, MovesLoadedMeshAndRepublishesArtifact) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path source = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(manager.import_mesh(handle));
        const std::shared_ptr<Mesh> original = loaded_asset(manager.load_mesh(handle));
        ASSERT_NE(original, nullptr);

        const AssetScanReport report = manager.move_asset(handle, "renamed/moved.gltf");

        EXPECT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_TRUE(contains_handle(report.modified_assets, handle));
        EXPECT_FALSE(std::filesystem::exists(source));
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == original);

        task_scheduler.wait_idle();
        completed_handles(manager.process_completions());

        EXPECT_TRUE(registry.resolve<Mesh>(handle) != original);
        const auto artifact =
            MeshArtifact::load(project.paths().cache() / "imported/mesh/42.bin", handle);
        ASSERT_TRUE(artifact.has_value());
        ASSERT_FALSE(artifact->source_inputs.files.empty());
        EXPECT_EQ(artifact->source_inputs.files.front().relative_path, "renamed/moved.gltf");
    }

    TEST(AssetManagerTest, RejectsMoveWhenDestinationAlreadyExists) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path source = project.add_material(handle, "move_test");
        const std::filesystem::path target = project.paths().assets() / "occupied.mat";
        EXPECT_TRUE(MaterialSerializer{}.save(
            {.template_name = "occupied", .texture_properties = {}}, target));
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().snapshot_updated);

        const AssetScanReport report = manager.move_asset(handle, "occupied.mat");

        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(report, "destination already exists"));
        EXPECT_TRUE(std::filesystem::is_regular_file(source));
        EXPECT_TRUE(std::filesystem::is_regular_file(metadata_path(source)));
        EXPECT_EQ(manager.get_database().find(handle)->path, "materials/test.mat");
    }

    TEST(AssetManagerTest, RejectsMoveOutsideAssetRoot) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path source = project.add_material(handle, "move_test");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().snapshot_updated);

        const AssetScanReport report = manager.move_asset(handle, "../outside.mat");

        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(report, "project-relative file path inside assets"));
        EXPECT_TRUE(std::filesystem::is_regular_file(source));
        EXPECT_TRUE(std::filesystem::is_regular_file(metadata_path(source)));
        EXPECT_FALSE(std::filesystem::exists(project.paths().root() / "outside.mat"));
    }

    TEST(AssetManagerTest, RollsBackMoveWhenScanFindsIdentityConflict) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path source = project.add_material(handle, "move_test");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().succeeded());

        const std::filesystem::path duplicate = project.paths().assets() / "duplicate.mat";
        EXPECT_TRUE(MaterialSerializer{}.save(
            {.template_name = "duplicate", .texture_properties = {}}, duplicate));
        EXPECT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Material}, metadata_path(duplicate)));

        const AssetScanReport report = manager.move_asset(handle, "renamed/moved.mat");

        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(report, "duplicate guid 42"));
        EXPECT_TRUE(has_issue_containing(report, "move was rolled back"));
        EXPECT_TRUE(std::filesystem::is_regular_file(source));
        EXPECT_TRUE(std::filesystem::is_regular_file(metadata_path(source)));
        EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "renamed/moved.mat"));
        EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "renamed"));
        ASSERT_NE(manager.get_database().find(handle), nullptr);
        EXPECT_EQ(manager.get_database().find(handle)->path, "materials/test.mat");

        ASSERT_TRUE(std::filesystem::remove(duplicate));
        ASSERT_TRUE(std::filesystem::remove(metadata_path(duplicate)));
        const auto retried = manager.move_asset(handle, "renamed/moved.mat");
        ASSERT_TRUE(retried.succeeded());
        ASSERT_TRUE(retried.snapshot_updated);
        EXPECT_EQ(manager.get_database().find(handle)->path, "renamed/moved.mat");
        EXPECT_FALSE(std::filesystem::exists(source));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        EXPECT_TRUE(
            std::filesystem::is_regular_file(project.paths().assets() / "renamed/moved.mat"));
        EXPECT_TRUE(
            std::filesystem::is_regular_file(project.paths().assets() / "renamed/moved.mat.meta"));
    }

    TEST(AssetManagerTest, RebuildsCorruptedMeshArtifactDuringImport) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        project.add_mesh(handle);
        const std::filesystem::path artifact_path =
            project.paths().cache() / "imported" / "mesh" / "42.bin";
        {
            AssetRegistry registry;
            FakeRenderResourceFactory resource_factory;
            TaskScheduler task_scheduler(1);
            AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);
            ASSERT_TRUE(manager.scan().snapshot_updated);
            ASSERT_TRUE(manager.import_mesh(handle));
            ASSERT_TRUE(loaded_asset(manager.load_mesh(handle)));
        }
        {
            std::ofstream output(artifact_path, std::ios::binary | std::ios::trunc);
            output << "corrupted";
        }

        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().snapshot_updated);

        EXPECT_FALSE(manager.load_mesh(handle));
        ASSERT_TRUE(manager.import_mesh(handle));
        EXPECT_TRUE(loaded_asset(manager.load_mesh(handle)));
        EXPECT_GT(std::filesystem::file_size(artifact_path), 9u);
    }

    TEST(AssetManagerTest, LoadsPublishedMeshArtifactWithoutReadingSource) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(manager.import_mesh(handle));
        {
            std::ofstream output(mesh_path, std::ios::binary | std::ios::trunc);
            output << "source is no longer readable as glTF";
        }

        const std::shared_ptr<Mesh> mesh = loaded_asset(manager.load_mesh(handle));

        EXPECT_NE(mesh, nullptr);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 3);
    }

    TEST(AssetManagerTest, RefreshesModifiedLoadedMeshAfterScan) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(manager.import_mesh(handle));
        const std::shared_ptr<Mesh> original = loaded_asset(manager.load_mesh(handle));
        ASSERT_NE(original, nullptr);
        TemporaryProject::write_mesh(mesh_path,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 1);

        task_scheduler.wait_idle();
        completed_handles(manager.process_completions());

        const std::shared_ptr<Mesh> modified = registry.resolve<Mesh>(handle);
        ASSERT_NE(modified, nullptr);
        EXPECT_NE(modified, original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 2);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 6);
    }

    TEST(AssetManagerTest, RefreshesLoadedMeshWhenExternalBufferChanges) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path dependency = project.add_external_mesh(handle);
        {
            AssetRegistry registry;
            FakeRenderResourceFactory resource_factory;
            TaskScheduler task_scheduler(1);
            AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);
            ASSERT_TRUE(manager.scan().succeeded());
            ASSERT_TRUE(manager.import_mesh(handle));
            ASSERT_TRUE(loaded_asset(manager.load_mesh(handle)));
        }

        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh(handle));
        const std::shared_ptr<Mesh> original = loaded_asset(manager.load_mesh(handle));
        ASSERT_NE(original, nullptr);
        EXPECT_EQ(std::vector<std::filesystem::path>(
                      manager.get_database().get_import_dependencies(handle).begin(),
                      manager.get_database().get_import_dependencies(handle).end()),
            (std::vector<std::filesystem::path>{"meshes/external.bin"}));

        const auto previous_write_time = std::filesystem::last_write_time(dependency);
        TemporaryProject::write_external_mesh_buffer(dependency, true);
        std::filesystem::last_write_time(dependency, previous_write_time + std::chrono::seconds(1));

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == original);
        task_scheduler.wait_idle();
        completed_handles(manager.process_completions());

        EXPECT_TRUE(registry.resolve<Mesh>(handle) != original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 2);
        EXPECT_EQ(resource_factory.last_mesh_vertex_count(), 3);
    }

    TEST(AssetManagerTest, DiscardsMeshCandidateWhenRevisionChangesBeforePublication) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(manager.import_mesh(handle));
        const AssetRevision requested_revision = manager.get_database().get_revision(handle);
        bool rescan_detected_change = false;
        resource_factory.on_next_mesh_creation([&] {
            TemporaryProject::write_mesh(mesh_path,
                R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
            const AssetScanReport report = manager.scan();
            rescan_detected_change = contains_handle(report.modified_assets, handle);
        });

        const auto stale_candidate = manager.load_mesh(handle);

        EXPECT_TRUE(rescan_detected_change);
        ASSERT_FALSE(stale_candidate);
        EXPECT_FALSE(stale_candidate.error().message.empty());
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_GT(manager.get_database().get_revision(handle), requested_revision);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 1);
    }

    TEST(AssetManagerTest, PublishesOnlyLatestBackgroundMeshRevision) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(manager.import_mesh(handle));
        const std::shared_ptr<Mesh> original = loaded_asset(manager.load_mesh(handle));
        ASSERT_NE(original, nullptr);

        std::promise<void> release_worker;
        const std::shared_future<void> worker_gate = release_worker.get_future().share();
        auto blocker = task_scheduler.try_submit([worker_gate] { worker_gate.wait(); });
        ASSERT_TRUE(blocker);

        TemporaryProject::write_mesh(mesh_path,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
        const AssetScanReport first_refresh = manager.scan();
        ASSERT_TRUE(contains_handle(first_refresh.modified_assets, handle));
        const AssetRevision first_revision = manager.get_database().get_revision(handle);

        TemporaryProject::write_mesh(mesh_path,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");
        const AssetScanReport second_refresh = manager.scan();
        ASSERT_TRUE(contains_handle(second_refresh.modified_assets, handle));
        EXPECT_GT(manager.get_database().get_revision(handle), first_revision);
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == original);

        release_worker.set_value();
        task_scheduler.wait_idle();
        blocker->get();
        completed_handles(manager.process_completions());
        task_scheduler.wait_idle();
        completed_handles(manager.process_completions());

        EXPECT_TRUE(registry.resolve<Mesh>(handle) != original);
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
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(manager.import_mesh(handle));
        const std::shared_ptr<Mesh> original = loaded_asset(manager.load_mesh(handle));
        ASSERT_NE(original, nullptr);
        {
            std::ofstream output(mesh_path, std::ios::binary);
            output << "corrupted glTF with a different file size";
        }

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        task_scheduler.wait_idle();
        completed_handles(manager.process_completions());
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 1);
    }

    TEST(AssetManagerTest, KeepsPreviousMeshWhenRuntimeCreationFails) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path mesh_path = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(manager.import_mesh(handle));
        const std::shared_ptr<Mesh> original = loaded_asset(manager.load_mesh(handle));
        ASSERT_NE(original, nullptr);
        resource_factory.fail_mesh_creation(true);
        TemporaryProject::write_mesh(mesh_path,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == original);

        task_scheduler.wait_idle();
        completed_handles(manager.process_completions());

        EXPECT_TRUE(registry.resolve<Mesh>(handle) == original);
        EXPECT_EQ(resource_factory.mesh_creation_count(), 2);
    }

    TEST(AssetManagerTest, EnvironmentLoadsAndRefreshesWithoutEnteringMaterialTextureSlots) {
        const TemporaryProject project;
        const auto source = project.paths().assets() / "studio.hdr";
        write_hdr(source);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto handle = manager.get_database().find("studio.hdr")->handle;
        EXPECT_FALSE(manager.load_texture(handle));
        auto first = manager.load_environment(handle);
        ASSERT_TRUE(first) << first.error();
        EXPECT_TRUE(manager.ensure_loaded(handle, AssetType::Environment));
        EXPECT_EQ(factory.texture_creation_count(), 1);
        write_hdr(source, 8, 4);
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_EQ(registry.resolve<Texture>(handle).get(), first.value().get());
        scheduler.wait_idle();
        EXPECT_EQ(
            completed_handles(manager.process_completions()), std::vector<AssetHandle>{handle});
        auto second = registry.resolve<Texture>(handle);
        EXPECT_NE(second.get(), first.value().get());
        EXPECT_EQ(factory.texture_creation_count(), 2);
        std::ofstream(source) << "invalid HDR";
        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_EQ(registry.resolve<Texture>(handle).get(), second.get());
        EXPECT_FALSE(manager.load_texture(handle));
        EXPECT_FALSE(manager.reimport_texture(handle, {}));
    }

    TEST(AssetManagerTest, LoadsAndCachesTextureByAssetHandle) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> texture = loaded_asset(manager.load_texture(handle));

        ASSERT_NE(texture, nullptr);
        EXPECT_TRUE(registry.resolve<Texture>(handle) == texture);
        EXPECT_TRUE(loaded_asset(manager.load_texture(handle)) == texture);
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);
    }

    TEST(AssetManagerTest, DiscardsTextureCandidateWhenRevisionChangesDuringCreation) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const auto path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().snapshot_updated);
        const auto revision = manager.get_database().get_revision(handle);
        factory.on_next_texture_creation([&] {
            TemporaryProject::replace_texture(path);
            EXPECT_TRUE(manager.scan().snapshot_updated);
        });
        EXPECT_FALSE(manager.load_texture(handle));
        EXPECT_GT(manager.get_database().get_revision(handle), revision);
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_TRUE(loaded_asset(manager.load_texture(handle)));
    }

    enum class MaterialOperation { Load, Reload, Update };
    class MaterialPublicationTest: public ::testing::TestWithParam<MaterialOperation> {};

    TEST_P(MaterialPublicationTest, DiscardsMaterialRemovedDuringDependencyCreation) {
        const TemporaryProject project;
        constexpr AssetHandle texture(84);
        constexpr AssetHandle material(168);
        project.add_texture(texture);
        project.add_textured_material(material, texture);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().snapshot_updated);
        const auto path = project.paths().assets() / "materials/168.mat";
        const auto data = MaterialSerializer{}.load(path);
        ASSERT_TRUE(data);
        factory.on_next_texture_creation([&] {
            std::filesystem::remove(path);
            EXPECT_TRUE(manager.scan().snapshot_updated);
        });
        switch(GetParam()) {
            case MaterialOperation::Load:
                EXPECT_FALSE(manager.load_material(material));
                break;
            case MaterialOperation::Reload:
                EXPECT_FALSE(manager.reload_material(material));
                break;
            case MaterialOperation::Update:
                EXPECT_FALSE(manager.update_material(material, data.value()));
                break;
        }
        EXPECT_FALSE(registry.contains(material));
        EXPECT_EQ(manager.get_database().find(material), nullptr);
        EXPECT_FALSE(std::filesystem::exists(path));
    }

    INSTANTIATE_TEST_SUITE_P(AllEntryPoints, MaterialPublicationTest,
        ::testing::Values(
            MaterialOperation::Load, MaterialOperation::Reload, MaterialOperation::Update));

    TEST(AssetManagerTest, KeepsPreviousTextureWhenRuntimeCreationFails) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const std::filesystem::path texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = loaded_asset(manager.load_texture(handle));
        ASSERT_NE(original, nullptr);
        resource_factory.fail_texture_creation(true);
        TemporaryProject::replace_texture(texture_path);

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_TRUE(registry.resolve<Texture>(handle) == original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);

        task_scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());

        EXPECT_TRUE(registry.resolve<Texture>(handle) == original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 2);
    }

    TEST(AssetManagerTest, RefreshesModifiedTextureOnOwnerThread) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const std::filesystem::path texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = loaded_asset(manager.load_texture(handle));
        ASSERT_NE(original, nullptr);
        TemporaryProject::replace_texture(texture_path);

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_TRUE(registry.resolve<Texture>(handle) == original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);

        task_scheduler.wait_idle();
        EXPECT_EQ(
            completed_handles(manager.process_completions()), std::vector<AssetHandle>{handle});

        EXPECT_TRUE(registry.resolve<Texture>(handle) != original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 2);
    }

    TEST(AssetManagerTest, RejectsExplicitTextureReimportAfterSourceChangesDuringCreation) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const auto texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto original = loaded_asset(manager.load_texture(handle));
        ASSERT_NE(original, nullptr);
        const auto revision = manager.get_database().get_revision(handle);

        factory.on_next_texture_creation([&] {
            TemporaryProject::replace_texture(texture_path);
            EXPECT_TRUE(manager.scan().succeeded());
        });
        EXPECT_FALSE(manager.reimport_texture(handle, {.flip_y = true}));
        EXPECT_EQ(registry.resolve<Texture>(handle), original);
        EXPECT_GT(manager.get_database().get_revision(handle), revision);
        const auto* record = manager.get_database().find(handle);
        ASSERT_NE(record, nullptr);
        EXPECT_FALSE(std::get<TextureImportSettings>(record->import_settings).flip_y);

        scheduler.wait_idle();
        EXPECT_EQ(
            completed_handles(manager.process_completions()), std::vector<AssetHandle>{handle});
        EXPECT_NE(registry.resolve<Texture>(handle), original);
    }

    class TextureDependentRefreshTest: public ::testing::TestWithParam<bool> {};

    TEST_P(TextureDependentRefreshTest, RefreshesEveryLoadedMaterialFromDependencySnapshot) {
        // 同时覆盖移除最后一个反向索引条目和修改共享依赖列表。
        for(const int loaded_count : {1, 3}) {
            for(const bool include_unloaded : {false, true}) {
                SCOPED_TRACE(::testing::Message()
                             << "loaded=" << loaded_count << ", unloaded=" << include_unloaded);
                const TemporaryProject project;
                constexpr AssetHandle texture_handle(84);
                const auto texture_path = project.add_texture(texture_handle);
                std::vector<AssetHandle> handles;
                for(int i = 0; i < loaded_count; ++i) {
                    handles.emplace_back(100 + i);
                    project.add_textured_material(handles.back(), texture_handle);
                }
                constexpr AssetHandle unloaded_handle(200);
                if(include_unloaded) {
                    project.add_textured_material(unloaded_handle, texture_handle);
                }
                AssetRegistry registry;
                FakeRenderResourceFactory resource_factory;
                TaskScheduler scheduler(1);
                AssetManager manager(project.paths(), registry, resource_factory, scheduler);
                ASSERT_TRUE(manager.scan().snapshot_updated);
                std::vector<std::shared_ptr<Material>> originals;
                for(const auto handle : handles) {
                    originals.push_back(loaded_asset(manager.load_material(handle)));
                    ASSERT_NE(originals.back(), nullptr);
                }
                const auto original_texture = registry.resolve<Texture>(texture_handle);
                ASSERT_NE(original_texture, nullptr);

                if(GetParam()) {
                    ASSERT_TRUE(
                        loaded_asset(manager.reimport_texture(texture_handle, {.flip_y = true})));
                } else {
                    TemporaryProject::replace_texture(texture_path);
                    ASSERT_TRUE(manager.scan().snapshot_updated);
                    scheduler.wait_idle();
                    completed_handles(manager.process_completions());
                }

                const auto updated_texture = registry.resolve<Texture>(texture_handle);
                ASSERT_NE(updated_texture, nullptr);
                EXPECT_NE(updated_texture, original_texture);
                EXPECT_EQ(resource_factory.texture_creation_count(), 2);
                for(std::size_t i = 0; i < handles.size(); ++i) {
                    const auto material = registry.resolve<Material>(handles[i]);
                    ASSERT_NE(material, nullptr);
                    EXPECT_NE(material, originals[i]);
                    EXPECT_EQ(material->get_texture_property("u_Texture0"), updated_texture);
                    EXPECT_EQ(originals[i]->get_texture_property("u_Texture0"), original_texture);
                }
                EXPECT_EQ(registry.resolve<Material>(unloaded_handle), nullptr);
                const auto dependents = manager.get_database().get_dependents(texture_handle);
                EXPECT_EQ(dependents.size(), loaded_count + static_cast<int>(include_unloaded));
                for(const auto handle : handles) {
                    EXPECT_NE(std::ranges::find(dependents, handle), dependents.end());
                }
                if(include_unloaded) {
                    EXPECT_NE(std::ranges::find(dependents, unloaded_handle), dependents.end());
                }
            }
        }
    }

    INSTANTIATE_TEST_SUITE_P(BackgroundAndExplicit, TextureDependentRefreshTest, ::testing::Bool(),
        [](const ::testing::TestParamInfo<bool>& info) {
            return info.param ? "ExplicitReimport" : "BackgroundRefresh";
        });

    TEST(AssetManagerTest, PublishesOnlyLatestBackgroundTextureRevision) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const std::filesystem::path texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = loaded_asset(manager.load_texture(handle));
        ASSERT_NE(original, nullptr);

        std::promise<void> release_worker;
        const std::shared_future<void> worker_gate = release_worker.get_future().share();
        auto blocker = task_scheduler.try_submit([worker_gate] { worker_gate.wait(); });
        ASSERT_TRUE(blocker);

        TemporaryProject::replace_texture(texture_path);
        const AssetScanReport first_refresh = manager.scan();
        ASSERT_TRUE(contains_handle(first_refresh.modified_assets, handle));
        const AssetRevision first_revision = manager.get_database().get_revision(handle);

        TemporaryProject::replace_texture(texture_path, true);
        const AssetScanReport second_refresh = manager.scan();
        ASSERT_TRUE(contains_handle(second_refresh.modified_assets, handle));
        EXPECT_GT(manager.get_database().get_revision(handle), first_revision);
        EXPECT_TRUE(registry.resolve<Texture>(handle) == original);

        release_worker.set_value();
        task_scheduler.wait_idle();
        blocker->get();
        completed_handles(manager.process_completions());
        task_scheduler.wait_idle();
        completed_handles(manager.process_completions());

        EXPECT_TRUE(registry.resolve<Texture>(handle) != original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 2);
    }

    TEST(AssetManagerTest, KeepsPreviousTextureWhenBackgroundImportFails) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const std::filesystem::path texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> original = loaded_asset(manager.load_texture(handle));
        ASSERT_NE(original, nullptr);
        TemporaryProject::corrupt_texture(texture_path);

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        task_scheduler.wait_idle();
        completed_handles(manager.process_completions());

        EXPECT_TRUE(registry.resolve<Texture>(handle) == original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);
    }

    TEST(AssetManagerTest, ReloadsModifiedLoadedMaterialOnlyDuringCompletionProcessing) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        constexpr AssetHandle texture(84);
        project.add_texture(texture);
        const std::filesystem::path material_path =
            project.add_material(handle, "original_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        const AssetScanReport initial_scan = manager.scan();
        ASSERT_TRUE(initial_scan.snapshot_updated);
        EXPECT_TRUE(contains_handle(initial_scan.added_assets, handle));
        const std::shared_ptr<Material> original = loaded_asset(manager.load_material(handle));
        ASSERT_NE(original, nullptr);
        EXPECT_EQ(original->get_template_name(), "original_template");

        EXPECT_TRUE(
            MaterialSerializer{}.save({.template_name = "modified_template_with_different_size",
                                          .texture_properties = {{"albedo", texture}}},
                material_path));
        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.modified_assets, handle));
        EXPECT_EQ(registry.resolve<Material>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 0);
        task_scheduler.wait_idle();
        EXPECT_EQ(registry.resolve<Material>(handle), original);
        EXPECT_EQ(resource_factory.texture_creation_count(), 0);
        EXPECT_EQ(
            completed_handles(manager.process_completions()), std::vector<AssetHandle>{handle});
        const std::shared_ptr<Material> modified = registry.resolve<Material>(handle);
        ASSERT_NE(modified, nullptr);
        EXPECT_NE(modified, original);
        EXPECT_EQ(modified->get_template_name(), "modified_template_with_different_size");
        EXPECT_EQ(resource_factory.texture_creation_count(), 1);
    }

    TEST(AssetManagerTest, DiscardsMaterialRefreshChangedDuringDependencyCreation) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        constexpr AssetHandle texture(84);
        project.add_texture(texture);
        const auto path = project.add_material(handle, "original");
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto original = loaded_asset(manager.load_material(handle));
        ASSERT_TRUE(original);
        ASSERT_TRUE(MaterialSerializer{}.save(
            {.template_name = "intermediate", .texture_properties = {{"albedo", texture}}}, path));
        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        factory.on_next_texture_creation([&] {
            EXPECT_TRUE(MaterialSerializer{}.save({.template_name = "latest"}, path));
            EXPECT_TRUE(manager.scan().succeeded());
        });
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_EQ(registry.resolve<Material>(handle), original);
        scheduler.wait_idle();
        EXPECT_EQ(
            completed_handles(manager.process_completions()), std::vector<AssetHandle>{handle});
        const auto latest = registry.resolve<Material>(handle);
        ASSERT_TRUE(latest);
        EXPECT_EQ(latest->get_template_name(), "latest");
    }

    TEST(AssetManagerTest, KeepsMaterialAfterInvalidRefreshAndPublishesRepairedSource) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const auto path = project.add_material(handle, "original");
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto original = loaded_asset(manager.load_material(handle));
        ASSERT_TRUE(original);

        std::ofstream(path, std::ios::trunc) << "invalid material";
        const auto invalid_scan = manager.scan();
        ASSERT_TRUE(invalid_scan.snapshot_updated);
        EXPECT_FALSE(invalid_scan.succeeded());
        EXPECT_TRUE(contains_handle(invalid_scan.modified_assets, handle));
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_EQ(registry.resolve<Material>(handle), original);
        EXPECT_EQ(manager.get_async_status().in_flight, 0);

        ASSERT_TRUE(MaterialSerializer{}.save({.template_name = "repaired"}, path));
        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        EXPECT_EQ(
            completed_handles(manager.process_completions()), std::vector<AssetHandle>{handle});
        const auto repaired = registry.resolve<Material>(handle);
        ASSERT_TRUE(repaired);
        EXPECT_NE(repaired, original);
        EXPECT_EQ(repaired->get_template_name(), "repaired");
    }

    TEST(AssetManagerTest, UnregistersRemovedRuntimeAssetsAfterScan) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path material_path = project.add_material(handle, "test_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(loaded_asset(manager.load_material(handle)));
        ASSERT_TRUE(registry.contains(handle));
        std::filesystem::remove(material_path);
        std::filesystem::remove(metadata_path(material_path));

        const AssetScanReport refresh = manager.scan();

        ASSERT_TRUE(refresh.snapshot_updated);
        EXPECT_TRUE(contains_handle(refresh.removed_assets, handle));
        EXPECT_FALSE(registry.contains(handle));
    }

    TEST(AssetManagerTest, KeepsRuntimeAssetWhenIndexedTypeChangeIsRejected) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path texture_path = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Texture> texture = loaded_asset(manager.load_texture(handle));
        ASSERT_NE(texture, nullptr);

        const std::filesystem::path material_path = texture_path.parent_path() / "test.mat";
        std::filesystem::rename(texture_path, material_path);
        std::filesystem::remove(metadata_path(texture_path));
        EXPECT_TRUE(MaterialSerializer{}.save(
            {.template_name = "changed_type", .texture_properties = {}}, material_path));
        EXPECT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Material}, metadata_path(material_path)));

        const AssetScanReport refresh = manager.scan();

        EXPECT_FALSE(refresh.snapshot_updated);
        EXPECT_FALSE(refresh.succeeded());
        EXPECT_EQ(registry.resolve<Texture>(handle), texture);
        EXPECT_FALSE(manager.load_material(handle));
        ASSERT_NE(manager.get_database().find(handle), nullptr);
        EXPECT_EQ(manager.get_database().find(handle)->type, AssetType::Texture);
    }

    TEST(AssetManagerTest, KeepsRuntimeAssetsWhenRescanCannotCommitSnapshot) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        project.add_material(handle, "test_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Material> original = loaded_asset(manager.load_material(handle));
        ASSERT_NE(original, nullptr);
        std::filesystem::remove_all(project.paths().assets());

        const AssetScanReport failed_refresh = manager.scan();

        EXPECT_FALSE(failed_refresh.snapshot_updated);
        EXPECT_EQ(registry.resolve<Material>(handle), original);
        ASSERT_NE(manager.get_database().find(handle), nullptr);
    }

    TEST(AssetManagerTest, RejectsInvalidMaterialBeforeUpdatingFileAndRuntime) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path material_path =
            project.add_material(handle, "original_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        TaskScheduler task_scheduler(1);
        AssetManager manager(project.paths(), registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Material> original = loaded_asset(manager.load_material(handle));
        ASSERT_NE(original, nullptr);

        const MaterialData parameters{.template_name = "updated_template",
            .scalar_properties = {{"intensity", 0.75f}},
            .vector_properties = {{"color", {0.2f, 0.4f, 0.6f, 1}}}};
        const std::shared_ptr<Material> updated =
            loaded_asset(manager.update_material(handle, parameters));

        ASSERT_NE(updated, nullptr);
        EXPECT_NE(updated, original);
        EXPECT_EQ(registry.resolve<Material>(handle), updated);
        EXPECT_EQ(updated->get_scalar_property("intensity"), 0.75f);
        EXPECT_EQ(updated->get_vector_property("color"), parameters.vector_properties.at("color"));
        EXPECT_EQ(MaterialSerializer{}.load(material_path).value(), parameters);
        EXPECT_EQ(
            MaterialSerializer{}.load(material_path).value().template_name, "updated_template");

        const std::shared_ptr<Material> before_invalid_update = registry.resolve<Material>(handle);
        EXPECT_FALSE(
            manager.update_material(handle, {.template_name = "", .texture_properties = {}}));
        EXPECT_EQ(registry.resolve<Material>(handle), before_invalid_update);
        EXPECT_EQ(
            MaterialSerializer{}.load(material_path).value().template_name, "updated_template");
    }
}
