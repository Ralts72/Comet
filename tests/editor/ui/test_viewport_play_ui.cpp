#ifdef COMET_TEST_EDITOR_UI
#include "support/viewport_fixture.h"
#include "asset/registry.h"
#include "common/scope_exit.h"
#include "core/project.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"

#include <algorithm>
#include <array>

namespace CometEditor::Tests {
    using ViewportPlayUiTest = ViewportUiTest;

    TEST_F(ViewportPlayUiTest, MouseClickInsidePlayImageKeepsProjectUiInputUntilRelease) {
        using Button = Comet::Input::MouseButton;
        viewport.set_game_ui_available(true);
        activate_play_camera();
        EXPECT_TRUE(viewport.route_game_ui_input(runtime_input.get_frame(), false).focused);
        auto& io = ImGui::GetIO();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        runtime_input.mouse_button_event(Button::Left, true);
        frame();
        auto input = viewport.route_game_ui_input(runtime_input.get_frame(), false);
        EXPECT_TRUE(input.mouse(Button::Left).pressed);
        frame();
        input = viewport.route_game_ui_input(runtime_input.get_frame(), false);
        EXPECT_TRUE(input.focused);
        EXPECT_TRUE(input.mouse(Button::Left).down);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        runtime_input.mouse_button_event(Button::Left, false);
        frame();
        input = viewport.route_game_ui_input(runtime_input.get_frame(), false);
        EXPECT_TRUE(input.focused);
        EXPECT_TRUE(input.mouse(Button::Left).released);
    }

    TEST_F(ViewportPlayUiTest, ProjectUiInputUsesPlayFocusAndReleasesBeforeGameReacquires) {
        using Key = Comet::Input::Key;
        viewport.set_game_ui_available(true);
        auto view = viewport.game_ui_view({1600, 1200});
        ASSERT_TRUE(view);
        EXPECT_EQ(view->pixel_size, Comet::Math::Vec2u(1600, 1200));
        EXPECT_EQ(view->size, viewport.get_layout().image_display_rect.size());
        for(const auto pixels : {Comet::Math::Vec2u(800, 600), Comet::Math::Vec2u(1600, 1200),
                Comet::Math::Vec2u(400, 300)}) {
            const auto scaled = viewport.game_ui_view(pixels);
            ASSERT_TRUE(scaled);
            // 同一 16dp 控件不随离屏分辨率改变显示大小。
            EXPECT_NEAR(16 * scaled->density * scaled->size.x / pixels.x, 16, 0.0001f);
            EXPECT_EQ(scaled->origin, view->origin);
        }
        EXPECT_FALSE(viewport.route_game_ui_input(runtime_input.get_frame(), false).focused);
        activate_play_camera();
        const auto tick_ui = [&](bool blocked = false) {
            return viewport.route_game_ui_input(runtime_input.publish_frame(), blocked);
        };
        ASSERT_TRUE(tick_ui().focused);
        runtime_input.key_event(Key::W, true);
        EXPECT_TRUE(tick_ui().key(Key::W).pressed);
        auto blocked = tick_ui(true);
        EXPECT_FALSE(blocked.focused);
        EXPECT_TRUE(blocked.key(Key::W).released);
        EXPECT_FALSE(tick_ui().key(Key::W).down);
        runtime_input.key_event(Key::W, false);
        tick_ui();
        runtime_input.key_event(Key::W, true);
        EXPECT_TRUE(tick_ui().key(Key::W).pressed);
        move_pointer({2000, 2000});
        EXPECT_TRUE(tick_ui().focused);
        EXPECT_FALSE(tick_ui().pointer_enabled);
        EXPECT_FALSE(
            viewport.route_runtime_input(runtime_input.get_frame(), false, true).pointer_enabled);
        state.mode = EditorMode::Edit;
        const auto stopped = tick_ui();
        EXPECT_FALSE(stopped.focused);
        EXPECT_TRUE(stopped.key(Key::W).released);
    }

    TEST_F(ViewportPlayUiTest, CameraCaptureKeepsRelativeMotionOutsideImageUntilRelease) {
        using Actions = Comet::InputActions;
        auto actions = Actions::create(
            {{"camera.look", Actions::Type::Button, {{Comet::Input::MouseButton::Right}}},
                {"camera.look_x", Actions::Type::Delta, {{Actions::Motion::CursorX}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        activate_play_camera();
        runtime_input.cursor_event({20, 20});
        frame();
        runtime_input.mouse_button_event(Comet::Input::MouseButton::Right, true);
        frame();
        ASSERT_TRUE(runtime.wants_cursor_capture());
        EXPECT_EQ(entity.get_component<Comet::TransformComponent>().rotation, Comet::Math::Vec3(0));

        runtime_input.cursor_event({220, 20});
        move_pointer({2000, 2000});
        EXPECT_TRUE(runtime_accepting);
        EXPECT_TRUE(runtime.wants_cursor_capture());
        EXPECT_NEAR(entity.get_component<Comet::TransformComponent>().rotation.y, -40, 0.0001f);

        runtime_input.mouse_button_event(Comet::Input::MouseButton::Right, false);
        frame();
        EXPECT_FALSE(runtime.wants_cursor_capture());
        frame();
        const auto& routed = viewport.route_runtime_input(runtime_input.get_frame());
        EXPECT_FALSE(routed.pointer_enabled);
    }

    TEST_F(ViewportPlayUiTest, CameraCaptureFollowsReboundActionAndModalAuthorization) {
        auto actions = Comet::InputActions::create(
            {{"camera.look", Comet::InputActions::Type::Button, {{Comet::Input::Key::Space}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        activate_play_camera();
        runtime_input.key_event(Comet::Input::Key::Space, true);
        frame();
        ASSERT_TRUE(runtime.wants_cursor_capture());
        runtime_ui_blocked = true;
        frame();
        EXPECT_FALSE(runtime.wants_cursor_capture());
        runtime_ui_blocked = false;
        frame();
        EXPECT_FALSE(runtime.wants_cursor_capture());
        runtime_input.key_event(Comet::Input::Key::Space, false);
        frame();
        runtime_input.key_event(Comet::Input::Key::Space, true);
        frame();
        ASSERT_TRUE(runtime.wants_cursor_capture());

        ASSERT_TRUE(runtime.set_state(Comet::SceneRuntime::State::Paused));
        EXPECT_FALSE(runtime.wants_cursor_capture());
        frame();
        ASSERT_TRUE(runtime.request_step());
        frame();
        EXPECT_FALSE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.set_state(Comet::SceneRuntime::State::Running));
        EXPECT_FALSE(runtime.wants_cursor_capture());
        frame();
        EXPECT_TRUE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(runtime.wants_cursor_capture());
    }

    TEST_F(ViewportPlayUiTest, PlayerInputMenuOnlyRequestsSettingsInActivePlay) {
        auto* window = ImGui::FindWindowByName("Viewport");
        ASSERT_NE(window, nullptr);
        const auto input_button = window->GetID("输入###Input");
        ImGui::ActivateItemByID(input_button);
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        const auto select_input = [&] {
            ImGui::ActivateItemByID(window->GetID("视图###View"));
            frame();
            frame();
            ASSERT_FALSE(GImGui->OpenPopupStack.empty());
            auto* popup = GImGui->OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID("输入###Input"));
            frame();
        };
        activate_play_camera();
        select_input();
        EXPECT_EQ(viewport.take_play_command(), PlayCommand::InputSettings);
        EXPECT_FALSE(viewport.take_play_command());
        EXPECT_EQ(runtime.get_state(), Comet::SceneRuntime::State::Running);
        EXPECT_FALSE(runtime_accepting);
        ASSERT_TRUE(runtime.set_state(Comet::SceneRuntime::State::Paused));
        frame();
        select_input();
        EXPECT_EQ(viewport.take_play_command(), PlayCommand::InputSettings);
        EXPECT_EQ(runtime.get_state(), Comet::SceneRuntime::State::Paused);
        ASSERT_TRUE(runtime.stop());
        frame();
        select_input();
        EXPECT_FALSE(viewport.take_play_command());
    }

    TEST_F(ViewportPlayUiTest, NarrowToolbarKeepsPreviewChoicesAndViewMenuAvailable) {
        viewport.set_game_ui_available(true);
        activate_play_camera();
        ASSERT_TRUE(runtime.set_state(Comet::SceneRuntime::State::Paused));
        const auto tick = [&] { frame({300, 700}); };
        tick();
        auto* window = ImGui::FindWindowByName("Viewport");
        ASSERT_NE(window, nullptr);
        ImGui::ActivateItemByID(window->GetID("###Preview"));
        tick();
        tick();
        ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("1280 x 720"));
        tick();
        EXPECT_EQ(viewport.get_layout().render_resolution, Comet::Math::Vec2u(1280, 720));
        ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
        viewport.set_texture_id(static_cast<ImTextureID>(1), 1280, 720);
        ImGui::ActivateItemByID(popup->GetID("1:1"));
        tick();
        EXPECT_EQ(viewport.get_layout().image_display_rect.size(), Comet::Math::Vec2(1280, 720));
        EXPECT_GT(viewport.get_layout().image_visible_rect.min.y,
            window->DC.CursorStartPos.y + ImGui::GetFrameHeightWithSpacing() * 2);
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(runtime.get_state(), Comet::SceneRuntime::State::Paused);

        ImGui::ClosePopupToLevel(0, true);
        ImGui::ActivateItemByID(window->GetID("###View"));
        tick();
        tick();
        ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
        popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("游戏 UI###Game UI"));
        tick();
        EXPECT_FALSE(viewport.game_ui_view({1280, 720}));
        EXPECT_FALSE(viewport.take_play_command());
    }

    TEST_F(ViewportPlayUiTest, RuntimeUiBlocksEscapeAndClosingFrameWithoutReplayingHeldKeys) {
        activate_play_camera();
        runtime_ui_blocked = true;
        runtime_input.key_event(Comet::Input::Key::W, true);
        runtime_input.key_event(Comet::Input::Key::Escape, true);
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_FALSE(viewport.take_play_command());
        const auto position = entity.get_component<Comet::TransformComponent>().translation;
        frame();
        EXPECT_EQ(entity.get_component<Comet::TransformComponent>().translation, position);
        runtime_ui_blocked = false;
        frame();
        EXPECT_TRUE(runtime_accepting);
        EXPECT_FALSE(viewport.take_play_command());
        EXPECT_EQ(entity.get_component<Comet::TransformComponent>().translation, position);
        runtime_input.key_event(Comet::Input::Key::W, false);
        runtime_input.key_event(Comet::Input::Key::Escape, false);
        frame();
        runtime_input.key_event(Comet::Input::Key::W, true);
        frame();
        EXPECT_NE(entity.get_component<Comet::TransformComponent>().translation, position);
        runtime_input.key_event(Comet::Input::Key::Escape, true);
        frame();
        EXPECT_EQ(viewport.take_play_command(), PlayCommand::Stop);
    }

    TEST_F(ViewportPlayUiTest, PlayControlsReadRuntimeStateAndOnlyEmitOneCommand) {
        using Command = PlayCommand;
        using State = Comet::SceneRuntime::State;
        auto* window = ImGui::FindWindowByName("Viewport");
        ASSERT_NE(window, nullptr);
        ImGui::ActivateItemByID(window->GetID("暂停###Pause"));
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        ImGui::ActivateItemByID(window->GetID("运行###Play"));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Play);
        EXPECT_EQ(state.mode, EditorMode::Edit);

        activate_play_camera();
        ImGui::ActivateItemByID(window->GetID("单步###Step"));
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        ImGui::ActivateItemByID(window->GetID("暂停###Pause"));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Pause);
        EXPECT_FALSE(viewport.take_play_command());
        EXPECT_EQ(runtime.get_state(), State::Running);

        ASSERT_TRUE(runtime.set_state(State::Paused));
        frame();
        const auto fixed_index = runtime.get_timing().fixed_index;
        ImGui::ActivateItemByID(window->GetID("单步###Step"));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Step);
        EXPECT_EQ(runtime.get_timing().fixed_index, fixed_index);
        ASSERT_TRUE(runtime.request_step());
        frame();
        EXPECT_EQ(runtime.get_timing().fixed_index, fixed_index + 1);
        EXPECT_EQ(runtime.get_state(), State::Paused);
        frame();
        EXPECT_EQ(runtime.get_timing().fixed_index, fixed_index + 1);
        ImGui::ActivateItemByID(window->GetID("继续###Resume"));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Resume);
        EXPECT_EQ(runtime.get_state(), State::Paused);
        ASSERT_TRUE(runtime.stop());
        frame();
        ImGui::ActivateItemByID(window->GetID("单步###Step"));
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        ImGui::ActivateItemByID(window->GetID("暂停###Pause"));
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        ImGui::ActivateItemByID(window->GetID("停止###Stop"));
        frame();
        EXPECT_EQ(viewport.take_play_command(), Command::Stop);
        EXPECT_EQ(history.undo_size(), 0u);
    }

    TEST_F(ViewportPlayUiTest, PlayKeyboardFollowsFocusAndBlocksHeldKeysOnReentry) {
        activate_play_camera();
        const auto& transform = entity.get_component<Comet::TransformComponent>();
        runtime_input.key_event(Comet::Input::Key::W, true);
        frame();
        EXPECT_NEAR(transform.translation.z, -0.3f, 0.00001f);
        move_pointer({990, 790});
        EXPECT_TRUE(runtime_accepting);
        EXPECT_NEAR(transform.translation.z, -0.6f, 0.00001f);
        show_other_panel = true;
        frame();
        ImGui::SetWindowFocus("Other Panel");
        const auto before = transform.translation;
        frame();
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
        EXPECT_NEAR(transform.translation.z, before.z - 0.3f, 0.00001f);
        viewport.set_visible(false);
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_NEAR(transform.translation.z, before.z - 0.3f, 0.00001f);
    }

    TEST_F(ViewportPlayUiTest, ModifiersReachPlayWithoutInterruptingHeldMovement) {
        using Key = Comet::Input::Key;
        struct Modifier {
            Key physical;
            ImGuiKey key;
            ImGuiKey aggregate;
        };
        const Modifier modifiers[]{{Key::LeftControl, ImGuiKey_LeftCtrl, ImGuiMod_Ctrl},
            {Key::RightControl, ImGuiKey_RightCtrl, ImGuiMod_Ctrl},
            {Key::LeftAlt, ImGuiKey_LeftAlt, ImGuiMod_Alt},
            {Key::RightAlt, ImGuiKey_RightAlt, ImGuiMod_Alt},
            {Key::LeftSuper, ImGuiKey_LeftSuper, ImGuiMod_Super},
            {Key::RightSuper, ImGuiKey_RightSuper, ImGuiMod_Super}};
        activate_play_camera();
        const auto& position = entity.get_component<Comet::TransformComponent>().translation;
        runtime_input.key_event(Key::W, true);
        frame();
        auto& io = ImGui::GetIO();
        for(const bool mac : {false, true}) {
            SCOPED_TRACE(mac);
            io.ConfigMacOSXBehaviors = mac;
            for(const auto& modifier : modifiers) {
                SCOPED_TRACE(int(modifier.physical));
                const auto before = position.z;
                runtime_input.key_event(modifier.physical, true);
                io.AddKeyEvent(modifier.aggregate, true);
                io.AddKeyEvent(modifier.key, true);
                frame();
                EXPECT_TRUE(io.KeyCtrl || io.KeySuper || io.KeyAlt);
                EXPECT_TRUE(runtime_accepting);
                const auto& pressed = viewport.route_runtime_input(runtime_input.get_frame());
                EXPECT_TRUE(pressed.key(modifier.physical).down);
                EXPECT_TRUE(pressed.key(modifier.physical).pressed);
                EXPECT_TRUE(pressed.key(Key::W).down);
                EXPECT_FALSE(pressed.key(Key::W).pressed);
                EXPECT_FALSE(pressed.key(Key::W).released);
                EXPECT_LT(position.z, before);

                const auto after_press = position.z;
                frame();
                const auto& held = viewport.route_runtime_input(runtime_input.get_frame());
                EXPECT_TRUE(held.key(modifier.physical).down);
                EXPECT_FALSE(held.key(modifier.physical).pressed);
                EXPECT_TRUE(held.key(Key::W).down);
                EXPECT_FALSE(held.key(Key::W).released);
                EXPECT_LT(position.z, after_press);

                const auto after_hold = position.z;
                runtime_input.key_event(modifier.physical, false);
                io.AddKeyEvent(modifier.aggregate, false);
                io.AddKeyEvent(modifier.key, false);
                frame();
                EXPECT_TRUE(runtime_accepting);
                const auto& released = viewport.route_runtime_input(runtime_input.get_frame());
                EXPECT_TRUE(released.key(modifier.physical).released);
                EXPECT_FALSE(released.key(modifier.physical).down);
                EXPECT_TRUE(released.key(Key::W).down);
                EXPECT_FALSE(released.key(Key::W).pressed);
                EXPECT_FALSE(released.key(Key::W).released);
                EXPECT_LT(position.z, after_hold);
            }
        }
    }

    TEST_F(ViewportPlayUiTest, ReboundModifierControlsRealDemoLuaWithoutRestartingItsState) {
        using Key = Comet::Input::Key;
        const auto project = Comet::Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        ASSERT_TRUE(runtime.set_input_actions(project.value().input_actions()));
        const std::array<std::filesystem::path, 1> roots{"scripts/spin.lua"};
        const auto scripts = Comet::Script::load_group(project.value().paths().assets(), roots);
        ASSERT_TRUE(scripts) << scripts.error().message;
        Comet::AssetRegistry assets;
        const Comet::ScopeExit stop_runtime([&] { static_cast<void>(runtime.stop()); });
        const Comet::AssetHandle script_handle{42};
        ASSERT_TRUE(assets.register_asset(script_handle, scripts.value().front()));
        entity.add_component<Comet::ScriptComponent>().asset = script_handle;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::ScriptSystem>(assets)));
        activate_play_camera();
        const auto& rotation = entity.get_component<Comet::TransformComponent>().rotation;
        EXPECT_GT(rotation.y, 0);
        runtime_input.key_event(Key::Space, true);
        frame();
        runtime_input.key_event(Key::Space, false);
        frame();
        const auto paused_rotation = rotation.y;

        auto actions = project.value().input_actions().actions();
        const auto spin =
            std::ranges::find(actions, "spin.toggle", &Comet::InputActions::Action::name);
        ASSERT_NE(spin, actions.end());
        ASSERT_FALSE(spin->bindings.empty());
        ASSERT_TRUE(spin->id);
        ASSERT_TRUE(spin->bindings.front().id);
        spin->bindings.front().control = Key::LeftControl;
        auto rebound = Comet::InputActions::create(
            std::move(actions), project.value().input_actions().contexts());
        ASSERT_TRUE(rebound);
        const auto fixed_index = runtime.get_timing().fixed_index;
        ASSERT_TRUE(runtime.rebind_input_actions(std::move(rebound).value()));
        runtime_ui_blocked = true;
        frame();
        runtime_ui_blocked = false;
        frame();
        EXPECT_GT(runtime.get_timing().fixed_index, fixed_index);
        EXPECT_FLOAT_EQ(rotation.y, paused_rotation);
        runtime_input.key_event(Key::Space, true);
        frame();
        runtime_input.key_event(Key::Space, false);
        frame();
        EXPECT_FLOAT_EQ(rotation.y, paused_rotation);

        auto& io = ImGui::GetIO();
        runtime_input.key_event(Key::LeftControl, true);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        frame();
        EXPECT_TRUE(runtime_accepting);
        EXPECT_GT(rotation.y, paused_rotation);
        const auto resumed_rotation = rotation.y;
        frame();
        EXPECT_GT(rotation.y, resumed_rotation);
        runtime_input.key_event(Key::LeftControl, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        frame();
        const auto before_second_press = rotation.y;
        runtime_input.key_event(Key::LeftControl, true);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        frame();
        EXPECT_FLOAT_EQ(rotation.y, before_second_press);
        frame();
        EXPECT_FLOAT_EQ(rotation.y, before_second_press);
        EXPECT_TRUE(runtime.is_active());
    }

    TEST_F(ViewportPlayUiTest, WindowSwitchingBlocksPlayAndDoesNotReplayHeldMovement) {
        using Key = Comet::Input::Key;
        auto& io = ImGui::GetIO();
        io.ConfigMacOSXBehaviors = false;
        GImGui->ConfigNavWindowingKeyNext = ImGuiMod_Ctrl | ImGuiKey_Tab;
        GImGui->ConfigNavWindowingKeyPrev = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        show_other_panel = true;
        activate_play_camera();
        const auto& position = entity.get_component<Comet::TransformComponent>().translation;
        runtime_input.key_event(Key::W, true);
        runtime_input.key_event(Key::LeftControl, true);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        frame();
        EXPECT_TRUE(runtime_accepting);
        EXPECT_LT(position.z, 0);
        ASSERT_EQ(GImGui->NavWindowingTarget, nullptr);
        const auto before_switch = position;

        runtime_input.key_event(Key::Tab, true);
        io.AddKeyEvent(ImGuiKey_Tab, true);
        frame();
        ASSERT_NE(GImGui->NavWindowingTarget, nullptr);
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(position, before_switch);
        const auto& blocked = viewport.route_runtime_input(runtime_input.get_frame());
        EXPECT_TRUE(blocked.key(Key::W).released);
        EXPECT_FALSE(blocked.key(Key::Tab).pressed);
        runtime_input.key_event(Key::Tab, false);
        io.AddKeyEvent(ImGuiKey_Tab, false);
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(position, before_switch);

        runtime_input.key_event(Key::LeftControl, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        frame();
        ASSERT_EQ(GImGui->NavWindowingTarget, nullptr);
        const auto& rect = viewport.get_layout().image_visible_rect;
        move_pointer((rect.min + rect.max) * 0.5f);
        EXPECT_TRUE(runtime_accepting);
        EXPECT_EQ(position, before_switch);
        const auto& reacquired = viewport.route_runtime_input(runtime_input.get_frame());
        EXPECT_FALSE(reacquired.key(Key::W).down);
        EXPECT_FALSE(reacquired.key(Key::W).pressed);
        runtime_input.key_event(Key::W, false);
        frame();
        runtime_input.key_event(Key::W, true);
        frame();
        EXPECT_LT(position.z, before_switch.z);
    }

    TEST_F(ViewportPlayUiTest, PlayAndResumeFocusViewportWithoutMovingPointerOffToolbar) {
        show_other_panel = true;
        frame();
        auto* window = ImGui::FindWindowByName("Viewport");
        ASSERT_NE(window, nullptr);
        // 浮动面板保留键盘焦点，但不遮挡现在靠左的播放按钮。
        ImGui::BringWindowToDisplayFront(window);
        const auto play_id = window->GetID("运行###Play");
        for(float x = window->WorkRect.Min.x; x < window->WorkRect.Max.x; x += 4) {
            move_pointer({x, window->DC.CursorStartPos.y + 5});
            if(GImGui->HoveredId == play_id)
                break;
        }
        ASSERT_EQ(GImGui->HoveredId, play_id);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        runtime_input.mouse_button_event(Comet::Input::MouseButton::Left, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        runtime_input.mouse_button_event(Comet::Input::MouseButton::Left, false);
        frame();
        ASSERT_EQ(viewport.take_play_command(), PlayCommand::Play);
        EXPECT_FALSE(runtime_accepting);

        entity.add_component<Comet::CameraComponent>(Comet::CameraComponent{.primary = true});
        entity.add_component<Comet::CameraControllerComponent>();
        ASSERT_TRUE(runtime.start(scene));
        state.mode = EditorMode::Play;
        ImGui::SetWindowFocus("Other Panel");
        frame();
        EXPECT_TRUE(runtime_accepting);
        ASSERT_NE(GImGui->NavWindow, nullptr);
        EXPECT_EQ(GImGui->NavWindow->RootWindow->ID, window->RootWindow->ID);
        const auto& transform = entity.get_component<Comet::TransformComponent>();
        runtime_input.key_event(Comet::Input::Key::W, true);
        runtime_input.scroll_event({0, 20});
        runtime_input.mouse_button_event(Comet::Input::MouseButton::Right, true);
        frame();
        EXPECT_NEAR(transform.translation.z, -0.3f, 0.00001f);
        const auto& routed = viewport.route_runtime_input(runtime_input.get_frame());
        EXPECT_FALSE(routed.pointer_enabled);
        EXPECT_TRUE(routed.key(Comet::Input::Key::W).down);
        EXPECT_FALSE(routed.mouse(Comet::Input::MouseButton::Right).down);
        EXPECT_EQ(routed.scroll, Comet::Math::Vec2(0));
        runtime_input.key_event(Comet::Input::Key::W, false);
        runtime_input.mouse_button_event(Comet::Input::MouseButton::Right, false);
        frame();

        ASSERT_TRUE(runtime.set_state(Comet::SceneRuntime::State::Paused));
        frame();
        ImGui::SetWindowFocus("Other Panel");
        frame();
        EXPECT_FALSE(runtime_accepting);
        ASSERT_TRUE(runtime.request_step());
        frame();
        EXPECT_FALSE(runtime_accepting);
        ASSERT_TRUE(runtime.set_state(Comet::SceneRuntime::State::Running));
        frame();
        EXPECT_TRUE(runtime_accepting);
        runtime_input.key_event(Comet::Input::Key::W, true);
        frame();
        EXPECT_NEAR(transform.translation.z, -0.6f, 0.00001f);
        ASSERT_TRUE(runtime.stop());
        state.mode = EditorMode::Edit;
        frame();
        EXPECT_FALSE(runtime_accepting);
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
