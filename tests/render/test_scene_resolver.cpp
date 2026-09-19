#include <gtest/gtest.h>

#include "asset/registry.h"
#include "render/material/material.h"
#include "render/resource/render_resources.h"
#include "render/resource/texture.h"
#include "render/resource/environment.h"
#include "render/scene/scene_resolver.h"
#include "support/engine_fixture.h"
#include "support/math_assertions.h"

#include <limits>

namespace Comet::Tests {
    namespace {
        RenderView runtime_view(const Math::Vec2u size) {
            return RenderView{.render_size = size};
        }
    }

    TEST(SceneResolverTest, CameraProjectionRejectsInvalidAspectAndNonfiniteView) {
        RenderCamera camera;
        EXPECT_FALSE(camera.projection_matrix(0));
        EXPECT_FALSE(camera.projection_matrix(-1));
        camera.view_matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(camera.projection_issue(1), RenderCamera::ProjectionIssue::InvalidView);
        EXPECT_FALSE(camera.projection_matrix(1));
    }

    TEST(SceneResolverTest, EmptySceneProducesEmptySubmission) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);

        const RenderSubmission submission =
            resolver.resolve(RenderScene{}, runtime_view(Math::Vec2u(1280, 720)));

        EXPECT_FALSE(submission.view_project_matrix);
        EXPECT_TRUE(submission.render_items.empty());
    }

    TEST(SceneResolverTest, SkipsItemsWithMissingResources) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.render_items.push_back({.entity_id = 7,
            .model_matrix = Math::Mat4(1.0f),
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(12)});

        const RenderSubmission submission =
            resolver.resolve(render_scene, runtime_view(Math::Vec2u(1280, 720)));

        EXPECT_TRUE(submission.render_items.empty());
    }

    using SceneEnvironmentResolverTest = EngineTest;

    TEST_F(
        SceneEnvironmentResolverTest, UnpublishedEnvironmentResolvesAfterPublicationWithoutError) {
        AssetRegistry registry;
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.cameras.push_back({.entity_id = 1, .primary = true});
        scene.environment = {AssetHandle(17), true, 2, 90};
        const auto view = runtime_view({160, 120});
        const auto before = messages.str();
        for(int frame = 0; frame < 3; ++frame) {
            const auto submission = resolver.resolve(scene, view);
            EXPECT_FALSE(submission.environment_resource);
            EXPECT_EQ(submission.environment, scene.environment);
        }
        EXPECT_EQ(messages.str(), before);

        TextureData data{
            .width = 1, .height = 1, .pixels = std::vector<uint8_t>(24), .cubemap = true};
        auto texture = engine->get_render_resources().try_create_texture(data);
        ASSERT_TRUE(texture) << texture.error().message;
        auto environment =
            std::make_shared<Environment>(Environment{.background = texture.value()});
        ASSERT_TRUE(registry.register_asset(scene.environment.asset, environment));
        const auto published = messages.str();
        EXPECT_EQ(resolver.resolve(scene, view).environment_resource, environment);
        scene.environment.background = false;
        EXPECT_FALSE(resolver.resolve(scene, view).environment_resource);
        scene.environment.lighting = true;
        EXPECT_EQ(resolver.resolve(scene, view).environment_resource, environment);
        scene.environment.background = true;
        ASSERT_TRUE(registry.unregister_asset(scene.environment.asset));
        EXPECT_FALSE(resolver.resolve(scene, view).environment_resource);
        EXPECT_EQ(messages.str(), published);
    }

    TEST_F(SceneEnvironmentResolverTest, IncompatibleEnvironmentStillReportsOnceUntilRemoved) {
        AssetRegistry registry;
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.cameras.push_back({.entity_id = 1, .primary = true});
        scene.environment = {AssetHandle(17), true, 1, 0};
        const auto view = runtime_view({160, 120});
        TextureData data{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}};
        auto texture = engine->get_render_resources().try_create_texture(data);
        ASSERT_TRUE(texture) << texture.error().message;
        ASSERT_TRUE(registry.register_asset(scene.environment.asset, texture.value()));
        const auto before = messages.str().size();
        EXPECT_FALSE(resolver.resolve(scene, view).environment_resource);
        const auto reported = messages.str();
        EXPECT_NE(reported.find("expected environment resource", before), std::string::npos);
        EXPECT_FALSE(resolver.resolve(scene, view).environment_resource);
        EXPECT_EQ(messages.str(), reported);

        ASSERT_TRUE(registry.unregister_asset(scene.environment.asset));
        EXPECT_FALSE(resolver.resolve(scene, view).environment_resource);
        EXPECT_EQ(messages.str(), reported);
        ASSERT_TRUE(registry.register_asset(
            scene.environment.asset, std::make_shared<Material>("wrong type", "unlit_color")));
        EXPECT_FALSE(resolver.resolve(scene, view).environment_resource);
        EXPECT_NE(messages.str().find("expected environment resource", reported.size()),
            std::string::npos);
    }

    TEST(SceneResolverTest, BuildsViewProjectionFromPrimaryCamera) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        const Math::Mat4 view_matrix =
            Math::translate(Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -3.0f));
        RenderScene render_scene;
        render_scene.cameras.push_back({.entity_id = 7,
            .primary = true,
            .view_matrix = view_matrix,
            .fov_degrees = 60.0f,
            .near_clip = 0.2f,
            .far_clip = 500.0f});

        const RenderSubmission submission =
            resolver.resolve(render_scene, runtime_view(Math::Vec2u(1600, 900)));

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(submission.view_project_matrix->view, view_matrix));
        EXPECT_TRUE(TestUtils::Mat4Equal(submission.view_project_matrix->projection,
            Math::perspective(60.0f, 1600.0f / 900.0f, 0.2f, 500.0f)));
    }

    TEST(SceneResolverTest, CameraOverrideWinsOverScenePrimary) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.cameras.push_back({.entity_id = 7,
            .primary = true,
            .view_matrix = Math::Mat4(1.0f),
            .fov_degrees = 45.0f});
        const Math::Mat4 editor_view = Math::look_at(
            Math::Vec3(4.0f, 3.0f, 2.0f), Math::Vec3(0.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        const RenderSubmission submission = resolver.resolve(
            render_scene, RenderView{.render_size = Math::Vec2u(1600, 900),
                              .camera_selection = RenderView::CameraSelection::Override,
                              .camera_override = RenderCamera{.view_matrix = editor_view,
                                  .fov_degrees = 70.0f,
                                  .near_clip = 0.5f,
                                  .far_clip = 250.0f}});

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(submission.view_project_matrix->view, editor_view));
        EXPECT_TRUE(TestUtils::Mat4Equal(submission.view_project_matrix->projection,
            Math::perspective(70.0f, 1600.0f / 900.0f, 0.5f, 250.0f)));
    }

    TEST(SceneResolverTest, BuildsOrthographicProjectionFromCameraHeight) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        const Math::Mat4 editor_view = Math::look_at(
            Math::Vec3(0.0f, 0.0f, 3.0f), Math::Vec3(0.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        const RenderSubmission submission =
            resolver.resolve({}, RenderView{
                                     .render_size = Math::Vec2u(1600, 800),
                                     .camera_selection = RenderView::CameraSelection::Override,
                                     .camera_override =
                                         RenderCamera{
                                             .view_matrix = editor_view,
                                             .projection = RenderCamera::Projection::Orthographic,
                                             .orthographic_height = 10.0f,
                                             .near_clip = 0.1f,
                                             .far_clip = 100.0f,
                                         },
                                 });

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(submission.view_project_matrix->view, editor_view));
        EXPECT_TRUE(TestUtils::Mat4Equal(submission.view_project_matrix->projection,
            Math::ortho(-10.0f, 10.0f, -5.0f, 5.0f, 0.1f, 100.0f)));
    }

    TEST(SceneResolverTest, MissingCameraOverrideDoesNotFallBackToScene) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.cameras.push_back({.entity_id = 7, .primary = true});

        const RenderSubmission submission = resolver.resolve(
            render_scene, RenderView{.render_size = Math::Vec2u(1280, 720),
                              .camera_selection = RenderView::CameraSelection::Override});

        EXPECT_FALSE(submission.view_project_matrix);
    }

    TEST(SceneResolverTest, SelectsLowestEntityIdWhenMultipleCamerasArePrimary) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        const Math::Mat4 selected_view =
            Math::translate(Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -5.0f));
        RenderScene render_scene;
        render_scene.cameras.push_back(
            {.entity_id = 9, .primary = true, .view_matrix = Math::Mat4(1.0f)});
        render_scene.cameras.push_back(
            {.entity_id = 3, .primary = true, .view_matrix = selected_view});

        const RenderSubmission submission =
            resolver.resolve(render_scene, runtime_view(Math::Vec2u(1280, 720)));

        ASSERT_TRUE(submission.view_project_matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(submission.view_project_matrix->view, selected_view));
    }

    TEST(SceneResolverTest, RejectsInvalidCameraParametersAndRenderSize) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.cameras.push_back({.entity_id = 7, .primary = true, .fov_degrees = 180.0f});

        EXPECT_FALSE(resolver.resolve(render_scene, runtime_view(Math::Vec2u(1280, 720)))
                .view_project_matrix);

        render_scene.cameras.front().fov_degrees = 45.0f;
        render_scene.cameras.front().near_clip = 1.0f;
        render_scene.cameras.front().far_clip = 0.5f;
        EXPECT_FALSE(resolver.resolve(render_scene, runtime_view(Math::Vec2u(1280, 720)))
                .view_project_matrix);

        render_scene.cameras.front().far_clip = 100.0f;
        EXPECT_FALSE(
            resolver.resolve(render_scene, runtime_view(Math::Vec2u(0, 720))).view_project_matrix);
        EXPECT_TRUE(resolver.resolve(render_scene, runtime_view(Math::Vec2u(1280, 720)))
                .view_project_matrix);

        render_scene.cameras.front().projection = RenderCamera::Projection::Orthographic;
        render_scene.cameras.front().orthographic_height = 0.0f;
        EXPECT_FALSE(resolver.resolve(render_scene, runtime_view(Math::Vec2u(1280, 720)))
                .view_project_matrix);
        render_scene.cameras.front().orthographic_height = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(resolver.resolve(render_scene, runtime_view(Math::Vec2u(1280, 720)))
                .view_project_matrix);
        render_scene.cameras.front().orthographic_height = 10.0f;
        EXPECT_TRUE(resolver.resolve(render_scene, runtime_view(Math::Vec2u(1280, 720)))
                .view_project_matrix);
    }
}
