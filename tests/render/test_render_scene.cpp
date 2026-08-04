#include <gtest/gtest.h>

#include "render/render_scene.h"
#include "../test_utils.h"

namespace Comet::Tests {
    TEST(RenderCameraTest, DefaultsToInvalidEntityAndIdentityView) {
        const RenderCamera camera;

        EXPECT_EQ(camera.entity_id, INVALID_ENTITY_ID);
        EXPECT_FALSE(camera.primary);
        EXPECT_TRUE(TestUtils::IsIdentityMatrix(camera.view_matrix));
        EXPECT_FLOAT_EQ(camera.fov_degrees, 45.0f);
        EXPECT_FLOAT_EQ(camera.near_clip, 0.1f);
        EXPECT_FLOAT_EQ(camera.far_clip, 1000.0f);
    }

    TEST(RenderItemTest, DefaultsToInvalidReferencesAndIdentityModel) {
        const RenderItem item;

        EXPECT_EQ(item.entity_id, INVALID_ENTITY_ID);
        EXPECT_EQ(item.mesh_handle, INVALID_ASSET_HANDLE);
        EXPECT_EQ(item.material_handle, INVALID_ASSET_HANDLE);
        EXPECT_TRUE(TestUtils::IsIdentityMatrix(item.model_matrix));
    }

    TEST(RenderSceneTest, StoresSubmissionItemsByValue) {
        const Math::Mat4 model_matrix =
            Math::translate(Math::Mat4(1.0f), Math::Vec3(1.0f, 2.0f, 3.0f));
        const RenderItem item = {
            .entity_id = 7,
            .model_matrix = model_matrix,
            .mesh_handle = AssetHandle(10),
            .material_handle = AssetHandle(20)
        };

        RenderScene render_scene;
        render_scene.render_items.push_back(item);

        ASSERT_EQ(render_scene.render_items.size(), 1u);
        const RenderItem& stored_item = render_scene.render_items.front();
        EXPECT_EQ(stored_item.entity_id, item.entity_id);
        EXPECT_EQ(stored_item.mesh_handle, item.mesh_handle);
        EXPECT_EQ(stored_item.material_handle, item.material_handle);
        EXPECT_TRUE(TestUtils::Mat4Equal(stored_item.model_matrix, model_matrix));
    }

    TEST(RenderSceneTest, StoresCameraSnapshotsByValue) {
        const Math::Mat4 view_matrix = Math::translate(
            Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -3.0f));
        const RenderCamera camera = {
            .entity_id = 9,
            .primary = true,
            .view_matrix = view_matrix,
            .fov_degrees = 60.0f,
            .near_clip = 0.2f,
            .far_clip = 500.0f
        };

        RenderScene render_scene;
        render_scene.cameras.push_back(camera);

        ASSERT_EQ(render_scene.cameras.size(), 1u);
        const RenderCamera& stored_camera = render_scene.cameras.front();
        EXPECT_EQ(stored_camera.entity_id, camera.entity_id);
        EXPECT_TRUE(stored_camera.primary);
        EXPECT_TRUE(TestUtils::Mat4Equal(stored_camera.view_matrix, view_matrix));
        EXPECT_FLOAT_EQ(stored_camera.fov_degrees, 60.0f);
        EXPECT_FLOAT_EQ(stored_camera.near_clip, 0.2f);
        EXPECT_FLOAT_EQ(stored_camera.far_clip, 500.0f);
    }
}
