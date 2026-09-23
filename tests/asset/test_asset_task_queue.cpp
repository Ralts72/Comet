#include "support/asset_manager_fixture.h"

namespace Comet::Tests {
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

}
