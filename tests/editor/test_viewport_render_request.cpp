#include <gtest/gtest.h>

#include "editor_state.h"
#include "../test_utils.h"

namespace CometEditor::Tests {
    TEST(ViewportRenderRequestTest, EditModeUsesEditorOnlyCamera) {
        EditorState state;
        state.camera.position = Comet::Math::Vec3(4.0f, 3.0f, 2.0f);

        const Comet::ViewportRenderRequest request =
            make_viewport_render_request(
                state, true, true, Comet::Math::Vec2u(1280, 720));

        EXPECT_TRUE(request.visible);
        EXPECT_EQ(request.render_size, Comet::Math::Vec2u(1280, 720));
        EXPECT_EQ(
            request.camera_source,
            Comet::ViewportCameraSource::Explicit);
        EXPECT_EQ(
            request.input_policy,
            Comet::ViewportInputPolicy::EditorCamera);
        ASSERT_TRUE(request.explicit_camera);
        EXPECT_TRUE(Comet::Tests::TestUtils::Mat4Equal(
            request.explicit_camera->view_matrix,
            Comet::Math::look_at(
                state.camera.position,
                state.camera.target,
                state.camera.up)));
    }

    TEST(ViewportRenderRequestTest, PlayModeUsesRuntimeSceneCamera) {
        EditorState state;
        state.mode = EditorMode::Play;

        const Comet::ViewportRenderRequest request =
            make_viewport_render_request(
                state, true, true, Comet::Math::Vec2u(1920, 1080));

        EXPECT_EQ(
            request.camera_source,
            Comet::ViewportCameraSource::ScenePrimary);
        EXPECT_EQ(
            request.input_policy,
            Comet::ViewportInputPolicy::RuntimeScene);
        EXPECT_FALSE(request.explicit_camera);
    }

    TEST(ViewportRenderRequestTest, HiddenViewportDisablesInput) {
        EditorState state;

        const Comet::ViewportRenderRequest request =
            make_viewport_render_request(
                state, false, false, Comet::Math::Vec2u(0));

        EXPECT_FALSE(request.visible);
        EXPECT_EQ(
            request.input_policy,
            Comet::ViewportInputPolicy::Disabled);
    }

    TEST(ViewportRenderRequestTest, UnfocusedViewportDisablesInputOnly) {
        EditorState state;

        const Comet::ViewportRenderRequest request =
            make_viewport_render_request(
                state, true, false, Comet::Math::Vec2u(1280, 720));

        EXPECT_TRUE(request.visible);
        EXPECT_EQ(
            request.camera_source,
            Comet::ViewportCameraSource::Explicit);
        EXPECT_TRUE(request.explicit_camera);
        EXPECT_EQ(
            request.input_policy,
            Comet::ViewportInputPolicy::Disabled);
    }
}
