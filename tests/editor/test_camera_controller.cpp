#include <gtest/gtest.h>

#include "../test_utils.h"
#include "viewport/camera_controller.h"

#include <limits>

namespace CometEditor::Tests {
    TEST(EditorCameraControllerTest, PlacementTracksCameraFocusPlaneInBothProjections) {
        EditorCameraState camera;
        camera.target = {2, 3, 4};
        camera.perspective.position = {2, 3, 9};
        camera.perspective.fov_degrees = 90;
        for(const auto projection : {Comet::RenderCamera::Projection::Perspective,
                Comet::RenderCamera::Projection::Orthographic}) {
            camera.projection = projection;
            auto center = camera_focus_plane_point(camera, {0.5f, 0.5f}, 2);
            ASSERT_TRUE(center);
            EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(*center, camera.target));
            auto corner = camera_focus_plane_point(camera, {1, 0}, 2);
            ASSERT_TRUE(corner);
            EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(*corner, {12, 8, 4}, 0.0001f));
        }
        camera.projection = Comet::RenderCamera::Projection::Perspective;
        camera.perspective.position = camera.target + Comet::Math::Vec3(5, 0, 0);
        const auto corner = camera_focus_plane_point(camera, {1, 0}, 2);
        ASSERT_TRUE(corner);
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(*corner, {2, 8, -6}, 0.0001f));
    }

    TEST(EditorCameraControllerTest, PlacementRejectsInvalidCameraOrImageCoordinates) {
        EditorCameraState camera;
        EXPECT_FALSE(camera_focus_plane_point(camera, {-0.1f, 0.5f}, 1));
        EXPECT_FALSE(camera_focus_plane_point(camera, {0.5f, 1.1f}, 1));
        EXPECT_FALSE(camera_focus_plane_point(camera, {0.5f, 0.5f}, 0));
        camera.perspective.fov_degrees = 180;
        EXPECT_FALSE(camera_focus_plane_point(camera, {0.5f, 0.5f}, 1));
        camera.perspective.fov_degrees = 45;
        camera.perspective.position = camera.target;
        EXPECT_FALSE(camera_focus_plane_point(camera, {0.5f, 0.5f}, 1));
        camera.perspective.position = {0, 0, 3};
        camera.near_clip = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(camera_focus_plane_point(camera, {0.5f, 0.5f}, 1));
    }

    TEST(EditorCameraControllerTest, FocusPerspectiveBoundsPreservesViewDirection) {
        EditorCameraState camera;
        camera.perspective.position = Comet::Math::Vec3(3.0f, 2.0f, 4.0f);
        const Comet::Math::Vec3 previous_direction =
            Comet::Math::normalize(camera.perspective.position - camera.target);
        const Comet::BoundingBox bounds{.minimum = Comet::Math::Vec3(8.0f, 1.0f, -2.0f),
            .maximum = Comet::Math::Vec3(12.0f, 3.0f, 0.0f)};

        focus_editor_camera(camera, bounds, 16.0f / 9.0f);

        EXPECT_TRUE(
            Comet::Tests::TestUtils::Vec3Equal(camera.target, bounds.center(), 0.0001f));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            Comet::Math::normalize(camera.perspective.position - camera.target),
            previous_direction, 0.0001f));
        EXPECT_GT(Comet::Math::length(camera.perspective.position - camera.target),
            Comet::Math::length(bounds.size()) * 0.5f);
    }

    TEST(EditorCameraControllerTest, FocusOrthographicBoundsFitsAspect) {
        EditorCameraState camera;
        camera.projection = Comet::RenderCamera::Projection::Orthographic;
        const Comet::Math::Vec3 previous_offset =
            camera.perspective.position - camera.target;
        const Comet::BoundingBox bounds{.minimum = Comet::Math::Vec3(-4.0f, -1.0f, -0.5f),
            .maximum = Comet::Math::Vec3(4.0f, 1.0f, 0.5f)};

        focus_editor_camera(camera, bounds, 2.0f);

        EXPECT_TRUE(
            Comet::Tests::TestUtils::Vec3Equal(camera.target, bounds.center(), 0.0001f));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.position - camera.target, previous_offset, 0.0001f));
        EXPECT_NEAR(camera.orthographic.height, 4.8f, 0.0001f);
    }

    TEST(EditorCameraControllerTest, FocusRejectsInvalidBoundsOrAspect) {
        EditorCameraState camera;
        const EditorCameraState previous = camera;
        Comet::BoundingBox invalid{
            .minimum = Comet::Math::Vec3(1.0f), .maximum = Comet::Math::Vec3(-1.0f)};

        focus_editor_camera(camera, invalid, 1.0f);
        focus_editor_camera(camera,
            {.minimum = Comet::Math::Vec3(-1.0f), .maximum = Comet::Math::Vec3(1.0f)},
            0.0f);
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.position, previous.perspective.position));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(camera.target, previous.target));
    }
    TEST(EditorCameraControllerTest, FocusNarrowAspectRequiresMoreDistance) {
        EditorCameraState camera;
        const Comet::BoundingBox bounds{
            .minimum = Comet::Math::Vec3(-1), .maximum = Comet::Math::Vec3(1)};
        focus_editor_camera(camera, bounds, 2.0f);
        const float wide_distance =
            Comet::Math::length(camera.perspective.position - camera.target);
        focus_editor_camera(camera, bounds, 0.5f);
        EXPECT_GT(Comet::Math::length(camera.perspective.position - camera.target),
            wide_distance);
    }

    TEST(EditorCameraControllerTest, FocusPointAndLargeBoundsStayWithinLimits) {
        EditorCameraState camera;
        focus_editor_camera(
            camera, Comet::BoundingBox::from_point(Comet::Math::Vec3(0)), 1.0f);
        EXPECT_NEAR(Comet::Math::length(camera.perspective.position - camera.target),
            0.05f, 0.0001f);
        const Comet::BoundingBox large{.minimum = Comet::Math::Vec3(-1.0e20f),
            .maximum = Comet::Math::Vec3(1.0e20f)};
        focus_editor_camera(camera, large, 1.0f);
        EXPECT_FLOAT_EQ(
            Comet::Math::length(camera.perspective.position - camera.target), 10000.0f);
        camera.projection = Comet::RenderCamera::Projection::Orthographic;
        focus_editor_camera(camera, large, 1.0f);
        EXPECT_FLOAT_EQ(camera.orthographic.height, 10000.0f);
    }

    TEST(EditorCameraControllerTest, EmptyInputDoesNotChangeCamera) {
        EditorCameraState camera;
        const EditorCameraState previous = camera;

        apply_editor_camera_input(camera, {});
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.position, previous.perspective.position));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(camera.target, previous.target));
    }

    TEST(EditorCameraControllerTest, OrbitPreservesTargetAndDistance) {
        EditorCameraState camera;
        const float previous_distance =
            Comet::Math::length(camera.perspective.position - camera.target);

        apply_editor_camera_input(
            camera, {.orbit_delta = Comet::Math::Vec2(100.0f, -40.0f),
                        .viewport_height = 600.0f});

        EXPECT_TRUE(
            Comet::Tests::TestUtils::Vec3Equal(camera.target, Comet::Math::Vec3(0.0f)));
        EXPECT_NEAR(Comet::Math::length(camera.perspective.position - camera.target),
            previous_distance, 0.0001f);
        EXPECT_FALSE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.position, Comet::Math::Vec3(0.0f, 0.0f, 3.0f)));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.up, Comet::Math::Vec3(0.0f, 1.0f, 0.0f)));
    }

    TEST(
        EditorCameraControllerTest, OrbitDragKeepsHorizontalDirectionAndInvertsVertical) {
        EditorCameraState horizontal;
        horizontal.target = Comet::Math::Vec3(0.0f);
        horizontal.perspective.position = {0.0f, 0.0f, 3.0f};
        EditorCameraState upward = horizontal;
        EditorCameraState downward = horizontal;

        apply_editor_camera_input(horizontal, {.orbit_delta = {20.0f, 0.0f}});
        apply_editor_camera_input(upward, {.orbit_delta = {0.0f, -20.0f}});
        apply_editor_camera_input(downward, {.orbit_delta = {0.0f, 20.0f}});

        EXPECT_LT(horizontal.perspective.position.x, 0.0f);
        EXPECT_FLOAT_EQ(horizontal.perspective.position.y, 0.0f);
        EXPECT_LT(upward.perspective.position.y, 0.0f);
        EXPECT_GT(downward.perspective.position.y, 0.0f);
        EXPECT_FLOAT_EQ(upward.perspective.position.x, 0.0f);
        EXPECT_NEAR(
            upward.perspective.position.y, -downward.perspective.position.y, 0.0001f);
    }

    TEST(EditorCameraControllerTest, PanMovesPositionAndTargetTogether) {
        EditorCameraState camera;
        const Comet::Math::Vec3 previous_position = camera.perspective.position;
        const Comet::Math::Vec3 previous_target = camera.target;

        apply_editor_camera_input(camera,
            {.pan_delta = Comet::Math::Vec2(80.0f, -25.0f), .viewport_height = 600.0f});

        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.position - previous_position,
            camera.target - previous_target));
        EXPECT_NEAR(Comet::Math::length(camera.perspective.position - camera.target),
            Comet::Math::length(previous_position - previous_target), 0.0001f);
    }

    TEST(EditorCameraControllerTest, ZoomChangesDistanceAndClampsNearTarget) {
        EditorCameraState camera;

        apply_editor_camera_input(camera, {.zoom_delta = 1.0f});
        EXPECT_LT(Comet::Math::length(camera.perspective.position - camera.target), 3.0f);

        apply_editor_camera_input(camera, {.zoom_delta = 1000.0f});
        EXPECT_NEAR(Comet::Math::length(camera.perspective.position - camera.target),
            0.05f, 0.0001f);
    }

    TEST(EditorCameraControllerTest, InvalidInputAndPanExtentAreIgnored) {
        EditorCameraState camera;
        const EditorCameraState previous = camera;

        apply_editor_camera_input(camera,
            {.pan_delta = Comet::Math::Vec2(10.0f, 5.0f), .viewport_height = 0.0f});
        apply_editor_camera_input(
            camera, {.zoom_delta = std::numeric_limits<float>::quiet_NaN()});
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.position, previous.perspective.position));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(camera.target, previous.target));

        camera.perspective.fov_degrees = std::numeric_limits<float>::quiet_NaN();
        apply_editor_camera_input(camera,
            {.pan_delta = Comet::Math::Vec2(10.0f, 5.0f), .viewport_height = 600.0f});
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.position, previous.perspective.position));
    }

    TEST(EditorCameraControllerTest, OrthographicModePansAndScalesWithoutOrbit) {
        EditorCameraState camera;
        camera.projection = Comet::RenderCamera::Projection::Orthographic;
        camera.perspective.position = Comet::Math::Vec3(3.0f, 2.0f, 4.0f);
        const Comet::Math::Vec3 previous_offset =
            camera.perspective.position - camera.target;

        apply_editor_camera_input(
            camera, {.orbit_delta = Comet::Math::Vec2(100.0f, 50.0f),
                        .pan_delta = Comet::Math::Vec2(20.0f, -10.0f),
                        .zoom_delta = 1.0f,
                        .viewport_height = 500.0f});

        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.perspective.position - camera.target, previous_offset));
        EXPECT_LT(camera.orthographic.height, 10.0f);
        EXPECT_FALSE(
            Comet::Tests::TestUtils::Vec3Equal(camera.target, Comet::Math::Vec3(0.0f)));
        EXPECT_FLOAT_EQ(camera.target.z, 0.0f);
    }
}
