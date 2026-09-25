#include "support/asset_manager_fixture.h"
#include "asset/import/texture_importer.h"
#include "asset/import/mesh_importer.h"

namespace Comet::Tests {
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

    enum class MaterialOperation { Load, Reload, PrepareUpdate };
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
            case MaterialOperation::PrepareUpdate:
                EXPECT_FALSE(manager.prepare_material_update(material, data.value()));
                break;
        }
        EXPECT_FALSE(registry.contains(material));
        EXPECT_EQ(manager.get_database().find(material), nullptr);
        EXPECT_FALSE(std::filesystem::exists(path));
    }

    INSTANTIATE_TEST_SUITE_P(AllEntryPoints, MaterialPublicationTest,
        ::testing::Values(
            MaterialOperation::Load, MaterialOperation::Reload, MaterialOperation::PrepareUpdate));

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

    TEST(AssetManagerTest, TextureRefreshRetainsItsByteReservationUntilPublication) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const auto source = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto previous = loaded_asset(manager.load_texture(handle));
        ASSERT_TRUE(previous);
        TemporaryProject::replace_texture(source);
        const auto bytes = TextureImporter::working_bytes(source);
        ASSERT_TRUE(bytes);

        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_EQ(manager.get_async_status().reserved_bytes, bytes.value());
        scheduler.wait_idle();
        EXPECT_EQ(manager.get_async_status().reserved_bytes, bytes.value());
        EXPECT_TRUE(registry.resolve<Texture>(handle) == previous);
        EXPECT_EQ(completed_handles(manager.process_completions()), std::vector{handle});
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
        EXPECT_TRUE(registry.resolve<Texture>(handle) != previous);
    }

    TEST(AssetManagerTest, MeshImportRetainsItsByteReservationUntilPublication) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const auto source = project.add_mesh(handle);
        const auto bytes = MeshImporter::working_bytes(source);
        ASSERT_TRUE(bytes);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());

        ASSERT_TRUE(manager.import_mesh_async(handle));
        EXPECT_EQ(manager.get_async_status().reserved_bytes, bytes.value());
        scheduler.wait_idle();
        EXPECT_EQ(manager.get_async_status().reserved_bytes, bytes.value());
        EXPECT_EQ(completed_handles(manager.process_completions()), std::vector{handle});
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
    }

    TEST(AssetManagerTest, MeshRefreshOverBudgetKeepsPreviousRuntimeVersion) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const auto source = project.add_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(
            project.paths(), registry, factory, scheduler, {.async = {.working_bytes = 1}});
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh(handle));
        const auto previous = loaded_asset(manager.load_mesh(handle));
        ASSERT_TRUE(previous);
        TemporaryProject::write_mesh(source,
            R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})");

        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == previous);
        EXPECT_EQ(factory.mesh_creation_count(), 1);
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
    }

    TEST(AssetManagerTest, GrowingMeshInputCannotExceedItsQueuedReservation) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const auto buffer = project.add_external_mesh(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(project.paths(), registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_mesh(handle));
        const auto previous = loaded_asset(manager.load_mesh(handle));
        ASSERT_TRUE(previous);
        const auto source = project.paths().assets() / "meshes/external.gltf";
        const auto reserved = MeshImporter::working_bytes(source);
        ASSERT_TRUE(reserved);

        BlockedWorker blocker(scheduler);
        ASSERT_TRUE(manager.import_mesh_async(handle, MeshImportMode::Force));
        EXPECT_EQ(manager.get_async_status().reserved_bytes, reserved.value());
        std::filesystem::resize_file(buffer, 1024 * 1024);
        blocker.release();
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_TRUE(registry.resolve<Mesh>(handle) == previous);
        EXPECT_EQ(factory.mesh_creation_count(), 1);
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
    }

    TEST(AssetManagerTest, TextureRefreshOverBudgetKeepsPreviousRuntimeVersion) {
        const TemporaryProject project;
        constexpr AssetHandle handle(84);
        const auto source = project.add_texture(handle);
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(
            project.paths(), registry, factory, scheduler, {.async = {.working_bytes = 1}});
        ASSERT_TRUE(manager.scan().succeeded());
        const auto previous = loaded_asset(manager.load_texture(handle));
        ASSERT_TRUE(previous);
        TemporaryProject::replace_texture(source);

        ASSERT_TRUE(manager.scan().succeeded());
        scheduler.wait_idle();
        EXPECT_TRUE(completed_handles(manager.process_completions()).empty());
        EXPECT_TRUE(registry.resolve<Texture>(handle) == previous);
        EXPECT_EQ(factory.texture_creation_count(), 1);
        EXPECT_EQ(manager.get_async_status().reserved_bytes, 0u);
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
        const auto prepared = manager.prepare_material_update(handle, parameters);
        ASSERT_TRUE(prepared);
        const std::shared_ptr<Material> updated =
            loaded_asset(manager.commit_material_update(prepared.value()));

        ASSERT_NE(updated, nullptr);
        EXPECT_NE(updated, original);
        EXPECT_EQ(registry.resolve<Material>(handle), updated);
        EXPECT_EQ(updated->get_scalar_property("intensity"), 0.75f);
        EXPECT_EQ(updated->get_vector_property("color"), parameters.vector_properties.at("color"));
        EXPECT_EQ(MaterialSerializer{}.load(material_path).value(), parameters);
        EXPECT_EQ(
            MaterialSerializer{}.load(material_path).value().template_name, "updated_template");

        const std::shared_ptr<Material> before_invalid_update = registry.resolve<Material>(handle);
        EXPECT_FALSE(manager.prepare_material_update(
            handle, {.template_name = "", .texture_properties = {}}));
        EXPECT_EQ(registry.resolve<Material>(handle), before_invalid_update);
        EXPECT_EQ(
            MaterialSerializer{}.load(material_path).value().template_name, "updated_template");
    }

    TEST(ScriptAssetTest, MetadataAndAssetManagerLoadLuaWithoutProjectCompilation) {
        const AssetHandle handle{42};
        AssetRegistry assets;
        TemporaryDirectory directory;
        ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        ASSERT_TRUE(write_text_file_atomic(
            paths.assets() / "spin.lua", "return {properties = {speed = 2}}"));
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Script}, paths.assets() / "spin.lua.meta"));
        TaskScheduler scheduler(1);
        FakeRenderResourceFactory factory;
        AssetManager manager(paths, assets, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        auto loaded = manager.load_script(handle);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_EQ(std::get<float>(loaded.value()->defaults().at("speed")), 2);
        EXPECT_EQ(manager.load_script(handle).value(), loaded.value());
        ASSERT_TRUE(write_text_file_atomic(
            paths.assets() / "spin.lua", "return {properties = {speed = 200}}"));
        ASSERT_TRUE(manager.scan().succeeded());
        auto updated = manager.load_script(handle);
        ASSERT_TRUE(updated);
        EXPECT_NE(updated.value(), loaded.value());
        EXPECT_EQ(std::get<float>(updated.value()->defaults().at("speed")), 200);
    }

    TEST(AudioAssetTest, ScansAndLoadsWavByHandle) {
        constexpr AssetHandle handle{42};
        AssetRegistry assets;
        TemporaryDirectory directory;
        ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto destination = paths.assets() / "cue.wav";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/audio/play_chime.wav",
            destination);
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Audio}, metadata_path(destination)));
        TaskScheduler scheduler(1);
        FakeRenderResourceFactory factory;
        AssetManager manager(paths, assets, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        auto loaded = manager.load_audio(handle);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_EQ(loaded.value(), assets.resolve<AudioClip>(handle));
        EXPECT_GT(loaded.value()->frame_count(), 0u);
    }

}
