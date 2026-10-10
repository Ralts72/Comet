#include "asset/registry.h"

#include <gtest/gtest.h>

#include <memory>

namespace Comet::Tests {
    namespace {
        struct TestMesh {
            int vertex_count = 0;
        };

        struct TestMaterial {
            int property_count = 0;
        };
    }

    TEST(AssetRegistryTest, RegistersAndResolvesAssetByHandleAndType) {
        AssetRegistry registry;
        const AssetHandle handle(1);
        const auto mesh = std::make_shared<TestMesh>(TestMesh{24});

        EXPECT_TRUE(registry.register_asset(handle, mesh));
        EXPECT_TRUE(registry.contains(handle));
        EXPECT_TRUE(registry.contains<TestMesh>(handle));
        EXPECT_TRUE(registry.contains<const TestMesh>(handle));
        EXPECT_FALSE(registry.contains<TestMaterial>(handle));
        EXPECT_EQ(registry.size(), 1u);

        const auto resolved_mesh = registry.resolve<TestMesh>(handle);
        ASSERT_NE(resolved_mesh, nullptr);
        EXPECT_EQ(resolved_mesh, mesh);
        EXPECT_EQ(resolved_mesh->vertex_count, 24);
        EXPECT_EQ(registry.resolve<const TestMesh>(handle), mesh);
    }

    TEST(AssetRegistryTest, RejectsInvalidNullAndDuplicateRegistrations) {
        AssetRegistry registry;
        const AssetHandle handle(2);
        const auto mesh = std::make_shared<TestMesh>();
        const auto material = std::make_shared<TestMaterial>();

        EXPECT_FALSE(registry.register_asset(INVALID_ASSET_HANDLE, mesh));
        EXPECT_FALSE(registry.register_asset(handle, std::shared_ptr<TestMesh>{}));
        ASSERT_TRUE(registry.register_asset(handle, mesh));
        EXPECT_FALSE(registry.register_asset(handle, material));

        EXPECT_EQ(registry.size(), 1u);
        EXPECT_EQ(registry.resolve<TestMesh>(handle), mesh);
        EXPECT_EQ(registry.resolve<TestMaterial>(handle), nullptr);
    }

    TEST(AssetRegistryTest, MissingAndInvalidHandlesDoNotResolve) {
        const AssetRegistry registry;

        EXPECT_FALSE(registry.contains(INVALID_ASSET_HANDLE));
        EXPECT_FALSE(registry.contains<TestMesh>(INVALID_ASSET_HANDLE));
        EXPECT_FALSE(registry.contains<TestMesh>(AssetHandle(99)));
        EXPECT_EQ(registry.resolve<TestMesh>(INVALID_ASSET_HANDLE), nullptr);
        EXPECT_EQ(registry.resolve<TestMesh>(AssetHandle(99)), nullptr);
    }

    TEST(AssetRegistryTest, OwnsRegisteredAssetUntilItIsUnregistered) {
        AssetRegistry registry;
        const AssetHandle handle(3);
        auto mesh = std::make_shared<TestMesh>();
        const std::weak_ptr<TestMesh> weak_mesh = mesh;

        ASSERT_TRUE(registry.register_asset(handle, std::move(mesh)));
        ASSERT_EQ(mesh, nullptr);
        EXPECT_FALSE(weak_mesh.expired());

        EXPECT_TRUE(registry.unregister_asset(handle));
        EXPECT_TRUE(weak_mesh.expired());
        EXPECT_FALSE(registry.contains(handle));
        EXPECT_FALSE(registry.contains<TestMesh>(handle));
        EXPECT_FALSE(registry.unregister_asset(handle));
    }

    TEST(AssetRegistryTest, ClearReleasesAllRegisteredAssets) {
        AssetRegistry registry;
        const auto mesh = std::make_shared<TestMesh>();
        const auto material = std::make_shared<TestMaterial>();

        ASSERT_TRUE(registry.register_asset(AssetHandle(4), mesh));
        ASSERT_TRUE(registry.register_asset(AssetHandle(5), material));
        ASSERT_EQ(registry.size(), 2u);

        registry.clear();

        EXPECT_EQ(registry.size(), 0u);
        EXPECT_FALSE(registry.contains(AssetHandle(4)));
        EXPECT_FALSE(registry.contains(AssetHandle(5)));
        EXPECT_FALSE(registry.contains<TestMesh>(AssetHandle(4)));
        EXPECT_FALSE(registry.contains<TestMaterial>(AssetHandle(5)));
    }

    TEST(AssetRegistryTest, ReplacesExistingAssetWithoutChangingItsType) {
        AssetRegistry registry;
        const AssetHandle handle(42);
        const auto original = std::make_shared<TestMesh>(TestMesh{1});
        const auto replacement = std::make_shared<TestMesh>(TestMesh{2});
        ASSERT_TRUE(registry.register_asset(handle, original));

        EXPECT_TRUE(registry.replace_asset(handle, replacement));
        EXPECT_EQ(registry.resolve<TestMesh>(handle), replacement);
        EXPECT_TRUE(registry.contains<TestMesh>(handle));
        EXPECT_EQ(original->vertex_count, 1);
        EXPECT_FALSE(registry.replace_asset(AssetHandle(73), std::make_shared<TestMesh>()));
        EXPECT_FALSE(registry.replace_asset(handle, std::make_shared<TestMaterial>()));
    }

    TEST(AssetRegistryTest, PublicationRevisionInvalidatesResolvedSnapshotsOnlyOnSuccess) {
        AssetRegistry registry;
        AssetRegistry other;
        EXPECT_NE(registry.get_revision(), other.get_revision());
        const auto initial = registry.get_revision();
        const AssetHandle handle{42};
        const auto mesh = std::make_shared<TestMesh>();
        EXPECT_EQ(registry.get_revision(handle), 0u);
        EXPECT_EQ(registry.get_revision(INVALID_ASSET_HANDLE), 0u);
        EXPECT_FALSE(registry.register_asset(INVALID_ASSET_HANDLE, mesh));
        EXPECT_FALSE(registry.unregister_asset(handle));
        EXPECT_EQ(registry.get_revision(), initial);

        ASSERT_TRUE(registry.register_asset(handle, mesh));
        const auto published = registry.get_revision();
        EXPECT_EQ(registry.get_revision(handle), published);
        EXPECT_NE(published, initial);
        EXPECT_FALSE(registry.register_asset(handle, mesh));
        EXPECT_FALSE(registry.replace_asset(handle, std::make_shared<TestMaterial>()));
        EXPECT_EQ(registry.get_revision(), published);
        EXPECT_EQ(registry.get_revision(handle), published);
        ASSERT_TRUE(registry.replace_asset(handle, std::make_shared<TestMesh>()));
        const auto replaced = registry.get_revision();
        EXPECT_EQ(registry.get_revision(handle), replaced);
        EXPECT_NE(replaced, published);
        ASSERT_TRUE(registry.unregister_asset(handle));
        EXPECT_EQ(registry.get_revision(handle), 0u);
        EXPECT_NE(registry.get_revision(), replaced);
        const auto removed = registry.get_revision();
        ASSERT_TRUE(registry.register_asset(handle, mesh));
        EXPECT_NE(registry.get_revision(), removed);
        const auto restored = registry.get_revision();
        EXPECT_NE(registry.get_revision(handle), replaced);
        registry.clear();
        EXPECT_EQ(registry.get_revision(handle), 0u);
        EXPECT_NE(registry.get_revision(), restored);
    }

    TEST(AssetRegistryTest, PublishingOtherAssetsPreservesResourceRevision) {
        AssetRegistry registry;
        const AssetHandle mesh{10}, material{20};
        ASSERT_TRUE(registry.register_asset(mesh, std::make_shared<TestMesh>()));
        const auto mesh_revision = registry.get_revision(mesh);
        ASSERT_TRUE(registry.register_asset(material, std::make_shared<TestMaterial>()));
        EXPECT_EQ(registry.get_revision(mesh), mesh_revision);
        const auto material_revision = registry.get_revision(material);
        EXPECT_NE(material_revision, mesh_revision);
        ASSERT_TRUE(registry.replace_asset(material, std::make_shared<TestMaterial>()));
        EXPECT_NE(registry.get_revision(material), material_revision);
        EXPECT_EQ(registry.get_revision(mesh), mesh_revision);
        ASSERT_TRUE(registry.unregister_asset(material));
        EXPECT_EQ(registry.get_revision(mesh), mesh_revision);
        AssetRegistry other;
        ASSERT_TRUE(other.register_asset(mesh, std::make_shared<TestMesh>()));
        EXPECT_NE(other.get_revision(mesh), mesh_revision);
    }

    TEST(AssetRegistryTest, MovingRegistriesInvalidatesBothPublishedStates) {
        AssetRegistry source;
        const AssetHandle handle{1};
        const auto mesh = std::make_shared<TestMesh>();
        ASSERT_TRUE(source.register_asset(handle, mesh));
        const auto before = source.get_revision();
        const auto resource_revision = source.get_revision(handle);
        AssetRegistry target(std::move(source));
        EXPECT_EQ(target.resolve<TestMesh>(handle), mesh);
        EXPECT_EQ(source.size(), 0u);
        EXPECT_EQ(source.get_revision(handle), 0u);
        EXPECT_EQ(target.get_revision(handle), resource_revision);
        EXPECT_NE(source.get_revision(), before);
        EXPECT_NE(target.get_revision(), before);
        const auto moved = target.get_revision();
        const auto cleared = source.get_revision();
        source = std::move(target);
        EXPECT_EQ(source.resolve<TestMesh>(handle), mesh);
        EXPECT_EQ(target.size(), 0u);
        EXPECT_EQ(target.get_revision(handle), 0u);
        EXPECT_EQ(source.get_revision(handle), resource_revision);
        EXPECT_NE(source.get_revision(), cleared);
        EXPECT_NE(target.get_revision(), moved);
    }
}
