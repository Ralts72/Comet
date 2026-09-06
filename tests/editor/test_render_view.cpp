#include <gtest/gtest.h>

#include "editor_state.h"
#include "../test_utils.h"

namespace CometEditor::Tests {
    TEST(RenderViewTest, EditModeUsesCameraOverride) {
        EditorState state;
        state.camera.perspective.position = Comet::Math::Vec3(4.0f, 3.0f, 2.0f);

        const Comet::RenderView view =
            make_render_view(state, true, Comet::Math::Vec2u(1280, 720));

        EXPECT_TRUE(view.visible);
        EXPECT_EQ(view.render_size, Comet::Math::Vec2u(1280, 720));
        EXPECT_EQ(view.camera_selection, Comet::RenderView::CameraSelection::Override);
        ASSERT_TRUE(view.camera_override);
        EXPECT_TRUE(Comet::Tests::TestUtils::Mat4Equal(view.camera_override->view_matrix,
            Comet::Math::look_at(state.camera.perspective.position, state.camera.target,
                state.camera.perspective.up)));
    }

    TEST(RenderViewTest, PlayModeUsesScenePrimaryCamera) {
        EditorState state;
        state.mode = EditorMode::Play;

        const Comet::RenderView view =
            make_render_view(state, true, Comet::Math::Vec2u(1920, 1080));

        EXPECT_EQ(
            view.camera_selection, Comet::RenderView::CameraSelection::ScenePrimary);
        EXPECT_FALSE(view.camera_override);
    }

    TEST(RenderViewTest, OrthographicEditorCameraUsesFixedAxis) {
        EditorState state;
        state.camera.perspective.position = Comet::Math::Vec3(4.0f, 3.0f, 2.0f);
        state.camera.target = Comet::Math::Vec3(1.0f, 2.0f, 0.0f);
        state.camera.perspective.up = Comet::Math::Vec3(1.0f, 0.0f, 0.0f);
        state.camera.projection = Comet::RenderCamera::Projection::Orthographic;
        state.camera.orthographic.height = 12.0f;

        const Comet::RenderView view =
            make_render_view(state, true, Comet::Math::Vec2u(1280, 720));

        ASSERT_TRUE(view.camera_override);
        EXPECT_EQ(view.camera_override->projection,
            Comet::RenderCamera::Projection::Orthographic);
        EXPECT_FLOAT_EQ(view.camera_override->orthographic_height, 12.0f);
        const float distance =
            Comet::Math::length(state.camera.perspective.position - state.camera.target);
        EXPECT_TRUE(Comet::Tests::TestUtils::Mat4Equal(view.camera_override->view_matrix,
            Comet::Math::look_at(
                state.camera.target + Comet::Math::Vec3(0.0f, 0.0f, distance),
                state.camera.target, Comet::Math::Vec3(0.0f, 1.0f, 0.0f))));
    }

    TEST(RenderViewTest, HiddenViewportPreservesCameraSelection) {
        EditorState state;

        const Comet::RenderView view =
            make_render_view(state, false, Comet::Math::Vec2u(0));

        EXPECT_FALSE(view.visible);
        EXPECT_EQ(view.camera_selection, Comet::RenderView::CameraSelection::Override);
        EXPECT_TRUE(view.camera_override);
    }
}
