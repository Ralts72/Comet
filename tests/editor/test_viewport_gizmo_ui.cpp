#ifdef COMET_TEST_EDITOR_UI
#include "panels/view.h"
#include "selection.h"
#include "translation_gizmo.h"
#include "shortcuts.h"

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
        TranslationGizmo gizmo{history, components};
        SelectionService selection{scene};
        EditorState state;
        EditorShortcuts shortcuts;
        ViewPanel viewport{state, selection, gizmo, property_edit, 4096, shortcuts};
        int gizmo_vertices = 0;
        bool mesh_drag = false;
        AssetDragPayload mesh_payload{.handle = Comet::AssetHandle(42),
            .revision = 1,
            .generation = 0,
            .type = Comet::AssetType::Mesh};
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
            return (handle->start + handle->end) * 0.5f;
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
            EXPECT_EQ(gizmo.active_axis(), TranslationGizmo::Axis::X);
            EXPECT_FALSE(viewport.take_pick_request());
            move_pointer(point + Comet::Math::Vec2(20, 0));
            move_pointer(point + Comet::Math::Vec2(40, 0));
        }

        float x() {
            return entity.get_component<Comet::TransformComponent>().translation.x;
        }
    };

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
        shortcuts = EditorShortcuts::parse(
            "editor: {shortcuts: {viewport.focus_selection: [Primary+G]}}");
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
