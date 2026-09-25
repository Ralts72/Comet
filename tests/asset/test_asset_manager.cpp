#include "support/asset_manager_fixture.h"
#include "asset/source_operations.h"

namespace Comet::Tests {
    namespace {
        AssetScanReport move_source(AssetDatabase& database, AssetManager& manager,
            const ProjectPaths& paths, AssetHandle handle,
            const std::filesystem::path& destination) {
            auto report = AssetSourceOperations::move(database, paths, handle, destination);
            manager.accept_scan_report(report);
            return report;
        }
    }

    class MeshAsyncImportTest: public ::testing::Test {
    protected:
        using Mode = MeshImportMode;
        static constexpr AssetHandle handle{42};
        TemporaryProject project;
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler{1};
        AssetDatabase database{project.paths()};
        AssetManager manager{database, registry, factory, scheduler};
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
            const auto result = manager.prepare_material_update(material_handle, data);
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
        const auto report =
            move_source(database, manager, project.paths(), handle, "moved/new.gltf");
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
        AssetDatabase database(project.paths());
        AssetManager manager(database, registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().succeeded());
        const std::shared_ptr<Material> original = loaded_asset(manager.load_material(handle));
        ASSERT_NE(original, nullptr);

        const AssetScanReport report =
            move_source(database, manager, project.paths(), handle, "renamed/moved.mat");

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
        AssetDatabase database(project.paths());
        AssetManager manager(database, registry, resource_factory, task_scheduler);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        ASSERT_TRUE(manager.import_mesh(handle));
        const std::shared_ptr<Mesh> original = loaded_asset(manager.load_mesh(handle));
        ASSERT_NE(original, nullptr);

        const AssetScanReport report =
            move_source(database, manager, project.paths(), handle, "renamed/moved.gltf");

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
        AssetDatabase database(project.paths());
        AssetManager manager(database, registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().snapshot_updated);

        const AssetScanReport report =
            move_source(database, manager, project.paths(), handle, "occupied.mat");

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
        AssetDatabase database(project.paths());
        AssetManager manager(database, registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().snapshot_updated);

        const AssetScanReport report =
            move_source(database, manager, project.paths(), handle, "../outside.mat");

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
        AssetDatabase database(project.paths());
        AssetManager manager(database, registry, resource_factory, task_scheduler);
        ASSERT_TRUE(manager.scan().succeeded());

        const std::filesystem::path duplicate = project.paths().assets() / "duplicate.mat";
        EXPECT_TRUE(MaterialSerializer{}.save(
            {.template_name = "duplicate", .texture_properties = {}}, duplicate));
        EXPECT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Material}, metadata_path(duplicate)));

        const AssetScanReport report =
            move_source(database, manager, project.paths(), handle, "renamed/moved.mat");

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
        const auto retried =
            move_source(database, manager, project.paths(), handle, "renamed/moved.mat");
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

    TEST(AssetManagerTest, MissingOptionalReferencesDoNotPreventRuntimeStartup) {
        const TemporaryProject project;
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const std::array optional{AssetReference{AssetHandle(17), AssetType::Environment, false}};
        auto prepared =
            manager.prepare_references(optional, AssetManager::MissingAssetPolicy::FailRequired);
        ASSERT_TRUE(prepared);
        EXPECT_EQ(prepared.value(), 1u);
        const std::array required{AssetReference{AssetHandle(18), AssetType::Mesh}};
        EXPECT_FALSE(
            manager.prepare_references(required, AssetManager::MissingAssetPolicy::FailRequired));
        EXPECT_TRUE(
            manager.prepare_references(required, AssetManager::MissingAssetPolicy::AllowMissing));
        EXPECT_EQ(factory.texture_creation_count(), 0);
    }

    TEST(AssetManagerTest, EnvironmentFirstLoadKeepsItsByteReservationUntilOwnerPublication) {
        const TemporaryProject project;
        const auto source = project.paths().assets() / "one.hdr";
        write_hdr(source);
        write_hdr(project.paths().assets() / "two.hdr");
        auto bytes = EnvironmentImporter::working_bytes(source);
        ASSERT_TRUE(bytes);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler,
            {.async = {.in_flight = 4, .queued = 4, .working_bytes = bytes.value()}});
        BlockedWorker blocked(scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto first = manager.get_database().find("one.hdr")->handle;
        const auto second = manager.get_database().find("two.hdr")->handle;
        ASSERT_TRUE(manager.request_load(first, AssetType::Environment));
        ASSERT_TRUE(manager.request_load(second, AssetType::Environment));
        EXPECT_EQ(factory.texture_creation_count(), 0);
        EXPECT_EQ(manager.get_async_status().in_flight, 1u);
        EXPECT_EQ(manager.get_async_status().queued, 1u);
        EXPECT_EQ(manager.get_async_status().reserved_bytes, bytes.value());
        blocked.release();
        scheduler.wait_idle();
        EXPECT_FALSE(registry.contains(first));
        EXPECT_EQ(manager.get_async_status().reserved_bytes, bytes.value());
        EXPECT_EQ(completed_handles(manager.process_completions()), std::vector{first});
        scheduler.wait_idle();
        EXPECT_EQ(completed_handles(manager.process_completions()), std::vector{second});
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
        EXPECT_EQ(factory.texture_creation_count(), 8);
    }

    TEST(AssetManagerTest, EnvironmentRejectsOversizedDemandAndStaleFirstLoad) {
        const TemporaryProject project;
        const auto source = project.paths().assets() / "studio.hdr";
        write_hdr(source);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager constrained(
            project.paths(), registry, factory, scheduler, {.async = {.working_bytes = 1}});
        ASSERT_TRUE(constrained.scan().succeeded());
        const auto handle = constrained.get_database().find("studio.hdr")->handle;
        EXPECT_FALSE(constrained.request_load(handle, AssetType::Environment));
        EXPECT_EQ(constrained.get_async_status().in_flight, 0u);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.request_load(handle, AssetType::Environment));
        scheduler.wait_idle();
        std::filesystem::remove(source);
        ASSERT_TRUE(manager.scan().snapshot_updated);
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_EQ(factory.texture_creation_count(), 0);
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
    }

    TEST(AssetManagerTest, GrowingEnvironmentCannotExceedItsQueuedReservation) {
        const TemporaryProject project;
        const auto source = project.paths().assets() / "studio.hdr";
        write_hdr(source);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        BlockedWorker blocked(scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto handle = manager.get_database().find("studio.hdr")->handle;
        ASSERT_TRUE(manager.request_load(handle, AssetType::Environment));
        write_hdr(source, 32, 16);
        blocked.release();
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_EQ(factory.texture_creation_count(), 0);
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
        EXPECT_FALSE(manager.request_load(handle, AssetType::Environment));
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.request_load(handle, AssetType::Environment));
        scheduler.wait_idle();
        EXPECT_EQ(completed_handles(manager.process_completions()), std::vector{handle});
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
        EXPECT_EQ(factory.texture_creation_count(), 4);
        write_hdr(source, 8, 4);
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_EQ(registry.resolve<Environment>(handle).get(), first.value().get());
        scheduler.wait_idle();
        EXPECT_EQ(
            completed_handles(manager.process_completions()), std::vector<AssetHandle>{handle});
        auto second = registry.resolve<Environment>(handle);
        EXPECT_NE(second.get(), first.value().get());
        EXPECT_EQ(factory.texture_creation_count(), 8);
        std::ofstream(source) << "invalid HDR";
        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_EQ(registry.resolve<Environment>(handle).get(), second.get());
        EXPECT_FALSE(manager.load_texture(handle));
        EXPECT_FALSE(manager.reimport_texture(handle, {}));
    }

    TEST(AssetManagerTest, FailedIblUploadRetainsTheCompletePreviousGeneration) {
        const TemporaryProject project;
        const auto source = project.paths().assets() / "studio.hdr";
        write_hdr(source);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto handle = manager.get_database().find("studio.hdr")->handle;
        auto original = manager.load_environment(handle);
        ASSERT_TRUE(original);
        write_hdr(source, 8, 4);
        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        // 背景上传成功，但随后的漫反射环境贴图上传失败。
        factory.on_next_texture_creation([&] {
            factory.on_next_texture_creation([&] { factory.fail_texture_creation(true); });
        });
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_EQ(registry.resolve<Environment>(handle), original.value());
        EXPECT_EQ(factory.texture_creation_count(), 6);
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
        factory.fail_texture_creation(false);
        write_hdr(source, 16, 8);
        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        EXPECT_EQ(completed_handles(manager.process_completions()), std::vector{handle});
        EXPECT_NE(registry.resolve<Environment>(handle), original.value());
    }

}
