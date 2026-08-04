#include <gtest/gtest.h>

#include "asset/registry.h"
#include "render/render_scene_resolver.h"
#include "../test_utils.h"

namespace Comet::Tests {
    TEST(RenderSceneResolverTest, EmptySceneProducesEmptySubmission) {
        const AssetRegistry asset_registry;
        RenderSceneResolver resolver(asset_registry);

        const RenderSubmission submission = resolver.resolve(
            RenderScene{}, Math::Vec2u(1280, 720));

        EXPECT_FALSE(submission.view_project_matrix);
        EXPECT_TRUE(submission.render_items.empty());
    }

    TEST(RenderSceneResolverTest, SkipsItemsWithMissingResources) {
        const AssetRegistry asset_registry;
        RenderSceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.render_items.push_back({
            .entity_id = 7,
            .model_matrix = Math::Mat4(1.0f),
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(12)
        });

        const RenderSubmission submission = resolver.resolve(
            render_scene, Math::Vec2u(1280, 720));

        EXPECT_TRUE(submission.render_items.empty());
    }

    TEST(RenderSceneResolverTest, BuildsViewProjectionFromPrimaryCamera) {
        const AssetRegistry asset_registry;
        RenderSceneResolver resolver(asset_registry);
        const Math::Mat4 view_matrix = Math::translate(
            Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -3.0f));
        RenderScene render_scene;
        render_scene.cameras.push_back({
            .entity_id = 7,
            .primary = true,
            .view_matrix = view_matrix,
            .fov_degrees = 60.0f,
            .near_clip = 0.2f,
            .far_clip = 500.0f
        });

        const RenderSubmission submission = resolver.resolve(
            render_scene, Math::Vec2u(1600, 900));

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->view, view_matrix));
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->projection,
            Math::perspective(60.0f, 1600.0f / 900.0f, 0.2f, 500.0f)));
    }

    TEST(RenderSceneResolverTest, SelectsLowestEntityIdWhenMultipleCamerasArePrimary) {
        const AssetRegistry asset_registry;
        RenderSceneResolver resolver(asset_registry);
        const Math::Mat4 selected_view = Math::translate(
            Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -5.0f));
        RenderScene render_scene;
        render_scene.cameras.push_back({
            .entity_id = 9,
            .primary = true,
            .view_matrix = Math::Mat4(1.0f)
        });
        render_scene.cameras.push_back({
            .entity_id = 3,
            .primary = true,
            .view_matrix = selected_view
        });

        const RenderSubmission submission = resolver.resolve(
            render_scene, Math::Vec2u(1280, 720));

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->view, selected_view));
    }

    TEST(RenderSceneResolverTest, RejectsInvalidCameraParametersAndRenderSize) {
        const AssetRegistry asset_registry;
        RenderSceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.cameras.push_back({
            .entity_id = 7,
            .primary = true,
            .fov_degrees = 180.0f
        });

        EXPECT_FALSE(resolver.resolve(
            render_scene, Math::Vec2u(1280, 720)).view_project_matrix);

        render_scene.cameras.front().fov_degrees = 45.0f;
        render_scene.cameras.front().near_clip = 1.0f;
        render_scene.cameras.front().far_clip = 0.5f;
        EXPECT_FALSE(resolver.resolve(
            render_scene, Math::Vec2u(1280, 720)).view_project_matrix);

        render_scene.cameras.front().far_clip = 100.0f;
        EXPECT_FALSE(resolver.resolve(
            render_scene, Math::Vec2u(0, 720)).view_project_matrix);
        EXPECT_TRUE(resolver.resolve(
            render_scene, Math::Vec2u(1280, 720)).view_project_matrix);
    }
}
