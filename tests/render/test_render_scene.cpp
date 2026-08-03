#include <gtest/gtest.h>

#include "render/render_scene.h"
#include "../test_utils.h"

namespace Comet::Tests {
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
}
