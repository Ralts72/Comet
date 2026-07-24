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
        EXPECT_EQ(registry.size(), 1u);

        const auto resolved_mesh = registry.resolve<TestMesh>(handle);
        ASSERT_NE(resolved_mesh, nullptr);
        EXPECT_EQ(resolved_mesh, mesh);
        EXPECT_EQ(resolved_mesh->vertex_count, 24);
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
    }
}
