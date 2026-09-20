#ifdef COMET_TEST_EDITOR_UI
#include "viewport/viewport_panel.h"
#include "scene/selection.h"
#include "viewport/transform_gizmo.h"
#include "ui/shortcuts.h"
#include "scene/systems/camera_controller.h"
#include "scene/scene_runtime.h"

#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace CometEditor::Tests {
    class ViewportGizmoUiTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui{{1000, 800}};
        Comet::Scene scene;
        Comet::SceneRuntime runtime;
        Comet::Entity entity = scene.create_entity();
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        CommandHistory history;
        PropertyEditTransaction property_edit{history, components};
        TransformGizmo gizmo{history, components};
        SelectionService selection{scene};
        EditorState state;
        EditorShortcuts shortcuts;
        ViewportPanel viewport{state, runtime, selection, gizmo, property_edit, 4096, shortcuts};
        int gizmo_vertices = 0;
        bool mesh_drag = false;
        bool show_other_panel = false;
        bool open_popup_after_viewport = false;
        bool focus_text_after_viewport = false;
        Comet::Input runtime_input;
        bool runtime_accepting = false;
        AssetDragPayload mesh_payload{.handle = Comet::AssetHandle(42),
            .revision = 1,
            .generation = 0,
            .type = Comet::AssetType::Mesh};
        std::size_t payload_size = sizeof(AssetDragPayload);

        void SetUp() override {
            runtime_input.focus_event(true);
            ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::CameraControllerSystem>()));
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            viewport.set_texture_id(static_cast<ImTextureID>(1), 800, 600);
            frame();
            frame();
        }

        void frame() {
            ImGui::NewFrame();
            if(show_other_panel) {
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(150, 100));
                ImGui::Begin("Other Panel");
                ImGui::TextUnformatted("Selection");
                ImGui::End();
            }
            if(mesh_drag && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload(
                    AssetDragPayload::TYPE, &mesh_payload, payload_size, ImGuiCond_Once);
                ImGui::TextUnformatted("Mesh");
                ImGui::EndDragDropSource();
            }
            ImGui::SetNextWindowPos(ImVec2(20, 40));
            ImGui::SetNextWindowSize(ImVec2(900, 700));
            viewport.render();
            auto* window = ImGui::FindWindowByName("Viewport");
            const int before = window ? window->DrawList->VtxBuffer.Size : 0;
            viewport.draw_gizmo();
            gizmo_vertices = window ? window->DrawList->VtxBuffer.Size - before : 0;
            if(open_popup_after_viewport) {
                ImGui::OpenPopup("Block runtime");
                open_popup_after_viewport = false;
            }
            if(ImGui::BeginPopupModal("Block runtime")) {
                ImGui::TextUnformatted("Modal");
                ImGui::EndPopup();
            }
            if(focus_text_after_viewport) {
                ImGui::SetNextWindowPos({0, 0});
                ImGui::Begin("Text editor");
                char text[64]{};
                ImGui::SetKeyboardFocusHere();
                ImGui::InputText("Name", text, sizeof(text));
                ImGui::End();
            }
            const auto& routed = viewport.route_runtime_input(runtime_input.publish_frame());
            runtime_accepting = routed.focused;
            if(state.mode == EditorMode::Play) {
                EXPECT_TRUE(runtime.advance(0.1, &routed));
            }
            ImGui::Render();
        }

        Comet::Math::Vec2 x_handle() {
            const auto handle =
                gizmo.handles(entity.get_uuid(), state.camera.snapshot(), viewport.get_layout())[0];
            EXPECT_TRUE(handle.has_value());
            if(!handle) {
                return {};
            }
            return (handle->segments.front().start + handle->segments.front().end) * 0.5f;
        }

        void move_pointer(const Comet::Math::Vec2 point) {
            ImGui::GetIO().AddMousePosEvent(point.x, point.y);
            frame();
        }

        void drag() {
            const auto point = x_handle();
            move_pointer(point);
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            EXPECT_EQ(gizmo.active_axis(), TransformGizmo::Axis::X);
            EXPECT_FALSE(viewport.take_pick_request());
            move_pointer(point + Comet::Math::Vec2(20, 0));
            move_pointer(point + Comet::Math::Vec2(40, 0));
        }

        float x() { return entity.get_component<Comet::TransformComponent>().translation.x; }

        void activate_play_camera() {
            state.mode = EditorMode::Play;
            entity.add_component<Comet::CameraComponent>().primary = true;
            entity.add_component<Comet::CameraControllerComponent>();
            ASSERT_TRUE(runtime.start(scene));
            frame();
            const auto& rect = viewport.get_layout().image_visible_rect;
            move_pointer((rect.min + rect.max) * 0.5f);
            EXPECT_TRUE(runtime_accepting);
        }
    };

    TEST_F(ViewportGizmoUiTest, PlayControlsReadRuntimeStateAndOnlyEmitOneCommand) {
        using Command = ViewportPanel::PlayCommand;
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

    TEST_F(ViewportGizmoUiTest, PlayCameraAcceptsHoverWithoutClickAndBlocksHeldKeysOnReentry) {
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

    TEST_F(ViewportGizmoUiTest, EscapeRequestsStopEvenWhenPlayViewportIsHidden) {
        activate_play_camera();
        runtime_input.key_event(Comet::Input::Key::W, true);
        runtime_input.key_event(Comet::Input::Key::Escape, true);
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(viewport.take_play_command(), ViewportPanel::PlayCommand::Stop);
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation, Comet::Math::Vec3(0));

        viewport.set_visible(false);
        runtime_input.key_event(Comet::Input::Key::Escape, false);
        frame();
        EXPECT_FALSE(viewport.take_play_command());
        runtime_input.key_event(Comet::Input::Key::Escape, true);
        frame();
        EXPECT_EQ(viewport.take_play_command(), ViewportPanel::PlayCommand::Stop);
        frame();
        EXPECT_FALSE(viewport.take_play_command());

        state.mode = EditorMode::Edit;
        runtime_input.key_event(Comet::Input::Key::Escape, false);
        frame();
        runtime_input.key_event(Comet::Input::Key::Escape, true);
        frame();
        EXPECT_FALSE(viewport.take_play_command());
    }

    TEST_F(ViewportGizmoUiTest, PopupOpenedAfterViewportStopsRuntimeInTheSameFrame) {
        activate_play_camera();
        runtime_input.key_event(Comet::Input::Key::W, true);
        open_popup_after_viewport = true;
        frame();
        EXPECT_FALSE(runtime_accepting);
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation, Comet::Math::Vec3(0));
    }

    TEST_F(ViewportGizmoUiTest, TextFocusAndModeChangesDoNotLeakInputToRuntime) {
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

    TEST_F(ViewportGizmoUiTest, LightAxisDragFromAnotherPanelCommitsAndUndoes) {
        entity.add_component<Comet::LightComponent>();
        entity.get_component<Comet::TransformComponent>().rotation = {-30, -35, 0};
        show_other_panel = true;
        for(const auto type :
            {Comet::LightType::Directional, Comet::LightType::Point, Comet::LightType::Spot}) {
            entity.get_component<Comet::LightComponent>().type = type;
            frame();
            ImGui::FocusWindow(ImGui::FindWindowByName("Other Panel"));
            frame();
            drag();
            ASSERT_GT(x(), 0);
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            EXPECT_FALSE(gizmo.active());
            EXPECT_EQ(history.undo_size(), 1);
            EXPECT_FALSE(viewport.take_pick_request());
            ASSERT_TRUE(history.undo());
            EXPECT_FLOAT_EQ(x(), 0);
            history.clear();
        }
    }

    TEST_F(ViewportGizmoUiTest, ToolMenuChangesInteractionPolicyWithoutEditingScene) {
        auto* window = ImGui::FindWindowByName("Viewport");
        ASSERT_NE(window, nullptr);
        ImGui::ActivateItemByID(window->GetID("Tool"));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Snap"));
        frame();
        EXPECT_TRUE(gizmo.settings().snap);
        EXPECT_FLOAT_EQ(gizmo.settings().translation_step, 0.25f);
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_FLOAT_EQ(x(), 0);
        ImGui::ActivateItemByID(popup->GetID("Space"));
        frame();
        frame();
        ASSERT_GE(GImGui->OpenPopupStack.Size, 2);
        auto* options = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(options, nullptr);
        // ImGui 的数组式 Combo 为各选项追加索引 ID。
        const int local_index = 1;
        const auto local_id =
            ImHashStr("Local", 0, ImHashData(&local_index, sizeof(local_index), options->ID));
        ImGui::ActivateItemByID(local_id);
        frame();
        EXPECT_EQ(gizmo.settings().space, TransformGizmo::Space::Local);
        EXPECT_EQ(history.undo_size(), 0);
        ImGui::ActivateItemByID(popup->GetID("Mode"));
        frame();
        frame();
        ASSERT_GE(GImGui->OpenPopupStack.Size, 2);
        options = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(options, nullptr);
        const int rotate_index = 1;
        ImGui::ActivateItemByID(
            ImHashStr("Rotate", 0, ImHashData(&rotate_index, sizeof(rotate_index), options->ID)));
        frame();
        EXPECT_EQ(gizmo.settings().mode, TransformGizmo::Mode::Rotate);
        EXPECT_FLOAT_EQ(gizmo.settings().rotation_step_degrees, 15);
        EXPECT_EQ(history.undo_size(), 0);
        ASSERT_TRUE(gizmo.set_settings(
            {.mode = TransformGizmo::Mode::Rotate, .space = TransformGizmo::Space::World}));
        ImGui::ActivateItemByID(popup->GetID("Mode"));
        frame();
        frame();
        ASSERT_GE(GImGui->OpenPopupStack.Size, 2);
        options = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(options, nullptr);
        const int scale_index = 2;
        ImGui::ActivateItemByID(
            ImHashStr("Scale", 0, ImHashData(&scale_index, sizeof(scale_index), options->ID)));
        frame();
        EXPECT_EQ(gizmo.settings().mode, TransformGizmo::Mode::Scale);
        EXPECT_FLOAT_EQ(gizmo.settings().scale_step, 0.1f);
        EXPECT_EQ(gizmo.settings().space, TransformGizmo::Space::World);
        ImGui::ActivateItemByID(popup->GetID("Mode"));
        frame();
        frame();
        ASSERT_GE(GImGui->OpenPopupStack.Size, 2);
        options = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(options, nullptr);
        const int move_index = 0;
        ImGui::ActivateItemByID(
            ImHashStr("Move", 0, ImHashData(&move_index, sizeof(move_index), options->ID)));
        frame();
        EXPECT_EQ(gizmo.settings().mode, TransformGizmo::Mode::Translate);
        EXPECT_EQ(gizmo.settings().space, TransformGizmo::Space::World);
        EXPECT_EQ(history.undo_size(), 0);
    }

    TEST_F(ViewportGizmoUiTest, CenterScaleCapturesInputAndPreservesComponentRatios) {
        state.camera.perspective.position = {0, 0, 3};
        state.camera.target = {};
        auto& transform = entity.get_component<Comet::TransformComponent>();
        transform.scale = {1, -2, 3};
        ASSERT_TRUE(gizmo.set_settings(
            {.mode = TransformGizmo::Mode::Scale, .snap = true, .scale_step = 0.25f}));
        frame();
        const auto handle =
            gizmo.handles(entity.get_uuid(), state.camera.snapshot(), viewport.get_layout())[3];
        ASSERT_TRUE(handle);
        const auto point = (handle->segments.front().start + handle->segments.front().end) * 0.5f;
        move_pointer(point);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        ASSERT_EQ(gizmo.active_axis(), TransformGizmo::Axis::All);
        move_pointer(point + glm::normalize(Comet::Math::Vec2(1, -1)) * 45.0f);
        EXPECT_EQ(transform.scale, Comet::Math::Vec3(1.5f, -3, 4.5f));
        EXPECT_FALSE(viewport.take_pick_request());
        EXPECT_FALSE(viewport.take_camera_input());
        EXPECT_EQ(history.undo_size(), 0);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(gizmo.active());
        EXPECT_EQ(ImGui::GetActiveID(), 0);
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(transform.scale, Comet::Math::Vec3(1, -2, 3));
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(transform.scale, Comet::Math::Vec3(1.5f, -3, 4.5f));
    }

    TEST_F(ViewportGizmoUiTest, RotationRingUsesSharedCaptureAndUndoTransaction) {
        state.camera.perspective.position = {0, 0, 3};
        state.camera.target = {};
        // ImGui 将鼠标坐标取整到逻辑像素；这里同时验证角度吸附的 UI 链路。
        ASSERT_TRUE(gizmo.set_settings({.mode = TransformGizmo::Mode::Rotate, .snap = true}));
        frame();
        const auto ring =
            gizmo.handles(entity.get_uuid(), state.camera.snapshot(), viewport.get_layout())[2];
        ASSERT_TRUE(ring);
        ASSERT_EQ(ring->segments.size(), 64);
        EXPECT_GT(gizmo_vertices, 0);
        move_pointer(ring->segments[4].start);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        ASSERT_EQ(gizmo.active_axis(), TransformGizmo::Axis::Z);
        move_pointer(ring->segments[12].start);
        EXPECT_NEAR(entity.get_component<Comet::TransformComponent>().rotation.z, 45, 0.001f);
        EXPECT_EQ(history.undo_size(), 0);
        ImGui::GetIO().AddMouseWheelEvent(0, 1);
        frame();
        EXPECT_NE(ImGui::GetKeyOwner(ImGuiKey_MouseWheelY), ImGuiKeyOwner_NoOwner);
        EXPECT_FALSE(viewport.take_camera_input());
        EXPECT_FALSE(viewport.take_pick_request());
        move_pointer(ring->segments[20].start);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(gizmo.active());
        EXPECT_EQ(ImGui::GetActiveID(), 0);
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_NEAR(entity.get_component<Comet::TransformComponent>().rotation.z, 90, 0.001f);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<Comet::TransformComponent>().rotation, Comet::Math::Vec3(0));
    }

    TEST_F(ViewportGizmoUiTest, MeshDropReportsOnePositionedRequestWithoutEditingScene) {
        mesh_payload.generation = history.generation();
        const auto& rect = viewport.get_layout().image_display_rect;
        const auto point = rect.min + rect.size() * Comet::Math::Vec2(0.75f, 0.25f);
        move_pointer(point);
        mesh_drag = true;
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        frame();
        EXPECT_FALSE(viewport.take_mesh_drop());
        EXPECT_FALSE(viewport.take_pick_request());
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        const auto request = viewport.take_mesh_drop();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->asset.handle, mesh_payload.handle);
        EXPECT_EQ(request->asset.revision, mesh_payload.revision);
        EXPECT_EQ(request->asset.generation, history.generation());
        const auto mouse = ImGui::GetIO().MousePos;
        const auto uv = (Comet::Math::Vec2(mouse.x, mouse.y) - rect.min) / rect.size();
        const auto expected =
            camera_focus_plane_point(state.camera, uv, rect.size().x / rect.size().y);
        ASSERT_TRUE(expected);
        EXPECT_LT(Comet::Math::length(request->position - *expected), 0.001f);
        EXPECT_FALSE(viewport.take_mesh_drop());
        EXPECT_EQ(scene.entity_count(), 1);
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_FALSE(gizmo.active());
    }

    TEST_F(ViewportGizmoUiTest, MeshDropRejectsPlayOutsideImageAndMalformedPayload) {
        const auto attempt = [&](const Comet::Math::Vec2 point) {
            mesh_drag = true;
            ImGui::GetIO().AddMousePosEvent(point.x, point.y);
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            frame();
            frame();
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            frame();
            EXPECT_FALSE(viewport.take_mesh_drop());
            mesh_drag = false;
            frame();
            frame();
        };
        const auto rect = viewport.get_layout().image_display_rect;
        attempt(rect.min - Comet::Math::Vec2(0, 10));
        payload_size = sizeof(std::uint64_t);
        attempt(rect.min + rect.size() * 0.5f);
        payload_size = sizeof(AssetDragPayload);
        mesh_payload.type = Comet::AssetType::Texture;
        attempt(rect.min + rect.size() * 0.5f);
        mesh_payload.type = Comet::AssetType::Mesh;
        state.mode = EditorMode::Play;
        attempt(rect.min + rect.size() * 0.5f);
        state.mode = EditorMode::Edit;
        viewport.clear_texture();
        attempt(rect.min + rect.size() * 0.5f);
        EXPECT_EQ(scene.entity_count(), 1);
        EXPECT_EQ(history.undo_size(), 0);
    }

    TEST_F(ViewportGizmoUiTest, ConfiguredFocusRequiresViewportFocusAndEditMode) {
        auto parsed =
            EditorShortcuts::parse("editor: {shortcuts: {viewport.focus_selection: [Primary+G]}}");
        ASSERT_TRUE(parsed);
        shortcuts = std::move(parsed).value();
        auto& io = ImGui::GetIO();
        ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));
        frame();
        frame();
        io.AddKeyEvent(ImGuiKey_F, true);
        frame();
        EXPECT_FALSE(viewport.take_focus_request());
        io.AddKeyEvent(ImGuiKey_F, false);
        frame();
        const auto modifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
        io.AddKeyEvent(modifier, true);
        io.AddKeyEvent(ImGuiKey_G, true);
        frame();
        EXPECT_TRUE(viewport.take_focus_request());
        EXPECT_FALSE(viewport.take_focus_request());
        io.AddKeyEvent(ImGuiKey_G, false);
        frame();
        ImGui::FocusWindow(nullptr);
        io.AddKeyEvent(ImGuiKey_G, true);
        frame();
        EXPECT_FALSE(viewport.take_focus_request());
        io.AddKeyEvent(ImGuiKey_G, false);
        ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));
        state.mode = EditorMode::Play;
        frame();
        io.AddKeyEvent(ImGuiKey_G, true);
        frame();
        EXPECT_FALSE(viewport.take_focus_request());
    }

    TEST_F(ViewportGizmoUiTest, AxisDragCapturesInputAndCommitsOneUndoOnRelease) {
        EXPECT_GT(gizmo_vertices, 0);
        drag();
        ASSERT_GT(x(), 0);
        EXPECT_NE(ImGui::GetActiveID(), 0);
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_FALSE(viewport.take_pick_request());
        EXPECT_FALSE(viewport.take_camera_input());

        // Inspector 隐藏时会提交自己的事务，不能提前提交 Gizmo 的拖动。
        ASSERT_TRUE(property_edit.commit());
        EXPECT_TRUE(gizmo.active());
        EXPECT_EQ(history.undo_size(), 0);
        ImGui::GetIO().AddMouseWheelEvent(0, 1);
        frame();
        EXPECT_NE(ImGui::GetKeyOwner(ImGuiKey_MouseWheelY), ImGuiKeyOwner_NoOwner);
        EXPECT_FALSE(viewport.take_camera_input());
        const float after = x();

        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        EXPECT_FALSE(gizmo.active());
        EXPECT_EQ(ImGui::GetActiveID(), 0);
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_FALSE(viewport.take_pick_request());
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 0);
        ASSERT_TRUE(history.redo());
        EXPECT_FLOAT_EQ(x(), after);
    }

    enum class CancelBoundary { Escape, Hidden, Collapsed, FocusLost, Play };

    class ViewportGizmoCancellationTest: public ViewportGizmoUiTest,
                                         public ::testing::WithParamInterface<CancelBoundary> {};

    TEST_P(ViewportGizmoCancellationTest, RestoresPreviewWithoutHistoryOrReactivation) {
        drag();
        ASSERT_GT(x(), 0);
        auto& io = ImGui::GetIO();
        switch(GetParam()) {
            case CancelBoundary::Escape:
                io.AddKeyEvent(ImGuiKey_Escape, true);
                break;
            case CancelBoundary::Hidden:
                viewport.set_visible(false);
                break;
            case CancelBoundary::Collapsed:
                ImGui::SetWindowCollapsed("Viewport", true);
                break;
            case CancelBoundary::FocusLost:
                io.AddFocusEvent(false);
                break;
            case CancelBoundary::Play:
                state.mode = EditorMode::Play;
                break;
        }
        frame();
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_FALSE(gizmo.active());
        EXPECT_EQ(ImGui::GetActiveID(), 0);
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(viewport.take_pick_request());
        if(GetParam() == CancelBoundary::Hidden || GetParam() == CancelBoundary::Collapsed) {
            EXPECT_FALSE(viewport.is_visible());
            EXPECT_EQ(viewport.get_requested_render_size(), Comet::Math::Vec2u{});
            EXPECT_EQ(viewport.get_layout().image_resolution, Comet::Math::Vec2u{});
        }
        io.AddKeyEvent(ImGuiKey_Escape, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_FALSE(gizmo.active());
        EXPECT_FALSE(history.can_undo());
    }

    INSTANTIATE_TEST_SUITE_P(InputBoundary, ViewportGizmoCancellationTest,
        ::testing::Values(CancelBoundary::Escape, CancelBoundary::Hidden, CancelBoundary::Collapsed,
            CancelBoundary::FocusLost, CancelBoundary::Play));

    TEST_F(ViewportGizmoUiTest, OptionLeftDragNavigatesCameraEvenOverAxis) {
        const auto point = x_handle();
        move_pointer(point);
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Alt, true);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        move_pointer(point + Comet::Math::Vec2(20, 10));
        const auto camera_input = viewport.take_camera_input();
        ASSERT_TRUE(camera_input.has_value());
        EXPECT_EQ(camera_input->orbit_delta, Comet::Math::Vec2(20, 10));
        EXPECT_FALSE(gizmo.active());
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(viewport.take_pick_request());
    }

    TEST_F(ViewportGizmoUiTest, ClickAwayFromHandlesStillRequestsPixelPicking) {
        const auto point = viewport.get_layout().image_visible_rect.min + Comet::Math::Vec2(20, 20);
        const auto pixel = map_viewport_point_to_pixel(viewport.get_layout(), point);
        ASSERT_TRUE(pixel.has_value());
        move_pointer(point);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        EXPECT_EQ(viewport.take_pick_request(), pixel);
        EXPECT_FALSE(viewport.take_pick_request());
        EXPECT_FALSE(gizmo.active());
        EXPECT_FALSE(history.can_undo());
        EXPECT_EQ(gizmo_vertices, 0);
    }
}
#endif
