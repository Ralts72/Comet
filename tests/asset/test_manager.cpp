#include "asset/manager.h"

#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "render/material.h"
#include "render/resource/resource_factory.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
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

        private:
            std::filesystem::path m_root;
        };

        class FakeRenderResourceFactory final : public RenderResourceFactory {
        public:
            std::shared_ptr<Texture> create_texture(
                const TextureData&) const override {
                return nullptr;
            }

            std::shared_ptr<Mesh> create_mesh(
                const MeshData&) const override {
                return nullptr;
            }
        };

        bool contains_handle(
            const std::vector<AssetHandle>& handles,
            const AssetHandle expected) {
            return std::ranges::find(handles, expected) != handles.end();
        }
    }

    TEST(AssetManagerTest, ReloadsModifiedLoadedMaterialAfterScan) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path material_path = project.add_material(
            handle, "original_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        AssetManager manager(project.paths(), registry, resource_factory);

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

    TEST(AssetManagerTest, UnregistersRemovedRuntimeAssetsAfterScan) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const std::filesystem::path material_path = project.add_material(
            handle, "test_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        AssetManager manager(project.paths(), registry, resource_factory);

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

    TEST(AssetManagerTest, KeepsRuntimeAssetsWhenRescanCannotCommitSnapshot) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        project.add_material(handle, "test_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        AssetManager manager(project.paths(), registry, resource_factory);

        ASSERT_TRUE(manager.scan().snapshot_updated);
        const std::shared_ptr<Material> original = manager.load_material(handle);
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
        const std::filesystem::path material_path = project.add_material(
            handle, "original_template");
        AssetRegistry registry;
        FakeRenderResourceFactory resource_factory;
        AssetManager manager(project.paths(), registry, resource_factory);

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
