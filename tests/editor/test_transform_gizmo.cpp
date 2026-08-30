#include "transform_gizmo.h"

#include "../test_utils.h"

#include <gtest/gtest.h>

#include <cmath>

namespace CometEditor::Tests {
    namespace {
        EditorCameraState camera() {
            EditorCameraState result;
            result.position = Comet::Math::Vec3(0.0f, 0.0f, 5.0f);
            result.target = Comet::Math::Vec3(0.0f);
            result.fov_degrees = 60.0f;
            return result;
        }
    }

    TEST(TransformGizmoTest, KeepsPerspectiveAxesAtStableScreenScale) {
        const auto frame = make_translation_gizmo_frame(
            camera(),
            Comet::Math::Vec3(0.0f),
            Comet::Math::Vec2u(1000, 1000),
            80.0f);

        ASSERT_TRUE(frame);
        const auto origin = project_translation_gizmo_point(
            *frame, frame->origin);
        const auto x_end = project_translation_gizmo_point(
            *frame,
            frame->origin + Comet::Math::Vec3(frame->axis_length, 0.0f, 0.0f));
        ASSERT_TRUE(origin);
        ASSERT_TRUE(x_end);
        EXPECT_NEAR(std::abs(x_end->x - origin->x), 80.0f, 0.001f);
    }

    TEST(TransformGizmoTest, PicksVisibleAxisInTexturePixelSpace) {
        const auto frame = make_translation_gizmo_frame(
            camera(),
            Comet::Math::Vec3(0.0f),
            Comet::Math::Vec2u(1000, 1000),
            80.0f);

        ASSERT_TRUE(frame);
        EXPECT_EQ(
            pick_translation_gizmo_axis(
                *frame, Comet::Math::Vec2(550.0f, 500.0f), 8.0f),
            TransformGizmoAxis::X);
        EXPECT_EQ(
            pick_translation_gizmo_axis(
                *frame, Comet::Math::Vec2(500.0f, 550.0f), 8.0f),
            TransformGizmoAxis::Y);
        EXPECT_FALSE(pick_translation_gizmo_axis(
            *frame, Comet::Math::Vec2(700.0f, 700.0f), 8.0f));
        EXPECT_EQ(
            pick_translation_gizmo_axis(
                *frame, Comet::Math::Vec2(500.0f, 500.0f), 8.0f),
            TransformGizmoAxis::X);
    }

    TEST(TransformGizmoTest, KeepsOrthographicAxesAtStableScreenScale) {
        EditorCameraState orthographic_camera = camera();
        orthographic_camera.projection = Comet::CameraProjection::Orthographic;
        orthographic_camera.orthographic_height = 10.0f;
        const auto frame = make_translation_gizmo_frame(
            orthographic_camera,
            Comet::Math::Vec3(0.0f),
            Comet::Math::Vec2u(1000, 1000),
            80.0f);

        ASSERT_TRUE(frame);
        const auto origin = project_translation_gizmo_point(
            *frame, frame->origin);
        const auto x_end = project_translation_gizmo_point(
            *frame,
            frame->origin + Comet::Math::Vec3(frame->axis_length, 0.0f, 0.0f));
        ASSERT_TRUE(origin);
        ASSERT_TRUE(x_end);
        EXPECT_NEAR(std::abs(x_end->x - origin->x), 80.0f, 0.001f);
    }

    TEST(TransformGizmoTest, ConvertsWorldAxisDragIntoParentLocalTranslation) {
        const auto frame = make_translation_gizmo_frame(
            camera(),
            Comet::Math::Vec3(2.0f, 0.0f, 0.0f),
            Comet::Math::Vec2u(1000, 1000),
            80.0f);
        ASSERT_TRUE(frame);
        const Comet::Math::Mat4 parent_world = Comet::Math::translate(
            Comet::Math::Mat4(1.0f), Comet::Math::Vec3(2.0f, 0.0f, 0.0f));
        const Comet::Math::Mat4 rotated_parent_world = Comet::Math::rotate(
            parent_world,
            Comet::Math::radians(90.0f),
            Comet::Math::Vec3(0.0f, 0.0f, 1.0f));
        const auto projected_origin = project_translation_gizmo_point(
            *frame, frame->origin);
        ASSERT_TRUE(projected_origin);
        const auto drag = begin_translation_gizmo_drag(
            *frame,
            TransformGizmoAxis::X,
            *projected_origin,
            Comet::Math::inverse(rotated_parent_world));

        ASSERT_TRUE(drag);
        const Comet::Ray moved_ray{
            .origin = Comet::Math::Vec3(3.0f, 0.0f, 4.9f),
            .direction = Comet::Math::Vec3(0.0f, 0.0f, -1.0f)
        };
        const auto translation = update_translation_gizmo_drag(
            *drag, moved_ray);

        ASSERT_TRUE(translation);
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            *translation, Comet::Math::Vec3(0.0f, -1.0f, 0.0f), 0.0001f));
    }

    TEST(TransformGizmoTest, RejectsCameraParallelAxisDrag) {
        const auto frame = make_translation_gizmo_frame(
            camera(),
            Comet::Math::Vec3(0.0f),
            Comet::Math::Vec2u(1000, 1000),
            80.0f);

        ASSERT_TRUE(frame);
        EXPECT_FALSE(begin_translation_gizmo_drag(
            *frame,
            TransformGizmoAxis::Z,
            Comet::Math::Vec2(500.0f, 500.0f),
            Comet::Math::Mat4(1.0f)));
    }

    TEST(TransformGizmoTest, AddsThreeColoredArrowAxes) {
        const auto frame = make_translation_gizmo_frame(
            camera(),
            Comet::Math::Vec3(0.0f),
            Comet::Math::Vec2u(1000, 1000),
            80.0f);
        Comet::DebugDrawList draw_list;

        ASSERT_TRUE(frame);
        EXPECT_TRUE(add_translation_gizmo(
            draw_list,
            *frame,
            TransformGizmoAxis::Y,
            std::nullopt));
        EXPECT_EQ(draw_list.line_count(), 9u);
    }

    TEST(TransformGizmoTest, ControllerCommitsOneCompletedDrag) {
        const auto frame = make_translation_gizmo_frame(
            camera(),
            Comet::Math::Vec3(0.0f),
            Comet::Math::Vec2u(1000, 1000),
            80.0f);
        ASSERT_TRUE(frame);
        const TranslationGizmoContext context{
            .entity_uuid = Comet::EntityUuid::generate(),
            .local_translation = Comet::Math::Vec3(0.0f),
            .frame = *frame,
            .hit_radius_pixels = 8.0f
        };
        TranslationGizmoController controller;

        const auto pressed = controller.update(
            context,
            TranslationGizmoPointerInput{
                .pixel_position = Comet::Math::Vec2(550.0f, 500.0f),
                .pointer_over_image = true,
                .pressed = true,
                .down = true
            });
        EXPECT_TRUE(pressed.pointer_press_consumed);
        EXPECT_EQ(controller.active_axis(), TransformGizmoAxis::X);

        const auto dragged = controller.update(
            context,
            TranslationGizmoPointerInput{
                .pixel_position = Comet::Math::Vec2(600.0f, 500.0f),
                .pointer_over_image = true,
                .down = true
            });
        ASSERT_TRUE(dragged.edit);
        EXPECT_GT(dragged.edit->translation.x, 0.0f);

        TranslationGizmoContext released_context = context;
        released_context.local_translation = dragged.edit->translation;
        const auto released = controller.update(
            released_context,
            TranslationGizmoPointerInput{
                .pixel_position = Comet::Math::Vec2(600.0f, 500.0f),
                .pointer_over_image = true,
                .released = true
            });
        ASSERT_TRUE(released.commit);
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            released.commit->before, Comet::Math::Vec3(0.0f)));
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            released.commit->after, dragged.edit->translation, 0.0001f));
        EXPECT_FALSE(controller.active_axis());
    }

    TEST(TransformGizmoTest, ControllerCancelRestoresTransactionStart) {
        const auto frame = make_translation_gizmo_frame(
            camera(),
            Comet::Math::Vec3(0.0f),
            Comet::Math::Vec2u(1000, 1000),
            80.0f);
        ASSERT_TRUE(frame);
        const TranslationGizmoContext context{
            .entity_uuid = Comet::EntityUuid::generate(),
            .local_translation = Comet::Math::Vec3(2.0f, 3.0f, 4.0f),
            .frame = *frame,
            .hit_radius_pixels = 8.0f
        };
        TranslationGizmoController controller;
        ASSERT_TRUE(controller.update(
            context,
            TranslationGizmoPointerInput{
                .pixel_position = Comet::Math::Vec2(550.0f, 500.0f),
                .pointer_over_image = true,
                .pressed = true,
                .down = true
            }).pointer_press_consumed);

        const auto restore = controller.cancel();

        ASSERT_TRUE(restore);
        EXPECT_EQ(restore->entity_uuid, context.entity_uuid);
        EXPECT_TRUE(Comet::Tests::TestUtils::Vec3Equal(
            restore->translation, context.local_translation));
        EXPECT_FALSE(controller.active_axis());
    }
}
