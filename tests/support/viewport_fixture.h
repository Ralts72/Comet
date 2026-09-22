#pragma once
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
    class ViewportUiTest: public ::testing::Test {
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
            auto actions = Comet::InputActions::create(
                {{"camera.move_z", Comet::InputActions::Type::Axis, {{Comet::Input::Key::W, -1}}}});
            ASSERT_TRUE(actions);
            ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
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

}
