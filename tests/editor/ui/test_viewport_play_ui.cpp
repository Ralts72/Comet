#ifdef COMET_TEST_EDITOR_UI
#include "support/viewport_fixture.h"

namespace CometEditor::Tests {
    using ViewportPlayUiTest = ViewportUiTest;

    TEST_F(ViewportPlayUiTest, PlayControlsReadRuntimeStateAndOnlyEmitOneCommand) {
        using Command = PlayCommand;
        using State = Comet::SceneRuntime::State;
        auto* window = ImGui::FindWindowByName("Viewport");
        ASSERT_NE(window, nullptr);
        ImGui::ActivateItemByID(window->GetID("||##Pause"));
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        ImGui::ActivateItemByID(window->GetID(Ui::label("Play").c_str()));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Play);
        EXPECT_EQ(state.mode, EditorMode::Edit);

        activate_play_camera();
        ImGui::ActivateItemByID(window->GetID("|>##Step"));
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        ImGui::ActivateItemByID(window->GetID("||##Pause"));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Pause);
        EXPECT_FALSE(viewport.take_play_command());
        EXPECT_EQ(runtime.get_state(), State::Running);

        ASSERT_TRUE(runtime.set_state(State::Paused));
        frame();
        const auto fixed_index = runtime.get_timing().fixed_index;
        ImGui::ActivateItemByID(window->GetID("|>##Step"));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Step);
        EXPECT_EQ(runtime.get_timing().fixed_index, fixed_index);
        ASSERT_TRUE(runtime.request_step());
        frame();
        EXPECT_EQ(runtime.get_timing().fixed_index, fixed_index + 1);
        EXPECT_EQ(runtime.get_state(), State::Paused);
        frame();
        EXPECT_EQ(runtime.get_timing().fixed_index, fixed_index + 1);
        ImGui::ActivateItemByID(window->GetID(">##Resume"));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Resume);
        EXPECT_EQ(runtime.get_state(), State::Paused);
        ASSERT_TRUE(runtime.stop());
        frame();
        ImGui::ActivateItemByID(window->GetID("|>##Step"));
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        ImGui::ActivateItemByID(window->GetID("||##Pause"));
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        ImGui::ActivateItemByID(window->GetID(Ui::label("Stop").c_str()));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Stop);
        EXPECT_EQ(history.undo_size(), 0u);
    }

    TEST_F(ViewportPlayUiTest, PlayCameraAcceptsHoverWithoutClickAndBlocksHeldKeysOnReentry) {
        activate_play_camera();
        const auto& transform = entity.get_component<Comet::TransformComponent>();
        runtime_input.key_event(Comet::Input::Key::W, true);
        frame();
        EXPECT_NEAR(transform.translation.z, -0.3f, 0.00001f);
        const auto before = transform.translation;
        move_pointer({990, 790});
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(transform.translation, before);
        const auto& rect = viewport.get_layout().image_visible_rect;
        move_pointer((rect.min + rect.max) * 0.5f);
        EXPECT_TRUE(runtime_accepting);
        EXPECT_EQ(transform.translation, before);
        runtime_input.key_event(Comet::Input::Key::W, false);
        frame();
        runtime_input.key_event(Comet::Input::Key::W, true);
        frame();
        EXPECT_NEAR(transform.translation.z, -0.6f, 0.00001f);
        viewport.set_visible(false);
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_NEAR(transform.translation.z, -0.6f, 0.00001f);
    }

    TEST_F(ViewportPlayUiTest, EscapeRequestsStopEvenWhenPlayViewportIsHidden) {
        activate_play_camera();
        runtime_input.key_event(Comet::Input::Key::W, true);
        runtime_input.key_event(Comet::Input::Key::Escape, true);
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(viewport.take_play_command(), PlayCommand::Stop);
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation, Comet::Math::Vec3(0));

        viewport.set_visible(false);
        runtime_input.key_event(Comet::Input::Key::Escape, false);
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        runtime_input.key_event(Comet::Input::Key::Escape, true);
        frame();
        EXPECT_EQ(viewport.take_play_command(), PlayCommand::Stop);
        frame();
        EXPECT_FALSE(viewport.take_play_command());

        state.mode = EditorMode::Edit;
        runtime_input.key_event(Comet::Input::Key::Escape, false);
        frame();
        runtime_input.key_event(Comet::Input::Key::Escape, true);
        frame();
        EXPECT_FALSE(viewport.take_play_command());
    }

    TEST_F(ViewportPlayUiTest, PopupOpenedAfterViewportStopsRuntimeInTheSameFrame) {
        activate_play_camera();
        runtime_input.key_event(Comet::Input::Key::W, true);
        open_popup_after_viewport = true;
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation, Comet::Math::Vec3(0));
    }

    TEST_F(ViewportPlayUiTest, TextFocusAndModeChangesDoNotLeakInputToRuntime) {
        activate_play_camera();
        runtime_input.key_event(Comet::Input::Key::W, true);
        focus_text_after_viewport = true;
        frame();
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation, Comet::Math::Vec3(0));
        viewport.cancel_interaction();
        state.mode = EditorMode::Edit;
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation, Comet::Math::Vec3(0));
    }

}
#endif
