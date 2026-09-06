#ifdef COMET_TEST_EDITOR_UI
#include "panels/view.h"
#include "selection.h"
#include "transform_gizmo.h"
#include "runtime/scene_runtime.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace CometEditor::Tests {
    class ViewportGizmoUiTest: public ::testing::Test {
    protected:
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity();
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        CommandHistory history;
        PropertyEditTransaction property_edit{history, components};
        TransformGizmo gizmo{history, components};
        SelectionService selection{scene};
        EditorState state;
        Comet::SceneRuntime runtime;
        ViewPanel viewport{state, runtime, selection, gizmo, property_edit, 4096};
        int gizmo_vertices = 0;
        bool mesh_drag = false;
        bool input_popup = false;
        AssetDragPayload mesh_payload{Comet::AssetHandle(42), 0, Comet::AssetType::Mesh};
        std::size_t payload_size = sizeof(AssetDragPayload);

        void SetUp() override {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(1000, 800);
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            viewport.set_texture_id(static_cast<ImTextureID>(1), 800, 600);
            frame();
            frame();
        }

        void TearDown() override { ImGui::DestroyContext(); }

        void frame() {
            ImGui::NewFrame();
            if(mesh_drag && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload(
                    AssetDragPayload::TYPE, &mesh_payload, payload_size, ImGuiCond_Once);
                ImGui::TextUnformatted("Mesh");
                ImGui::EndDragDropSource();
            }
            ImGui::SetNextWindowPos(ImVec2(20, 40));
            ImGui::SetNextWindowSize(ImVec2(900, 700));
            viewport.render();
            if(input_popup) {
                ImGui::OpenPopup("Input blocker");
                if(ImGui::BeginPopup("Input blocker")) {
                    ImGui::TextUnformatted("Popup owns input");
                    ImGui::EndPopup();
                }
            }
            auto* window = ImGui::FindWindowByName("Viewport");
            const int before = window ? window->DrawList->VtxBuffer.Size : 0;
            viewport.draw_gizmo();
            gizmo_vertices = window ? window->DrawList->VtxBuffer.Size - before : 0;
            ImGui::Render();
        }

        Comet::Math::Vec2 x_handle() {
            const auto handle = gizmo.handles(
                entity.get_uuid(), state.camera.snapshot(), viewport.get_layout())[0];
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

        float x() {
            return entity.get_component<Comet::TransformComponent>().translation.x;
        }
    };

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
        const auto local_id = ImHashStr(
            "Local", 0, ImHashData(&local_index, sizeof(local_index), options->ID));
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
        ImGui::ActivateItemByID(ImHashStr(
            "Rotate", 0, ImHashData(&rotate_index, sizeof(rotate_index), options->ID)));
        frame();
        EXPECT_EQ(gizmo.settings().mode, TransformGizmo::Mode::Rotate);
        EXPECT_FLOAT_EQ(gizmo.settings().rotation_step_degrees, 15);
        EXPECT_EQ(history.undo_size(), 0);
        ImGui::ActivateItemByID(popup->GetID("Mode"));
        frame();
        frame();
        ASSERT_GE(GImGui->OpenPopupStack.Size, 2);
        options = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(options, nullptr);
        const int scale_index = 2;
        ImGui::ActivateItemByID(ImHashStr(
            "Scale", 0, ImHashData(&scale_index, sizeof(scale_index), options->ID)));
        frame();
        EXPECT_EQ(gizmo.settings().mode, TransformGizmo::Mode::Scale);
        EXPECT_FLOAT_EQ(gizmo.settings().scale_step, 0.1f);
        EXPECT_EQ(history.undo_size(), 0);
    }

    TEST_F(ViewportGizmoUiTest, RuntimeButtonsReadAuthoritativeStateAndEmitOneRequest) {
        auto* window = ImGui::FindWindowByName("Viewport");
        ASSERT_NE(window, nullptr);
        ImGui::ActivateItemByID(window->GetID("||##Pause"));
        frame();
        EXPECT_FALSE(viewport.take_runtime_command());
        state.mode = EditorMode::Play;
        runtime.start(scene);
        frame();
        ImGui::ActivateItemByID(window->GetID("|>##Step"));
        frame();
        EXPECT_FALSE(viewport.take_runtime_command());
        ImGui::ActivateItemByID(window->GetID("||##Pause"));
        frame();
        EXPECT_EQ(viewport.take_runtime_command(), ViewPanel::RuntimeCommand::Pause);
        EXPECT_FALSE(viewport.take_runtime_command());
        EXPECT_EQ(runtime.get_state(), Comet::SceneRuntime::State::Running);
        runtime.set_state(Comet::SceneRuntime::State::Paused);
        frame();
        ImGui::ActivateItemByID(window->GetID("|>##Step"));
        frame();
        EXPECT_EQ(viewport.take_runtime_command(), ViewPanel::RuntimeCommand::Step);
        EXPECT_FALSE(viewport.take_runtime_command());
        ImGui::ActivateItemByID(window->GetID(">##Resume"));
        frame();
        EXPECT_EQ(viewport.take_runtime_command(), ViewPanel::RuntimeCommand::Resume);
        runtime.stop();
        frame();
        ImGui::ActivateItemByID(window->GetID("|>##Step"));
        frame();
        EXPECT_FALSE(viewport.take_runtime_command());
        EXPECT_EQ(history.undo_size(), 0U);
    }

    TEST_F(ViewportGizmoUiTest, GameInputRequiresFocusedVisibleImageAndNoEditorCapture) {
        auto* window = ImGui::FindWindowByName("Viewport");
        ASSERT_NE(window, nullptr);
        const auto rect = viewport.get_layout().image_visible_rect;
        const auto center = rect.min + rect.size() * 0.5f;
        ImGui::FocusWindow(window);
        move_pointer(center);
        EXPECT_FALSE(viewport.accepts_game_input());
        state.mode = EditorMode::Play;
        runtime.start(scene);
        frame();
        EXPECT_TRUE(viewport.accepts_game_input());
        EXPECT_NE(ImGui::GetKeyOwner(ImGuiKey_MouseWheelX), ImGuiKeyOwner_NoOwner);
        EXPECT_NE(ImGui::GetKeyOwner(ImGuiKey_MouseWheelY), ImGuiKeyOwner_NoOwner);
        move_pointer(rect.min - Comet::Math::Vec2(0, 5));
        EXPECT_FALSE(viewport.accepts_game_input());
        move_pointer(center);
        EXPECT_TRUE(viewport.accepts_game_input());
        ImGui::GetIO().WantTextInput = true;
        EXPECT_FALSE(viewport.accepts_game_input());
        ImGui::GetIO().WantTextInput = false;
        ImGui::SetActiveID(window->GetID("Other widget"), window);
        EXPECT_FALSE(viewport.accepts_game_input());
        ImGui::ClearActiveID();
        ImGui::FocusWindow(nullptr);
        frame();
        EXPECT_FALSE(viewport.accepts_game_input());
        ImGui::FocusWindow(window);
        frame();
        EXPECT_TRUE(viewport.accepts_game_input());
        viewport.set_visible(false);
        frame();
        EXPECT_FALSE(viewport.accepts_game_input());
    }

    TEST_F(ViewportGizmoUiTest, PopupOpenedAfterViewportAlsoBlocksGameInput) {
        state.mode = EditorMode::Play;
        runtime.start(scene);
        auto* window = ImGui::FindWindowByName("Viewport");
        ImGui::FocusWindow(window);
        const auto rect = viewport.get_layout().image_visible_rect;
        move_pointer(rect.min + rect.size() * 0.5f);
        ASSERT_TRUE(viewport.accepts_game_input());
        input_popup = true;
        frame();
        EXPECT_FALSE(viewport.accepts_game_input());
    }

    TEST_F(ViewportGizmoUiTest, CenterScaleCapturesInputAndPreservesComponentRatios) {
        state.camera.perspective.position = {0, 0, 3};
        state.camera.target = {};
        auto& transform = entity.get_component<Comet::TransformComponent>();
        transform.scale = {1, -2, 3};
        ASSERT_TRUE(gizmo.set_settings(
            {.mode = TransformGizmo::Mode::Scale, .snap = true, .scale_step = 0.25f}));
        frame();
        const auto handle = gizmo.handles(
            entity.get_uuid(), state.camera.snapshot(), viewport.get_layout())[3];
        ASSERT_TRUE(handle);
        const auto point =
            (handle->segments.front().start + handle->segments.front().end) * 0.5f;
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
        ASSERT_TRUE(
            gizmo.set_settings({.mode = TransformGizmo::Mode::Rotate, .snap = true}));
        frame();
        const auto ring = gizmo.handles(
            entity.get_uuid(), state.camera.snapshot(), viewport.get_layout())[2];
        ASSERT_TRUE(ring);
        ASSERT_EQ(ring->segments.size(), 64);
        EXPECT_GT(gizmo_vertices, 0);
        move_pointer(ring->segments[4].start);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        ASSERT_EQ(gizmo.active_axis(), TransformGizmo::Axis::Z);
        move_pointer(ring->segments[12].start);
        EXPECT_NEAR(
            entity.get_component<Comet::TransformComponent>().rotation.z, 45, 0.001f);
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
        EXPECT_NEAR(
            entity.get_component<Comet::TransformComponent>().rotation.z, 90, 0.001f);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<Comet::TransformComponent>().rotation,
            Comet::Math::Vec3(0));
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
        mesh_payload.type = Comet::AssetType::Material;
        attempt(rect.min + rect.size() * 0.5f);
        mesh_payload.type = Comet::AssetType::Mesh;
        state.mode = EditorMode::Play;
        attempt(rect.min + rect.size() * 0.5f);
        EXPECT_EQ(scene.entity_count(), 1);
        EXPECT_EQ(history.undo_size(), 0);
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

    enum class CancelBoundary { Escape, Hidden, FocusLost, Play };

    class ViewportGizmoCancellationTest
        : public ViewportGizmoUiTest,
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
        io.AddKeyEvent(ImGuiKey_Escape, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_FALSE(gizmo.active());
        EXPECT_FALSE(history.can_undo());
    }

    INSTANTIATE_TEST_SUITE_P(InputBoundary, ViewportGizmoCancellationTest,
        ::testing::Values(CancelBoundary::Escape, CancelBoundary::Hidden,
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
        const auto point =
            viewport.get_layout().image_visible_rect.min + Comet::Math::Vec2(20, 20);
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
