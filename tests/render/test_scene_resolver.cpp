#include <gtest/gtest.h>

#include "asset/registry.h"
#include "render/scene/scene_resolver.h"
#include "../test_utils.h"

namespace Comet::Tests {
    namespace {
        ViewportRenderRequest runtime_view(const Math::Vec2u size) {
            return ViewportRenderRequest{.render_size = size};
        }
    }

    TEST(SceneResolverTest, EmptySceneProducesEmptySubmission) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);

        const RenderSubmission submission = resolver.resolve(
            RenderScene{}, runtime_view(Math::Vec2u(1280, 720)));

        EXPECT_FALSE(submission.view_project_matrix);
        EXPECT_TRUE(submission.render_items.empty());
    }

    TEST(SceneResolverTest, SkipsItemsWithMissingResources) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.render_items.push_back({
            .entity_id = 7,
            .model_matrix = Math::Mat4(1.0f),
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(12)
        });

        const RenderSubmission submission = resolver.resolve(
            render_scene, runtime_view(Math::Vec2u(1280, 720)));

        EXPECT_TRUE(submission.render_items.empty());
    }

    TEST(SceneResolverTest, BuildsViewProjectionFromPrimaryCamera) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
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
            render_scene, runtime_view(Math::Vec2u(1600, 900)));

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->view, view_matrix));
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->projection,
            Math::perspective(60.0f, 1600.0f / 900.0f, 0.2f, 500.0f)));
    }

    TEST(SceneResolverTest, ExplicitViewportCameraOverridesScenePrimary) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.cameras.push_back({
            .entity_id = 7,
            .primary = true,
            .view_matrix = Math::Mat4(1.0f),
            .fov_degrees = 45.0f
        });
        const Math::Mat4 editor_view = Math::look_at(
            Math::Vec3(4.0f, 3.0f, 2.0f),
            Math::Vec3(0.0f),
            Math::Vec3(0.0f, 1.0f, 0.0f));

        const RenderSubmission submission = resolver.resolve(
            render_scene,
            ViewportRenderRequest{
                .render_size = Math::Vec2u(1600, 900),
                .camera_source = ViewportCameraSource::Explicit,
                .explicit_camera = RenderCamera{
                    .view_matrix = editor_view,
                    .fov_degrees = 70.0f,
                    .near_clip = 0.5f,
                    .far_clip = 250.0f
                }
            });

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->view, editor_view));
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->projection,
            Math::perspective(70.0f, 1600.0f / 900.0f, 0.5f, 250.0f)));
    }

    TEST(SceneResolverTest, BuildsOrthographicProjectionFromCameraHeight) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        const Math::Mat4 editor_view = Math::look_at(
            Math::Vec3(0.0f, 0.0f, 3.0f),
            Math::Vec3(0.0f),
            Math::Vec3(0.0f, 1.0f, 0.0f));

        const RenderSubmission submission = resolver.resolve(
            {},
            ViewportRenderRequest{
                .render_size = Math::Vec2u(1600, 800),
                .camera_source = ViewportCameraSource::Explicit,
                .explicit_camera = RenderCamera{
                    .view_matrix = editor_view,
                    .projection = CameraProjection::Orthographic,
                    .orthographic_height = 10.0f,
                    .near_clip = 0.1f,
                    .far_clip = 100.0f
                }
            });

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->view, editor_view));
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->projection,
            Math::ortho(-10.0f, 10.0f, -5.0f, 5.0f, 0.1f, 100.0f)));
    }

    TEST(SceneResolverTest, MissingExplicitCameraDoesNotFallBackToScene) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.cameras.push_back({
            .entity_id = 7,
            .primary = true
        });

        const RenderSubmission submission = resolver.resolve(
            render_scene,
            ViewportRenderRequest{
                .render_size = Math::Vec2u(1280, 720),
                .camera_source = ViewportCameraSource::Explicit
            });

        EXPECT_FALSE(submission.view_project_matrix);
    }

    TEST(SceneResolverTest, SelectsLowestEntityIdWhenMultipleCamerasArePrimary) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
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
            render_scene, runtime_view(Math::Vec2u(1280, 720)));

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(
            submission.view_project_matrix->view, selected_view));
    }

    TEST(SceneResolverTest, RejectsInvalidCameraParametersAndRenderSize) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.cameras.push_back({
            .entity_id = 7,
            .primary = true,
            .fov_degrees = 180.0f
        });

        EXPECT_FALSE(resolver.resolve(
            render_scene,
            runtime_view(Math::Vec2u(1280, 720))).view_project_matrix);

        render_scene.cameras.front().fov_degrees = 45.0f;
        render_scene.cameras.front().near_clip = 1.0f;
        render_scene.cameras.front().far_clip = 0.5f;
        EXPECT_FALSE(resolver.resolve(
            render_scene,
            runtime_view(Math::Vec2u(1280, 720))).view_project_matrix);

        render_scene.cameras.front().far_clip = 100.0f;
        EXPECT_FALSE(resolver.resolve(
            render_scene,
            runtime_view(Math::Vec2u(0, 720))).view_project_matrix);
        EXPECT_TRUE(resolver.resolve(
            render_scene,
            runtime_view(Math::Vec2u(1280, 720))).view_project_matrix);

        render_scene.cameras.front().projection =
            CameraProjection::Orthographic;
        render_scene.cameras.front().orthographic_height = 0.0f;
        EXPECT_FALSE(resolver.resolve(
            render_scene,
            runtime_view(Math::Vec2u(1280, 720))).view_project_matrix);
        render_scene.cameras.front().orthographic_height = 10.0f;
        EXPECT_TRUE(resolver.resolve(
            render_scene,
            runtime_view(Math::Vec2u(1280, 720))).view_project_matrix);
    }
}
