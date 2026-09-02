#include <gtest/gtest.h>

#include "render/scene/render_scene.h"
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
}
