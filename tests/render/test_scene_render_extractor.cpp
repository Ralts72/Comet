#include <gtest/gtest.h>

#include "render/scene_render_extractor.h"
#include "scene/scene.h"
#include "../test_utils.h"

#include <algorithm>

namespace Comet::Tests {
    TEST(SceneRenderExtractorTest, EmptySceneProducesNoRenderItems) {
        const Scene scene;

        const RenderScene render_scene = SceneRenderExtractor::extract(scene);

        EXPECT_TRUE(render_scene.render_items.empty());
    }

    TEST(SceneRenderExtractorTest, ExtractsOnlyEntitiesWithRequiredComponents) {
        Scene scene;

        Entity first = scene.create_entity("First");
        auto& first_transform = first.get_component<TransformComponent>();
        first_transform.translation = Math::Vec3(1.0f, 2.0f, 3.0f);
        first_transform.rotation = Math::Vec3(10.0f, 20.0f, 30.0f);
        first_transform.scale = Math::Vec3(2.0f, 2.0f, 2.0f);
        first.add_component<MeshRendererComponent>(AssetHandle(10), AssetHandle(20));

        Entity second = scene.create_entity("Second");
        auto& second_transform = second.get_component<TransformComponent>();
        second_transform.translation = Math::Vec3(-4.0f, 5.0f, 6.0f);
        second_transform.rotation = Math::Vec3(0.0f, 90.0f, 0.0f);
        second.add_component<MeshRendererComponent>(AssetHandle(30), AssetHandle(40));

        scene.create_entity("No MeshRenderer");

        Entity no_transform = scene.create_entity("No Transform");
        no_transform.add_component<MeshRendererComponent>(AssetHandle(50), AssetHandle(60));
        no_transform.remove_component<TransformComponent>();

        const Math::Mat4 expected_first_model = Math::compose_trs(
            first_transform.translation, first_transform.rotation, first_transform.scale);
        const Math::Mat4 expected_second_model = Math::compose_trs(
            second_transform.translation, second_transform.rotation, second_transform.scale);
        const Scene& const_scene = scene;

        const RenderScene render_scene = SceneRenderExtractor::extract(const_scene);

        ASSERT_EQ(render_scene.render_items.size(), 2u);
        const auto find_item = [&render_scene](const EntityId id) {
            return std::find_if(
                render_scene.render_items.begin(),
                render_scene.render_items.end(),
                [id](const RenderItem& item) {
                    return item.entity_id == id;
                });
        };

        const auto first_item = find_item(first.get_id());
        ASSERT_NE(first_item, render_scene.render_items.end());
        EXPECT_EQ(first_item->mesh_handle, AssetHandle(10));
        EXPECT_EQ(first_item->material_handle, AssetHandle(20));
        EXPECT_TRUE(TestUtils::Mat4Equal(first_item->model_matrix, expected_first_model));

        const auto second_item = find_item(second.get_id());
        ASSERT_NE(second_item, render_scene.render_items.end());
        EXPECT_EQ(second_item->mesh_handle, AssetHandle(30));
        EXPECT_EQ(second_item->material_handle, AssetHandle(40));
        EXPECT_TRUE(TestUtils::Mat4Equal(second_item->model_matrix, expected_second_model));
    }

    TEST(SceneRenderExtractorTest, ExtractsCameraViewWithoutTransformScale) {
        Scene scene;
        Entity camera_entity = scene.create_entity("Main Camera");
        auto& transform = camera_entity.get_component<TransformComponent>();
        transform.translation = Math::Vec3(1.0f, 2.0f, 3.0f);
        transform.rotation = Math::Vec3(10.0f, 20.0f, 30.0f);
        transform.scale = Math::Vec3(2.0f, 3.0f, 4.0f);
        auto& camera = camera_entity.add_component<CameraComponent>();
        camera.primary = true;
        camera.fov = 60.0f;
        camera.near_clip = 0.2f;
        camera.far_clip = 500.0f;

        Entity missing_transform = scene.create_entity("Missing Transform");
        missing_transform.add_component<CameraComponent>().primary = true;
        missing_transform.remove_component<TransformComponent>();

        TransformComponent camera_pose = transform;
        camera_pose.scale = Math::Vec3(1.0f);
        const Math::Mat4 expected_view = Math::inverse(Math::compose_trs(
            camera_pose.translation, camera_pose.rotation, camera_pose.scale));

        const RenderScene render_scene = SceneRenderExtractor::extract(scene);

        ASSERT_EQ(render_scene.cameras.size(), 1u);
        const RenderCamera& extracted = render_scene.cameras.front();
        EXPECT_EQ(extracted.entity_id, camera_entity.get_id());
        EXPECT_TRUE(extracted.primary);
        EXPECT_TRUE(TestUtils::Mat4Equal(extracted.view_matrix, expected_view));
        EXPECT_FLOAT_EQ(extracted.fov_degrees, 60.0f);
        EXPECT_FLOAT_EQ(extracted.near_clip, 0.2f);
        EXPECT_FLOAT_EQ(extracted.far_clip, 500.0f);
    }
}
