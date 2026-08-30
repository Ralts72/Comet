#include <gtest/gtest.h>

#include "editor_camera_controller.h"
#include "../test_utils.h"

#include <limits>

namespace CometEditor::Tests {
    TEST(EditorCameraControllerTest, EmptyInputDoesNotChangeCamera) {
        EditorCameraState camera;
        const EditorCameraState previous = camera;

        EXPECT_FALSE(apply_editor_camera_input(camera, {}));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.position, previous.position));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.target, previous.target));
    }

    TEST(EditorCameraControllerTest, OrbitPreservesTargetAndDistance) {
        EditorCameraState camera;
        const float previous_distance = Comet::Math::length(
            camera.position - camera.target);

        EXPECT_TRUE(apply_editor_camera_input(camera, {
            .orbit_delta = Comet::Math::Vec2(100.0f, -40.0f),
            .viewport_height = 600.0f
        }));

        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.target, Comet::Math::Vec3(0.0f)));
        EXPECT_NEAR(
            Comet::Math::length(camera.position - camera.target),
            previous_distance,
            0.0001f);
        EXPECT_FALSE(Comet::Tests::TestUtils::Vec3Equal(
            camera.position, Comet::Math::Vec3(0.0f, 0.0f, 3.0f)));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.up, Comet::Math::Vec3(0.0f, 1.0f, 0.0f)));
    }

    TEST(EditorCameraControllerTest, PanMovesPositionAndTargetTogether) {
        EditorCameraState camera;
        const Comet::Math::Vec3 previous_position = camera.position;
        const Comet::Math::Vec3 previous_target = camera.target;

        EXPECT_TRUE(apply_editor_camera_input(camera, {
            .pan_delta = Comet::Math::Vec2(80.0f, -25.0f),
            .viewport_height = 600.0f
        }));

        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.position - previous_position,
            camera.target - previous_target));
        EXPECT_NEAR(
            Comet::Math::length(camera.position - camera.target),
            Comet::Math::length(previous_position - previous_target),
            0.0001f);
    }

    TEST(EditorCameraControllerTest, ZoomChangesDistanceAndClampsNearTarget) {
        EditorCameraState camera;

        EXPECT_TRUE(apply_editor_camera_input(camera, {
            .zoom_delta = 1.0f
        }));
        EXPECT_LT(
            Comet::Math::length(camera.position - camera.target), 3.0f);

        EXPECT_TRUE(apply_editor_camera_input(camera, {
            .zoom_delta = 1000.0f
        }));
        EXPECT_NEAR(
            Comet::Math::length(camera.position - camera.target),
            0.05f,
            0.0001f);
    }

    TEST(EditorCameraControllerTest, InvalidInputAndPanExtentAreIgnored) {
        EditorCameraState camera;
        const EditorCameraState previous = camera;

        EXPECT_FALSE(apply_editor_camera_input(camera, {
            .pan_delta = Comet::Math::Vec2(10.0f, 5.0f),
            .viewport_height = 0.0f
        }));
        EXPECT_FALSE(apply_editor_camera_input(camera, {
            .zoom_delta = std::numeric_limits<float>::quiet_NaN()
        }));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.position, previous.position));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.target, previous.target));

        camera.fov_degrees = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(apply_editor_camera_input(camera, {
            .pan_delta = Comet::Math::Vec2(10.0f, 5.0f),
            .viewport_height = 600.0f
        }));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.position, previous.position));
    }

    TEST(EditorCameraControllerTest, OrthographicModePansAndScalesWithoutOrbit) {
        EditorCameraState camera;
        camera.projection = Comet::CameraProjection::Orthographic;
        camera.position = Comet::Math::Vec3(3.0f, 2.0f, 4.0f);
        const Comet::Math::Vec3 previous_offset =
            camera.position - camera.target;

        EXPECT_TRUE(apply_editor_camera_input(camera, {
            .orbit_delta = Comet::Math::Vec2(100.0f, 50.0f),
            .pan_delta = Comet::Math::Vec2(20.0f, -10.0f),
            .zoom_delta = 1.0f,
            .viewport_height = 500.0f
        }));

        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            camera.position - camera.target, previous_offset));
        EXPECT_LT(camera.orthographic_height, 10.0f);
        EXPECT_FALSE(Comet::Tests::TestUtils::Vec3Equal(
            camera.target, Comet::Math::Vec3(0.0f)));
        EXPECT_FLOAT_EQ(camera.target.z, 0.0f);
    }
}
