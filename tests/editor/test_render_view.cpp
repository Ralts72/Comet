#include <gtest/gtest.h>

#include "editor_state.h"
#include "../test_utils.h"

namespace CometEditor::Tests {
    TEST(RenderViewTest, EditModeUsesCameraOverride) {
        EditorState state;
        state.camera.position = Comet::Math::Vec3(4.0f, 3.0f, 2.0f);

        const Comet::RenderView view =
            make_render_view(state, true, Comet::Math::Vec2u(1280, 720));

        EXPECT_TRUE(view.visible);
        EXPECT_EQ(view.render_size, Comet::Math::Vec2u(1280, 720));
        EXPECT_EQ(view.camera_selection, Comet::RenderView::CameraSelection::Override);
        ASSERT_TRUE(view.camera_override);
        EXPECT_TRUE(Comet::Tests::TestUtils::Mat4Equal(view.camera_override->view_matrix,
            Comet::Math::look_at(
                state.camera.position, state.camera.target, state.camera.up)));
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

    TEST(RenderViewTest, HiddenViewportPreservesCameraSelection) {
        EditorState state;

        const Comet::RenderView view =
            make_render_view(state, false, Comet::Math::Vec2u(0));

        EXPECT_FALSE(view.visible);
        EXPECT_EQ(view.camera_selection, Comet::RenderView::CameraSelection::Override);
        EXPECT_TRUE(view.camera_override);
    }
}
