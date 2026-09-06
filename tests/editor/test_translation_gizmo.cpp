#include "translation_gizmo.h"
#include "render/scene/scene_extractor.h"

#include <gtest/gtest.h>
#include <limits>

namespace {
    using namespace Comet;
    using namespace CometEditor;

    class TranslationGizmoTest: public ::testing::Test {
    protected:
        ComponentRegistry registry = create_scene_component_registry();
        Scene scene;
        Entity entity = scene.create_entity("Moved");
        CommandHistory history;
        TranslationGizmo gizmo{history, registry};
        RenderCamera camera{
            .view_matrix =
                Math::look_at(Math::Vec3(0, 0, 3), Math::Vec3(0), Math::Vec3(0, 1, 0)),
        };
        ViewportLayout layout{
            .panel_content_size = {800, 600},
            .render_resolution = {1600, 1200},
            .image_resolution = {1600, 1200},
            .image_display_rect = {{100, 200}, {900, 800}},
            .image_visible_rect = {{100, 200}, {900, 800}},
        };

        void SetUp() override { history.bind_scene(&scene); }

        bool update(const TranslationGizmo::Input& input) {
            return gizmo.update(entity.get_uuid(), camera, layout, input);
        }

        Math::Vec2 begin_x() {
            const auto handle = gizmo.handles(entity.get_uuid(), camera, layout)[0];
            EXPECT_TRUE(handle.has_value());
            if(!handle)
                return {};
            const auto position = (handle->start + handle->end) * 0.5f;
            EXPECT_TRUE(update(
                {.position = position, .hovered = true, .pressed = true, .down = true}));
            EXPECT_EQ(gizmo.active_axis(), TranslationGizmo::Axis::X);
            return position;
        }

        Math::Vec3 translation() {
            return entity.get_component<TransformComponent>().translation;
        }

        void expect_vector(const Math::Vec3 actual, const Math::Vec3 expected) {
            EXPECT_NEAR(actual.x, expected.x, 0.0001f);
            EXPECT_NEAR(actual.y, expected.y, 0.0001f);
            EXPECT_NEAR(actual.z, expected.z, 0.0001f);
        }
    };

    TEST_F(TranslationGizmoTest, LocalAxesFollowRotationAndIgnoreOwnNegativeScale) {
        auto& transform = entity.get_component<TransformComponent>();
        transform.rotation.z = 90;
        transform.scale = {-2, 0, 3};
        ASSERT_TRUE(gizmo.set_settings({.space = TranslationGizmo::Space::Local}));
        const auto handles = gizmo.handles(entity.get_uuid(), camera, layout);
        ASSERT_TRUE(handles[0]);
        EXPECT_NEAR(handles[0]->end.x, handles[0]->start.x, 0.001f);
        EXPECT_LT(handles[0]->end.y, handles[0]->start.y);
        const auto start = begin_x();
        EXPECT_TRUE(update({.position = start + Math::Vec2(0, -100), .released = true}));
        EXPECT_NEAR(translation().x, 0, 0.0001f);
        EXPECT_GT(translation().y, 0);
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        expect_vector(translation(), Math::Vec3(0));
        EXPECT_EQ(transform.scale, Math::Vec3(-2, 0, 3));
    }

    TEST_F(TranslationGizmoTest, LocalDragUsesParentAffineBasisAndWorldUnitSnap) {
        auto parent = scene.create_entity();
        auto& parent_transform = parent.get_component<TransformComponent>();
        parent_transform.rotation.z = 45;
        parent_transform.scale = {2, 3, 1};
        ASSERT_TRUE(scene.set_parent(entity, parent));
        entity.get_component<TransformComponent>().rotation.z = 30;
        ASSERT_TRUE(gizmo.set_settings(
            {.space = TranslationGizmo::Space::Local, .snap = true, .step = 0.25f}));
        const auto handle = gizmo.handles(entity.get_uuid(), camera, layout)[0];
        ASSERT_TRUE(handle);
        const auto direction = glm::normalize(handle->end - handle->start);
        const auto start = begin_x();
        ASSERT_TRUE(update({.position = start + direction * 100.0f, .released = true}));
        const auto world_delta = Math::Vec3(scene.get_world_matrix(entity)[3]);
        EXPECT_NEAR(Math::length(world_delta), 0.5f, 0.0001f);
        const auto local_axis = Math::normalize(
            Math::Vec3(Math::compose_trs({}, {0, 0, 30}, Math::Vec3(1))[0]));
        EXPECT_GT(Math::dot(Math::normalize(translation()), local_axis), 0.9999f);
        EXPECT_EQ(history.undo_size(), 1);
    }

    TEST_F(TranslationGizmoTest, SnapUsesStartRelativeDistanceAndSmallMotionIsNoOp) {
        entity.get_component<TransformComponent>().translation.x = 0.13f;
        ASSERT_TRUE(gizmo.set_settings({.snap = true, .step = 0.25f}));
        auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(10, 0), .released = true}));
        EXPECT_FLOAT_EQ(translation().x, 0.13f);
        EXPECT_EQ(history.undo_size(), 0);
        start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(-100, 0), .released = true}));
        EXPECT_NEAR(translation().x, -0.37f, 0.0001f);
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(translation().x, 0.13f);
        ASSERT_TRUE(history.redo());
        EXPECT_NEAR(translation().x, -0.37f, 0.0001f);
    }

    TEST_F(TranslationGizmoTest, SettingsChangesCancelGestureAndRejectInvalidStep) {
        const auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(60, 0), .down = true}));
        EXPECT_GT(translation().x, 0);
        EXPECT_FALSE(gizmo.set_settings({.step = 0}));
        EXPECT_FALSE(
            gizmo.set_settings({.step = std::numeric_limits<float>::quiet_NaN()}));
        EXPECT_TRUE(gizmo.active());
        ASSERT_TRUE(gizmo.set_settings({.space = TranslationGizmo::Space::Local}));
        EXPECT_FALSE(gizmo.active());
        expect_vector(translation(), Math::Vec3(0));
        EXPECT_EQ(history.undo_size(), 0);
        const auto local_start = begin_x();
        ASSERT_TRUE(update({.position = local_start + Math::Vec2(60, 0), .down = true}));
        entity.get_component<TransformComponent>().rotation.z = 15;
        ASSERT_TRUE(update({.position = local_start + Math::Vec2(80, 0), .down = true}));
        EXPECT_FALSE(gizmo.active());
        expect_vector(translation(), Math::Vec3(0));
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().rotation.z, 15);
        EXPECT_EQ(history.undo_size(), 0);
    }

    TEST_F(
        TranslationGizmoTest, ProjectsLogicalScreenHandlesWithUpwardYAndNoDegenerateZ) {
        const auto handles = gizmo.handles(entity.get_uuid(), camera, layout);
        ASSERT_TRUE(handles[0]);
        ASSERT_TRUE(handles[1]);
        EXPECT_FALSE(handles[2]);
        EXPECT_EQ(handles[0]->start, Math::Vec2(500, 500));
        EXPECT_NEAR(handles[0]->end.x - handles[0]->start.x, 90, 0.001f);
        EXPECT_NEAR(handles[1]->end.y - handles[1]->start.y, -90, 0.001f);
        layout.image_resolution *= 2U;
        const auto high_dpi = gizmo.handles(entity.get_uuid(), camera, layout);
        ASSERT_TRUE(high_dpi[0]);
        EXPECT_EQ(high_dpi[0]->start, handles[0]->start);
        EXPECT_EQ(high_dpi[0]->end, handles[0]->end);
    }

    TEST_F(TranslationGizmoTest, DragPreviewsCurrentSceneAndCommitsExactlyOnceOnRelease) {
        const auto start = begin_x();
        EXPECT_TRUE(update({.position = start + Math::Vec2(40, 0), .down = true}));
        EXPECT_GT(translation().x, 0.0f);
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_TRUE(update({.position = start + Math::Vec2(90, 0), .released = true}));
        EXPECT_FALSE(gizmo.active());
        EXPECT_FALSE(gizmo.active_axis());
        const auto after = translation();
        EXPECT_NEAR(
            after.x, 3.0f * std::tan(Math::radians(45.0f) * 0.5f) * 0.3f, 0.0001f);
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_FALSE(update({.position = start, .released = true}));
        ASSERT_TRUE(history.undo());
        expect_vector(translation(), Math::Vec3(0));
        ASSERT_TRUE(history.redo());
        expect_vector(translation(), after);
    }

    TEST_F(TranslationGizmoTest, EscapeRollsBackAndHoldingMouseDoesNotRestart) {
        const auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(100, 0), .down = true}));
        EXPECT_TRUE(update({.position = start, .down = true, .cancel = true}));
        EXPECT_FALSE(gizmo.active());
        EXPECT_EQ(translation(), Math::Vec3(0));
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(update({.position = start, .hovered = true, .down = true}));
        EXPECT_FALSE(gizmo.active());
    }

    TEST_F(TranslationGizmoTest, NoMovementAndCancelledDragPreserveRedo) {
        PropertyEditTransaction edit(history, registry);
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "transform", "translation"}));
        ASSERT_TRUE(edit.preview(Math::Vec3(1, 0, 0)));
        ASSERT_TRUE(edit.commit());
        ASSERT_TRUE(history.undo());
        auto start = begin_x();
        ASSERT_TRUE(update({.position = start, .released = true}));
        EXPECT_FALSE(history.can_undo());
        EXPECT_TRUE(history.can_redo());
        start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        ASSERT_TRUE(gizmo.cancel());
        EXPECT_FALSE(history.can_undo());
        EXPECT_TRUE(history.can_redo());
    }

    TEST_F(TranslationGizmoTest, ContinuesOutsideImageButMissingReleaseCancels) {
        auto start = begin_x();
        const auto outside = Math::Vec2(1000, 500);
        ASSERT_TRUE(update({.position = outside, .hovered = false, .down = true}));
        EXPECT_GT(translation().x, 0.0f);
        ASSERT_TRUE(update({.position = outside, .released = true}));
        EXPECT_TRUE(history.can_undo());
        ASSERT_TRUE(history.undo());
        start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        ASSERT_TRUE(update({.position = outside}));
        EXPECT_FALSE(gizmo.active());
        EXPECT_EQ(translation(), Math::Vec3(0));
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(
        TranslationGizmoTest, SelectionAndSceneChangesInvalidateWithoutWritingNewScene) {
        auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        const auto other = scene.create_entity("Other");
        EXPECT_TRUE(gizmo.update(
            other.get_uuid(), camera, layout, {.position = start, .down = true}));
        EXPECT_EQ(translation(), Math::Vec3(0));
        EXPECT_FALSE(gizmo.active());
        start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        Scene replacement;
        auto replaced =
            replacement.create_entity_with_uuid(entity.get_uuid(), "Replacement");
        history.bind_scene(&replacement);
        EXPECT_FALSE(gizmo.active());
        EXPECT_TRUE(update({.position = start, .down = true}));
        EXPECT_EQ(
            replaced.get_component<TransformComponent>().translation, Math::Vec3(0));
        EXPECT_FALSE(history.can_undo());
        history.bind_scene(nullptr);
    }

    TEST_F(TranslationGizmoTest, DeletedTargetAndUnboundHistoryClearTheGesture) {
        const auto start = begin_x();
        const auto uuid = entity.get_uuid();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        scene.destroy_entity(entity);
        EXPECT_TRUE(
            gizmo.update(uuid, camera, layout, {.position = start, .down = true}));
        EXPECT_FALSE(gizmo.active());
        EXPECT_FALSE(history.can_undo());
        history.bind_scene(nullptr);
        EXPECT_FALSE(gizmo.handles(uuid, camera, layout)[0]);
        EXPECT_FALSE(gizmo.update(uuid, camera, layout,
            {.position = start, .hovered = true, .pressed = true, .down = true}));
    }

    TEST_F(TranslationGizmoTest, ConvertsWorldAxisMovementThroughRotatedScaledParent) {
        auto parent = scene.create_entity("Parent");
        auto& parent_transform = parent.get_component<TransformComponent>();
        parent_transform.rotation.z = 90.0f;
        parent_transform.scale = Math::Vec3(2, 3, 1);
        ASSERT_TRUE(scene.set_parent(entity, parent));
        const auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(90, 0), .released = true}));
        EXPECT_NEAR(translation().x, 0, 0.0001f);
        EXPECT_LT(translation().y, 0);
        const auto world = scene.get_world_matrix(entity)[3];
        EXPECT_GT(world.x, 0);
        EXPECT_NEAR(world.y, 0, 0.0001f);
        EXPECT_NEAR(world.z, 0, 0.0001f);
        ASSERT_TRUE(history.undo());
        expect_vector(translation(), Math::Vec3(0));
    }

    TEST_F(TranslationGizmoTest, NoMotionUnderParentDoesNotInventAnUndoRecord) {
        auto parent = scene.create_entity("Parent");
        auto& parent_transform = parent.get_component<TransformComponent>();
        parent_transform.translation = Math::Vec3(0.1f, -0.2f, 0.3f);
        parent_transform.rotation = Math::Vec3(13, 29, 47);
        parent_transform.scale = Math::Vec3(0.7f, 1.3f, 0.8f);
        ASSERT_TRUE(scene.set_parent(entity, parent));
        entity.get_component<TransformComponent>().translation = Math::Vec3(0.3f);
        const auto before = translation();
        const auto start = begin_x();
        ASSERT_TRUE(update({.position = start, .released = true}));
        EXPECT_EQ(translation(), before);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(TranslationGizmoTest, InspectorFinishingItsOwnTransactionDoesNotCommitGizmo) {
        PropertyEditTransaction inspector_edit(history, registry);
        const auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        ASSERT_TRUE(inspector_edit.commit());
        EXPECT_TRUE(gizmo.active());
        EXPECT_FALSE(history.can_undo());
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .released = true}));
        EXPECT_EQ(history.undo_size(), 1);
    }

    TEST_F(
        TranslationGizmoTest, ParentTransformAndReparentingCancelToOriginalLocalValue) {
        auto parent = scene.create_entity("Parent");
        ASSERT_TRUE(scene.set_parent(entity, parent));
        auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        parent.get_component<TransformComponent>().rotation.z = 30;
        EXPECT_TRUE(update({.position = start, .down = true}));
        EXPECT_EQ(translation(), Math::Vec3(0));
        start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        auto same_matrix_parent = scene.create_entity("Replacement parent");
        same_matrix_parent.get_component<TransformComponent>() =
            parent.get_component<TransformComponent>();
        ASSERT_TRUE(scene.set_parent(entity, same_matrix_parent));
        EXPECT_TRUE(update({.position = start, .down = true}));
        EXPECT_EQ(translation(), Math::Vec3(0));
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(TranslationGizmoTest, CameraAndLayoutChangesCancelInsteadOfJumping) {
        auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        camera.fov_degrees = 60;
        EXPECT_TRUE(update({.position = start, .down = true}));
        EXPECT_EQ(translation(), Math::Vec3(0));
        start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        layout.image_display_rect.min.x += 1;
        EXPECT_TRUE(update({.position = start, .down = true}));
        EXPECT_EQ(translation(), Math::Vec3(0));
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(TranslationGizmoTest, OrthographicScaleAndHitRadiusAreLogicalPoints) {
        camera.projection = RenderCamera::Projection::Orthographic;
        camera.orthographic_height = 10;
        const auto handle = gizmo.handles(entity.get_uuid(), camera, layout)[0];
        ASSERT_TRUE(handle);
        const auto middle = (handle->start + handle->end) * 0.5f;
        EXPECT_FALSE(update({.position = middle + Math::Vec2(0, 8),
            .hovered = true,
            .pressed = true,
            .down = true}));
        EXPECT_FALSE(gizmo.hovered_axis());
        EXPECT_FALSE(update({.position = middle + Math::Vec2(0, 6), .hovered = true}));
        EXPECT_EQ(gizmo.hovered_axis(), TranslationGizmo::Axis::X);
        const auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(60, 0), .released = true}));
        expect_vector(translation(), Math::Vec3(1, 0, 0));
    }

    TEST_F(TranslationGizmoTest, ObliqueZDragUpdatesWorldSnapshotBeforeCommit) {
        camera.view_matrix =
            Math::look_at(Math::Vec3(3, 2, 4), Math::Vec3(0), Math::Vec3(0, 1, 0));
        entity.add_component<MeshRendererComponent>();
        const auto handle = gizmo.handles(entity.get_uuid(), camera, layout)[2];
        ASSERT_TRUE(handle);
        const auto start = handle->start + (handle->end - handle->start) * 0.8f;
        ASSERT_TRUE(
            update({.position = start, .hovered = true, .pressed = true, .down = true}));
        ASSERT_EQ(gizmo.active_axis(), TranslationGizmo::Axis::Z);
        const auto moved = start + glm::normalize(handle->end - handle->start) * 30.0f;
        ASSERT_TRUE(update({.position = moved, .down = true}));
        EXPECT_GT(translation().z, 0);
        EXPECT_FLOAT_EQ(translation().x, 0);
        EXPECT_FLOAT_EQ(translation().y, 0);
        const auto snapshot = SceneExtractor::extract(scene);
        ASSERT_EQ(snapshot.render_items.size(), 1);
        expect_vector(
            Math::Vec3(snapshot.render_items[0].model_matrix[3]), translation());
        EXPECT_FALSE(history.can_undo());
        ASSERT_TRUE(update({.position = moved, .released = true}));
        EXPECT_EQ(history.undo_size(), 1);
    }

    TEST_F(TranslationGizmoTest, UnhoveredAndClippedPressesCannotBegin) {
        const auto handle = gizmo.handles(entity.get_uuid(), camera, layout)[0];
        ASSERT_TRUE(handle);
        const auto position = (handle->start + handle->end) * 0.5f;
        EXPECT_FALSE(update({.position = position, .pressed = true, .down = true}));
        layout.image_visible_rect.max.x = 510;
        EXPECT_FALSE(update(
            {.position = position, .hovered = true, .pressed = true, .down = true}));
        EXPECT_FALSE(gizmo.active());
    }

    TEST_F(
        TranslationGizmoTest, CrossingClipPlanesHidesHandlesButKeepsActiveTransaction) {
        camera.view_matrix =
            Math::look_at(Math::Vec3(3, 2, 4), Math::Vec3(0), Math::Vec3(0, 1, 0));
        camera.projection = RenderCamera::Projection::Orthographic;
        camera.near_clip = 4;
        camera.far_clip = 6;
        for(const float distance : {-120.0f, 120.0f}) {
            SCOPED_TRACE(distance);
            const auto handle = gizmo.handles(entity.get_uuid(), camera, layout)[2];
            ASSERT_TRUE(handle);
            const auto start = handle->start + (handle->end - handle->start) * 0.8f;
            ASSERT_TRUE(update(
                {.position = start, .hovered = true, .pressed = true, .down = true}));
            ASSERT_EQ(gizmo.active_axis(), TranslationGizmo::Axis::Z);
            const auto moved =
                start + glm::normalize(handle->end - handle->start) * distance;
            ASSERT_TRUE(update({.position = moved, .down = true}));
            const auto outside_translation = translation();
            const float depth =
                -(camera.view_matrix * Math::Vec4(outside_translation, 1.0f)).z;
            EXPECT_TRUE(depth < camera.near_clip || depth > camera.far_clip);
            for(const auto& hidden : gizmo.handles(entity.get_uuid(), camera, layout)) {
                EXPECT_FALSE(hidden);
            }
            ASSERT_TRUE(update({.position = moved, .down = true}));
            EXPECT_TRUE(gizmo.active());
            expect_vector(translation(), outside_translation);
            EXPECT_FALSE(history.can_undo());
            ASSERT_TRUE(update({.position = moved, .released = true}));
            EXPECT_EQ(history.undo_size(), 1);
            ASSERT_TRUE(history.undo());
            expect_vector(translation(), Math::Vec3(0));
        }
    }

    TEST_F(TranslationGizmoTest, RejectsNonFiniteSingularAndDepthClippedGeometry) {
        auto parent = scene.create_entity("Parent");
        ASSERT_TRUE(scene.set_parent(entity, parent));
        parent.get_component<TransformComponent>().scale.y = 0;
        EXPECT_FALSE(gizmo.handles(entity.get_uuid(), camera, layout)[0]);
        ASSERT_TRUE(scene.clear_parent(entity));
        entity.get_component<TransformComponent>().translation.z = 4;
        EXPECT_FALSE(gizmo.handles(entity.get_uuid(), camera, layout)[0]);
        entity.get_component<TransformComponent>().translation.z = -2000;
        EXPECT_FALSE(gizmo.handles(entity.get_uuid(), camera, layout)[0]);
        entity.get_component<TransformComponent>().translation = Math::Vec3(0);
        camera.view_matrix = Math::Mat4(0);
        EXPECT_FALSE(gizmo.handles(entity.get_uuid(), camera, layout)[0]);
        camera.view_matrix =
            Math::look_at(Math::Vec3(0, 0, 3), Math::Vec3(0), Math::Vec3(0, 1, 0));
        camera.fov_degrees = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(gizmo.handles(entity.get_uuid(), camera, layout)[0]);
        camera.fov_degrees = 45;
        layout.image_resolution = Math::Vec2u(0);
        EXPECT_FALSE(gizmo.handles(entity.get_uuid(), camera, layout)[0]);
    }

    TEST_F(
        TranslationGizmoTest, InvalidPointerCancelsWithoutRecordingNonFiniteTranslation) {
        const auto start = begin_x();
        ASSERT_TRUE(update({.position = start + Math::Vec2(80, 0), .down = true}));
        EXPECT_TRUE(
            update({.position = Math::Vec2(std::numeric_limits<float>::infinity()),
                .down = true}));
        EXPECT_EQ(translation(), Math::Vec3(0));
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(gizmo.active());
    }
}
