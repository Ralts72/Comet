#ifdef COMET_TEST_EDITOR_UI
#include "scene/command_history.h"
#include "scene/scene_commands.h"
#include "ui/menu_bar.h"
#include "inspector/inspector.h"
#include "scene/hierarchy.h"
#include "scene/entity_reference.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "asset/registry.h"
#include "render/material/material_programs.h"
#include "asset/script.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"

#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>
#include <vector>

namespace CometEditor::Tests {
    constexpr const char* COLOR_SCRIPT_SOURCE =
        "return {properties = {score_color = {type = 'color', default = {2, 0.25, -0.5, 0.75}}}}";

    class EditingUiTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui;
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity();
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        Comet::AssetDatabase assets{Comet::ProjectPaths(COMET_SAMPLE_PROJECT_DIRECTORY)};
        Comet::AssetRegistry runtime_assets;
        Comet::MaterialProgramValidator programs{runtime_assets};
        CommandHistory history;
        PropertyEditTransaction edit{history, components};
        SelectionService selection{scene};
        PropertyEditorRegistry widgets;
        EditorState state;
        EditorShortcuts shortcuts;
        MenuBar menu{state, history, shortcuts};
        std::unique_ptr<InspectorPanel> inspector;
        SceneCommands::EntityClipboard clipboard;
        std::unique_ptr<HierarchyPanel> hierarchy;
        ImVec2 drag_point{};
        ImVec2 joint_anchor_button{};
        ImVec2 joint_axis_button{};
        ImVec2 joint_reference_button{};
        ImVec2 text_point{};
        float inspector_width = 700;
        float inspector_height = 500;
        bool draw_trailing_item = false;
        bool show_text_input = false;
        char text_buffer[32]{};
        std::optional<EntityDragPayload> dragged_entity;
        int payload_size = sizeof(EntityDragPayload);

        void SetUp() override {
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Vec3,
                [this, builtin = create_property_editor_registry(assets)](
                    const Comet::PropertyDescriptor& property, void* value) {
                    const auto result = builtin.edit_property(property, value);
                    if(property.id == "translation") {
                        const auto start = ImGui::GetItemRectMin();
                        drag_point = ImVec2(start.x + 20, start.y + 8);
                    }
                    if(property.id == "connected_anchor" || property.id == "connected_axis"
                        || property.id == "connected_reference_axis") {
                        const auto start = ImGui::GetItemRectMin();
                        const auto end = ImGui::GetItemRectMax();
                        const auto& style = ImGui::GetStyle();
                        const float width =
                            ImGui::CalcTextSize("对齐").x + style.FramePadding.x * 2;
                        const ImVec2 button{
                            end.x + style.ItemSpacing.x + width * 0.5f, (start.y + end.y) * 0.5f};
                        if(property.id == "connected_anchor")
                            joint_anchor_button = button;
                        else if(property.id == "connected_axis")
                            joint_axis_button = button;
                        else
                            joint_reference_button = button;
                    }
                    if(draw_trailing_item)
                        ImGui::TextUnformatted("Extra widget content");
                    return result;
                }));
            inspector = std::make_unique<InspectorPanel>(state, selection, history, edit,
                components, widgets, assets, runtime_assets, programs);
            frame();
            frame();
        }

        void TearDown() override { inspector.reset(); }
        void observe_runtime(const Comet::SceneRuntime& runtime) {
            inspector = std::make_unique<InspectorPanel>(state, selection, history, edit,
                components, widgets, assets, runtime_assets, programs, &runtime);
        }
        void frame() {
            ImGui::NewFrame();
            if(dragged_entity && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload(
                    EntityDragPayload::TYPE, &*dragged_entity, payload_size, ImGuiCond_Once);
                ImGui::EndDragDropSource();
            }
            menu.render();
            if(hierarchy) {
                ImGui::SetNextWindowPos(ImVec2(10, 40));
                ImGui::SetNextWindowSize(ImVec2(260, 500));
                hierarchy->render();
            }
            ImGui::SetNextWindowPos(ImVec2(hierarchy ? 300 : 20, 40));
            ImGui::SetNextWindowSize(ImVec2(inspector_width, inspector_height));
            inspector->render();
            if(show_text_input) {
                ImGui::Begin("Text input");
                ImGui::InputText("##Text", text_buffer, sizeof(text_buffer));
                const auto start = ImGui::GetItemRectMin();
                text_point = ImVec2(start.x + 20, start.y + 8);
                ImGui::End();
            }
            menu.collect_shortcuts();
            ImGui::Render();
        }
        float x() { return entity.get_component<Comet::TransformComponent>().translation.x; }
        void focus_text_input() {
            show_text_input = true;
            frame();
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(text_point.x, text_point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        }
        void drag_at(ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMousePosEvent(point.x + 30, point.y);
            frame();
            io.AddMousePosEvent(point.x + 60, point.y);
            frame();
        }

        void drag() { drag_at(drag_point); }

        void click(ImVec2 point, ImGuiMouseButton button = ImGuiMouseButton_Left) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(button, true);
            frame();
            io.AddMouseButtonEvent(button, false);
            frame();
        }

        void parameter_menu(ImVec2 point, const char* name) {
            click(point, ImGuiMouseButton_Right);
            auto* window = ImGui::FindWindowByName("Inspector");
            auto id = window->GetID("script");
            for(const char* part : {"parameters", name, "Parameter actions"})
                id = ImHashStr(part, 0, id);
            ASSERT_FALSE(GImGui->OpenPopupStack.empty());
            ASSERT_EQ(GImGui->OpenPopupStack.back().PopupId, id);
            frame();
            auto* popup = GImGui->OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID("Use script default"));
            frame();
        }

        void align_joint_anchor() { click(joint_anchor_button); }
        void align_joint_axis() { click(joint_axis_button); }

        ImVec2 entity_parameter_point() {
            auto* window = ImGui::FindWindowByName("Inspector");
            ImGuiID id = window->GetID("script");
            for(const char* part : {"parameters", "player", "player"})
                id = ImHashStr(part, 0, id);
            for(float y = window->WorkRect.Min.y; y < window->WorkRect.Max.y; y += 3) {
                const ImVec2 point{window->WorkRect.Min.x + 30, y};
                ImGui::GetIO().AddMousePosEvent(point.x, point.y);
                frame();
                if(ImGui::GetCurrentContext()->HoveredId == id)
                    return point;
            }
            ADD_FAILURE() << "Entity parameter widget not found";
            return {};
        }

        ImGuiID color_item_id(const char* item) {
            auto* window = ImGui::FindWindowByName("Inspector");
            ImGuiID id = window->GetID("script");
            for(const char* part : {"parameters", "score_color", "score_color", item})
                id = ImHashStr(part, 0, id);
            return id;
        }

        ImVec2 color_item_point(const char* item = "##X") {
            auto* window = ImGui::FindWindowByName("Inspector");
            const auto red_id = color_item_id("##X");
            const auto item_id = color_item_id(item);
            for(float y = window->WorkRect.Min.y; y < window->WorkRect.Max.y; y += 3) {
                ImGui::GetIO().AddMousePosEvent(window->WorkRect.Min.x + 20, y);
                frame();
                if(GImGui->HoveredId != red_id)
                    continue;
                for(float x = window->WorkRect.Min.x + 20; x < window->WorkRect.Max.x; x += 3) {
                    ImGui::GetIO().AddMousePosEvent(x, y);
                    frame();
                    if(GImGui->HoveredId == item_id)
                        return {x, y};
                }
                break;
            }
            ADD_FAILURE() << "Color parameter widget not found: " << item;
            return {};
        }

        void restore_parameters() {
            auto* window = ImGui::FindWindowByName("Inspector");
            auto id = ImHashStr("parameters", 0, window->GetID("script"));
            ImGui::ActivateItemByID(ImHashStr("Restore default parameters", 0, id));
            frame();
        }

        void remove_incompatible_parameters() {
            auto* window = ImGui::FindWindowByName("Inspector");
            auto id = ImHashStr("parameters", 0, window->GetID("script"));
            ImGui::ActivateItemByID(ImHashStr("Remove incompatible overrides", 0, id));
            frame();
        }

        void choose_entity(int row) {
            click(entity_parameter_point());
            frame();
            const auto* popup = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(popup, nullptr);
            ASSERT_TRUE(popup->Active);
            click({popup->DC.CursorStartPos.x + 20,
                popup->DC.CursorStartPos.y + row * ImGui::GetTextLineHeightWithSpacing()
                    + ImGui::GetTextLineHeight() * 0.5f});
        }

        void drop_entity(EntityDragPayload payload, int size = sizeof(EntityDragPayload)) {
            const auto point = entity_parameter_point();
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            dragged_entity = payload;
            payload_size = size;
            io.AddMouseButtonEvent(0, true);
            frame();
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            dragged_entity.reset();
            frame();
            frame();
        }

        void show_hierarchy() {
            inspector_width = 480;
            hierarchy = std::make_unique<HierarchyPanel>(selection, history, state, clipboard);
            frame();
            frame();
        }

        ImVec2 hierarchy_root_point(Comet::Entity root) {
            auto* window = ImGui::FindWindowByName("Hierarchy");
            const auto node_id =
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(root.get_id()));
            const auto id = ImHashData(&node_id, sizeof(node_id), window->GetID("Scene"));
            for(float y = window->WorkRect.Min.y; y < window->WorkRect.Max.y; y += 3) {
                const ImVec2 point{window->WorkRect.Min.x + 70, y};
                ImGui::GetIO().AddMousePosEvent(point.x, point.y);
                frame();
                if(GImGui->HoveredId == id)
                    return point;
            }
            ADD_FAILURE() << "Hierarchy root not found";
            return {};
        }

        void begin_entity_drag(ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            EXPECT_EQ(selection.get_selected_entity(), entity);
            io.AddMousePosEvent(point.x + 30, point.y);
            frame();
            ASSERT_TRUE(ImGui::IsDragDropActive());
            EXPECT_EQ(selection.get_selected_entity(), entity);
        }
    };

    TEST_F(EditingUiTest, HierarchySupportsToggleAndVisibleRangeSelection) {
        scene.create_entity("Second");
        scene.create_entity("Third");
        const auto roots = scene.get_root_entities();
        ASSERT_EQ(roots.size(), 3u);
        show_hierarchy();
        click(hierarchy_root_point(roots[0]));
        EXPECT_EQ(selection.get_selected_entities(), (std::vector{roots[0].get_uuid()}));
        auto& io = ImGui::GetIO();
        io.ConfigMacOSXBehaviors = false;
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        click(hierarchy_root_point(roots[2]));
        EXPECT_EQ(selection.get_selected_entities(),
            (std::vector{roots[0].get_uuid(), roots[2].get_uuid()}));
        click(hierarchy_root_point(roots[2]));
        EXPECT_EQ(selection.get_selected_entities(), (std::vector{roots[0].get_uuid()}));
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        click(hierarchy_root_point(roots[0]));
        io.AddKeyEvent(ImGuiMod_Shift, true);
        click(hierarchy_root_point(roots[2]));
        EXPECT_EQ(selection.get_selected_entities(),
            (std::vector{roots[0].get_uuid(), roots[1].get_uuid(), roots[2].get_uuid()}));
        EXPECT_EQ(selection.get_selected_entity(), roots[2]);
        // 保持起点，缩短连续选择范围。
        click(hierarchy_root_point(roots[1]));
        EXPECT_EQ(selection.get_selected_entities(),
            (std::vector{roots[0].get_uuid(), roots[1].get_uuid()}));
        io.AddKeyEvent(ImGuiMod_Shift, false);
        io.ConfigMacOSXBehaviors = true;
        io.AddKeyEvent(ImGuiMod_Super, true);
        click(hierarchy_root_point(roots[2]));
        EXPECT_TRUE(selection.is_selected(roots[2].get_id()));
        io.AddKeyEvent(ImGuiMod_Super, false);
        frame();
        click(hierarchy_root_point(roots[1]), ImGuiMouseButton_Right);
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Duplicate"));
        frame();
        const auto request = hierarchy->take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->entities, selection.get_selected_entities());
        EXPECT_EQ(request->entities.size(), 3u);
        selection.clear();
        EXPECT_EQ(request->entities.size(), 3u);
        EXPECT_EQ(scene.entity_count(), 3u);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, HierarchyDragKeepsInspectorTargetAndAssignsOneUndoableReference) {
        auto script = Comet::Script::create("return {properties = {player = {type = 'entity'}}}");
        ASSERT_TRUE(script);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        const auto player = scene.create_entity("Player");
        show_hierarchy();
        const auto destination = entity_parameter_point();
        const auto source = hierarchy_root_point(player);
        begin_entity_drag(source);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(destination.x, destination.y);
        frame();
        frame();
        EXPECT_TRUE(binding.parameters.empty());
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        EXPECT_EQ(selection.get_selected_entity(), entity);
        ASSERT_TRUE(binding.parameters.contains("player"));
        EXPECT_EQ(std::get<Comet::EntityUuid>(binding.parameters.at("player")), player.get_uuid());
        EXPECT_FALSE(hierarchy->take_request());
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_TRUE(binding.parameters.empty());
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(std::get<Comet::EntityUuid>(binding.parameters.at("player")), player.get_uuid());
        frame();
        click(source);
        EXPECT_EQ(selection.get_selected_entity(), player);
    }

    TEST_F(EditingUiTest, HierarchyDragCancelPreservesSelectionAndReparentStillQueuesRequest) {
        const auto player = scene.create_entity("Player");
        show_hierarchy();
        const auto parent = hierarchy_root_point(entity);
        const auto source = hierarchy_root_point(player);
        auto& io = ImGui::GetIO();
        for(const auto destination : {ImVec2{790, 590}, source}) {
            begin_entity_drag(source);
            io.AddMousePosEvent(destination.x, destination.y);
            frame();
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            EXPECT_EQ(selection.get_selected_entity(), entity);
            EXPECT_FALSE(hierarchy->take_request());
            EXPECT_FALSE(history.can_undo());
            frame();
        }
        begin_entity_drag(source);
        io.AddMousePosEvent(parent.x, parent.y);
        frame();
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        EXPECT_EQ(selection.get_selected_entity(), entity);
        const auto request = hierarchy->take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Reparent);
        EXPECT_EQ(request->entity, player.get_uuid());
        EXPECT_EQ(request->parent, entity.get_uuid());
        EXPECT_FALSE(scene.get_parent(player));
    }

    TEST_F(EditingUiTest, JointAlignButtonsFitNarrowInspectorAndUseLeftClick) {
        entity.add_component<Comet::HingeJointComponent>(
            Comet::HingeJointComponent{.anchor = {1, 2, 3}, .axis = {1, 1, 0}});
        for(const float width : {700.0f, 480.0f, 320.0f}) {
            inspector_width = width;
            frame();
            frame();
            const auto* window = ImGui::FindWindowByName("Inspector");
            ASSERT_NE(window, nullptr);
            for(const auto button : {joint_anchor_button, joint_axis_button}) {
                EXPECT_GT(button.x, window->WorkRect.Min.x);
                EXPECT_LT(button.x, window->WorkRect.Max.x);
            }
            ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
            EXPECT_EQ(entity.get_component<Comet::HingeJointComponent>().connected_anchor,
                Comet::Math::Vec3(1, 2, 3));
            ASSERT_NO_FATAL_FAILURE(align_joint_axis());
            EXPECT_LT(Comet::Math::length(
                          entity.get_component<Comet::HingeJointComponent>().connected_axis
                          - Comet::Math::normalize(Comet::Math::Vec3(1, 1, 0))),
                1e-5f);
            EXPECT_TRUE(GImGui->OpenPopupStack.empty());
        }
        EXPECT_EQ(history.undo_size(), 2u);
    }

    TEST_F(EditingUiTest, JointAnchorAlignmentConvertsBetweenBodiesAndIsUndoable) {
        entity.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {2, 1, 0.5f};
        });
        auto target = scene.create_entity();
        target.edit_transform([](auto& value) {
            value.translation = {5, 6, 7};
            value.rotation.z = 90;
            value.scale = {2, 3, 4};
        });
        const Comet::PointJointComponent joint{.connected_body = target.get_uuid(),
            .anchor = {1, 0, 0},
            .connected_anchor = {9, 8, 7}};
        entity.add_component<Comet::PointJointComponent>(joint);
        frame();
        ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
        const auto aligned = entity.get_component<Comet::PointJointComponent>();
        EXPECT_LT(
            Comet::Math::length(aligned.connected_anchor - Comet::Math::Vec3(-0.5f, 1, -0.75f)),
            1e-5f);
        EXPECT_EQ(aligned.connected_body, target.get_uuid());
        EXPECT_EQ(aligned.anchor, joint.anchor);
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_FALSE(edit.active());
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<Comet::PointJointComponent>(), joint);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(entity.get_component<Comet::PointJointComponent>(), aligned);
        ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
        EXPECT_EQ(history.undo_size(), 1u);

        target.edit_transform([](auto& value) { value.scale.y = 0; });
        ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
        EXPECT_EQ(entity.get_component<Comet::PointJointComponent>(), aligned);
        EXPECT_EQ(history.undo_size(), 1u);
        scene.destroy_entity(target);
        ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
        EXPECT_EQ(entity.get_component<Comet::PointJointComponent>(), aligned);
        EXPECT_EQ(history.undo_size(), 1u);
    }

    TEST_F(EditingUiTest, HingeAlignmentUsesUnscaledAxesSupportsUndoAndPlayEditing) {
        using namespace Comet;
        entity.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {2, 1, 0.5f};
        });
        auto target = scene.create_entity();
        target.edit_transform([](auto& value) {
            value.translation = {5, 6, 7};
            value.scale = {2, 3, 4};
        });
        const HingeJointComponent joint{
            .connected_body = target.get_uuid(), .anchor = {1, 0, 0}, .axis = {1, 1, 0}};
        entity.add_component<HingeJointComponent>(joint);
        frame();
        ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
        EXPECT_LT(Math::length(entity.get_component<HingeJointComponent>().connected_anchor
                               - Math::Vec3(-1.5f, -1.0f / 3, -0.75f)),
            1e-5f);
        ASSERT_NO_FATAL_FAILURE(align_joint_axis());
        const auto expected = Math::normalize(Math::Vec3(-1, 1, 0));
        EXPECT_LT(
            Math::length(entity.get_component<HingeJointComponent>().connected_axis - expected),
            1e-5f);
        EXPECT_EQ(history.undo_size(), 2u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<HingeJointComponent>().connected_axis, joint.connected_axis);
        ASSERT_TRUE(history.redo());
        ASSERT_NO_FATAL_FAILURE(align_joint_axis());
        EXPECT_EQ(history.undo_size(), 2u);
        scene.destroy_entity(target);
        ASSERT_NO_FATAL_FAILURE(align_joint_axis());
        EXPECT_EQ(history.undo_size(), 2u);
        entity.edit_component<HingeJointComponent>([](auto& value) {
            value.connected_body = {};
            value.connected_axis = {0, 0, 1};
        });
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        ASSERT_NO_FATAL_FAILURE(align_joint_axis());
        EXPECT_LT(
            Math::length(entity.get_component<HingeJointComponent>().connected_axis - expected),
            1e-5f);
        EXPECT_FALSE(history.can_undo());
        entity.edit_component<HingeJointComponent>([](auto& value) { value.axis = {}; });
        ASSERT_NO_FATAL_FAILURE(align_joint_axis());
        EXPECT_LT(
            Math::length(entity.get_component<HingeJointComponent>().connected_axis - expected),
            1e-5f);
    }

    TEST_F(EditingUiTest, HingeControlsShowEnabledParametersAndReferenceAlignmentUsesButtons) {
        using namespace Comet;
        ImGui::GetIO().DisplaySize.y = 1000;
        inspector_height = 900;
        auto builtin = create_property_editor_registry(assets);
        ImVec2 limits_checkbox{}, motor_checkbox{};
        bool saw_motor_speed = false;
        for(const auto type : {PropertyType::Bool, PropertyType::Float})
            ASSERT_TRUE(
                widgets.register_editor(type, [&](const PropertyDescriptor& property, void* value) {
                    const auto result = builtin.edit_property(property, value);
                    const auto start = ImGui::GetItemRectMin();
                    if(property.id == "limits_enabled")
                        limits_checkbox = {start.x + 8, start.y + 8};
                    if(property.id == "motor_enabled")
                        motor_checkbox = {start.x + 8, start.y + 8};
                    saw_motor_speed |= property.id == "motor_speed";
                    return result;
                }));
        entity.edit_transform([](auto& value) {
            value.rotation.z = 90;
            value.scale = {2, 3, 4};
        });
        auto target = scene.create_entity();
        target.edit_transform([](auto& value) {
            value.rotation.z = 30;
            value.scale = {3, 4, 5};
        });
        entity.add_component<HingeJointComponent>(
            HingeJointComponent{.connected_body = target.get_uuid(),
                .reference_axis = {1, 0, 2},
                .connected_reference_axis = {0, 0, 1}});
        frame();
        EXPECT_FLOAT_EQ(joint_reference_button.x, 0);
        EXPECT_FALSE(saw_motor_speed);
        click(limits_checkbox);
        EXPECT_TRUE(entity.get_component<HingeJointComponent>().limits_enabled);
        EXPECT_GT(joint_reference_button.x, 0);
        click(joint_reference_button);
        const auto expected = Math::Vec3(0.5f, std::sqrt(3.0f) * 0.5f, 0);
        EXPECT_LT(Math::length(entity.get_component<HingeJointComponent>().connected_reference_axis
                               - expected),
            1e-5f);
        click(motor_checkbox);
        EXPECT_TRUE(entity.get_component<HingeJointComponent>().motor_enabled);
        EXPECT_TRUE(saw_motor_speed);
        EXPECT_EQ(history.undo_size(), 3u);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(entity.get_component<HingeJointComponent>().motor_enabled);
        saw_motor_speed = false;
        frame();
        EXPECT_FALSE(saw_motor_speed);
        ASSERT_TRUE(history.redo());
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        entity.edit_component<HingeJointComponent>(
            [](auto& joint) { joint.connected_reference_axis = {1, 0, 0}; });
        click(joint_reference_button);
        EXPECT_LT(Math::length(entity.get_component<HingeJointComponent>().connected_reference_axis
                               - expected),
            1e-5f);
        EXPECT_FALSE(history.can_undo());
        entity.edit_component<HingeJointComponent>(
            [](auto& joint) { joint.reference_axis = joint.axis; });
        click(joint_reference_button);
        EXPECT_LT(Math::length(entity.get_component<HingeJointComponent>().connected_reference_axis
                               - expected),
            1e-5f);
    }

    TEST_F(EditingUiTest, SliderControlsAlignFramesAndShowOnlyEnabledParameters) {
        using namespace Comet;
        ImGui::GetIO().DisplaySize.y = 1000;
        inspector_height = 900;
        auto builtin = create_property_editor_registry(assets);
        ImVec2 limits_checkbox{}, motor_checkbox{};
        bool saw_distance = false, saw_speed = false;
        for(const auto type : {PropertyType::Bool, PropertyType::Float})
            ASSERT_TRUE(
                widgets.register_editor(type, [&](const PropertyDescriptor& property, void* value) {
                    const auto result = builtin.edit_property(property, value);
                    const auto start = ImGui::GetItemRectMin();
                    if(property.id == "limits_enabled")
                        limits_checkbox = {start.x + 8, start.y + 8};
                    if(property.id == "motor_enabled")
                        motor_checkbox = {start.x + 8, start.y + 8};
                    saw_distance |= property.id == "min_distance";
                    saw_speed |= property.id == "motor_speed";
                    return result;
                }));
        entity.edit_transform([](auto& value) {
            value.rotation.z = 90;
            value.scale = {2, 3, 4};
        });
        auto target = scene.create_entity();
        target.edit_transform([](auto& value) { value.scale = {3, 4, 5}; });
        entity.add_component<SliderJointComponent>(
            SliderJointComponent{.connected_body = target.get_uuid(), .anchor = {0.5f, 0, 0}});
        for(const float width : {700.0f, 480.0f, 320.0f}) {
            inspector_width = width;
            frame();
            frame();
            const auto* window = ImGui::FindWindowByName("Inspector");
            ASSERT_NE(window, nullptr);
            for(const auto button :
                {joint_anchor_button, joint_axis_button, joint_reference_button})
                EXPECT_LT(button.x, window->WorkRect.Max.x);
            click(joint_anchor_button);
            click(joint_axis_button);
            click(joint_reference_button);
            const auto& joint = entity.get_component<SliderJointComponent>();
            EXPECT_LT(Math::length(joint.connected_anchor - Math::Vec3(0, 0.25f, 0)), 1e-5f);
            EXPECT_LT(Math::length(joint.connected_axis - Math::Vec3(-1, 0, 0)), 1e-5f);
            EXPECT_LT(Math::length(joint.connected_reference_axis - Math::Vec3(0, 1, 0)), 1e-5f);
        }
        EXPECT_FALSE(saw_distance);
        EXPECT_FALSE(saw_speed);
        EXPECT_EQ(history.undo_size(), 3u);
        click(limits_checkbox);
        EXPECT_TRUE(entity.get_component<SliderJointComponent>().limits_enabled);
        EXPECT_TRUE(saw_distance);
        click(motor_checkbox);
        EXPECT_TRUE(entity.get_component<SliderJointComponent>().motor_enabled);
        EXPECT_TRUE(saw_speed);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(entity.get_component<SliderJointComponent>().motor_enabled);
        ASSERT_TRUE(history.redo());
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        entity.edit_component<SliderJointComponent>(
            [](auto& joint) { joint.connected_anchor = {}; });
        frame();
        click(joint_anchor_button);
        EXPECT_LT(Math::length(entity.get_component<SliderJointComponent>().connected_anchor
                               - Math::Vec3(0, 0.25f, 0)),
            1e-5f);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, JointBreakThresholdsUseUndoBoundsAndPlayEditing) {
        using namespace Comet;
        ImGui::GetIO().MouseDoubleClickTime = 0.001f;
        auto builtin = create_property_editor_registry(assets);
        ImVec2 force_point{};
        bool saw_torque = false;
        ASSERT_TRUE(widgets.register_editor(
            PropertyType::Float, [&](const PropertyDescriptor& property, void* value) {
                const auto result = builtin.edit_property(property, value);
                if(property.id == "break_force") {
                    const auto start = ImGui::GetItemRectMin();
                    force_point = {start.x + 20, start.y + 8};
                }
                saw_torque |= property.id == "break_torque";
                return result;
            }));
        for(const auto id : {"point_joint", "hinge_joint", "slider_joint"}) {
            SCOPED_TRACE(id);
            state.mode = EditorMode::Edit;
            history.bind_scene(&scene);
            const auto* descriptor = components.find_component(id);
            ASSERT_NE(descriptor, nullptr);
            ASSERT_TRUE(descriptor->add_component(entity));
            const auto* force = descriptor->find_property("break_force");
            ASSERT_NE(force, nullptr);
            const auto value = [&] {
                return std::get<float>(*force->copy_value(descriptor->get_component(entity)));
            };
            saw_torque = false;
            frame();
            EXPECT_EQ(saw_torque, std::string_view(id) != "point_joint");
            EXPECT_FALSE(edit.apply({entity.get_uuid(), id, "break_force"}, -1.0f));
            drag_at(force_point);
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            EXPECT_GT(value(), 0);
            EXPECT_EQ(history.undo_size(), 1u);
            ASSERT_TRUE(history.undo());
            EXPECT_FLOAT_EQ(value(), 0);
            ASSERT_TRUE(history.redo());
            const auto configured = value();
            history.bind_scene(nullptr);
            state.mode = EditorMode::Play;
            frame();
            drag_at(force_point);
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            EXPECT_GT(value(), configured);
            EXPECT_FALSE(history.can_undo());
            ASSERT_TRUE(descriptor->remove_component(entity));
        }
    }

    TEST_F(EditingUiTest, JointModesShowRelevantControlsAndUseUndoAndPlayEditing) {
        using namespace Comet;
        ImGui::GetIO().DisplaySize.y = 1000;
        inspector_height = 900;
        auto builtin = create_property_editor_registry(assets);
        std::vector<std::string> visible;
        ImVec2 mode_point{};
        for(const auto type : {PropertyType::Bool, PropertyType::Float, PropertyType::Enum})
            ASSERT_TRUE(
                widgets.register_editor(type, [&](const PropertyDescriptor& property, void* value) {
                    visible.push_back(property.id);
                    const auto result = builtin.edit_property(property, value);
                    if(property.id == "motor_mode") {
                        const auto start = ImGui::GetItemRectMin();
                        mode_point = {start.x + 20, start.y + 8};
                    }
                    return result;
                }));
        for(const auto id : {"hinge_joint", "slider_joint"}) {
            SCOPED_TRACE(id);
            history.clear();
            state.mode = EditorMode::Edit;
            history.bind_scene(&scene);
            const auto* descriptor = components.find_component(id);
            ASSERT_NE(descriptor, nullptr);
            ASSERT_TRUE(descriptor->add_component(entity));
            ASSERT_TRUE(edit.apply({entity.get_uuid(), id, "motor_enabled"}, true));
            visible.clear();
            frame();
            const auto shown = [&](std::string_view property) {
                return std::find(visible.begin(), visible.end(), property) != visible.end();
            };
            EXPECT_TRUE(shown("motor_speed"));
            EXPECT_FALSE(shown("motor_target"));
            EXPECT_FALSE(shown("motor_frequency"));
            EXPECT_FALSE(shown("limits_frequency"));
            joint_reference_button = {};
            click(mode_point);
            frame();
            const auto* popup = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(popup, nullptr);
            ASSERT_TRUE(popup->Active);
            click({popup->DC.CursorStartPos.x + 20, popup->DC.CursorStartPos.y
                                                        + ImGui::GetTextLineHeightWithSpacing()
                                                        + ImGui::GetTextLineHeight() * 0.5f});
            visible.clear();
            frame();
            EXPECT_FALSE(shown("motor_speed"));
            EXPECT_TRUE(shown("motor_target"));
            EXPECT_TRUE(shown("motor_frequency"));
            EXPECT_TRUE(shown("motor_damping"));
            EXPECT_GT(joint_reference_button.x, 0);
            EXPECT_EQ(history.undo_size(), 2u);
            ASSERT_TRUE(history.undo());
            visible.clear();
            frame();
            EXPECT_TRUE(shown("motor_speed"));
            EXPECT_FALSE(shown("motor_target"));
            ASSERT_TRUE(history.redo());
            ASSERT_TRUE(edit.apply({entity.get_uuid(), id, "limits_enabled"}, true));
            visible.clear();
            frame();
            EXPECT_TRUE(shown("limits_frequency"));
            EXPECT_FALSE(shown("limits_damping"));
            ASSERT_TRUE(edit.apply({entity.get_uuid(), id, "limits_frequency"}, 2.0f));
            visible.clear();
            frame();
            EXPECT_TRUE(shown("limits_damping"));
            history.bind_scene(nullptr);
            state.mode = EditorMode::Play;
            click(mode_point);
            frame();
            popup = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(popup, nullptr);
            ASSERT_TRUE(popup->Active);
            click({popup->DC.CursorStartPos.x + 20,
                popup->DC.CursorStartPos.y + ImGui::GetTextLineHeight() * 0.5f});
            visible.clear();
            frame();
            EXPECT_TRUE(shown("motor_speed"));
            EXPECT_FALSE(shown("motor_target"));
            EXPECT_FALSE(history.can_undo());
            ASSERT_TRUE(descriptor->remove_component(entity));
        }
    }

    TEST_F(EditingUiTest, WorldAnchorAlignmentUsesSimulationPoseInPlayAndDoesNotWriteHistory) {
        class Move final: public Comet::System {
        public:
            Comet::Result<void, Comet::Error> fixed_update(
                Comet::Scene& scene, const Context&) override {
                scene.each<const Comet::PointJointComponent>(
                    [](Comet::Entity entity, const Comet::PointJointComponent&) {
                        entity.edit_transform([](auto& value) { value.translation.x += 10; });
                    });
                return Comet::Result<void, Comet::Error>::success();
            }
        };
        entity.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {2, 1, 0.5f};
        });
        entity.add_component<Comet::PointJointComponent>(
            Comet::PointJointComponent{.anchor = {1, 0, 0}});
        frame();
        ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
        EXPECT_LT(
            Comet::Math::length(entity.get_component<Comet::PointJointComponent>().connected_anchor
                                - Comet::Math::Vec3(2, 5, 4)),
            1e-5f);
        ASSERT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<Comet::PointJointComponent>().connected_anchor,
            Comet::Math::Vec3(0));
        ASSERT_TRUE(history.redo());

        Comet::SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Move>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        EXPECT_NEAR(scene.get_render_transform(entity).world_matrix[3].x, 7, 1e-5f);
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
        EXPECT_LT(
            Comet::Math::length(entity.get_component<Comet::PointJointComponent>().connected_anchor
                                - Comet::Math::Vec3(12, 5, 4)),
            1e-5f);
        EXPECT_FALSE(history.can_undo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::TransformComponent>().translation.x, 12);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(EditingUiTest, PointJointBodySelectionUsesSharedReferenceWidgetAndPlayEditing) {
        entity.add_component<Comet::PointJointComponent>();
        entity.get_component<Comet::NameComponent>().name = "ZOwner";
        const auto target = scene.create_entity("ATarget").get_uuid();
        const auto choose = [&](int row) {
            auto* window = ImGui::FindWindowByName("Inspector");
            auto id = ImHashStr("connected_body", 0, window->GetID("point_joint"));
            ImGui::ActivateItemByID(ImHashStr("Connected Body", 0, id));
            frame();
            frame();
            const auto* popup = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(popup, nullptr);
            ASSERT_TRUE(popup->Active);
            click({popup->DC.CursorStartPos.x + 20,
                popup->DC.CursorStartPos.y + row * ImGui::GetTextLineHeightWithSpacing()
                    + ImGui::GetTextLineHeight() * 0.5f});
        };
        frame();
        ASSERT_NO_FATAL_FAILURE(choose(1));
        EXPECT_EQ(entity.get_component<Comet::PointJointComponent>().connected_body, target);
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_FALSE(edit.active());
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(entity.get_component<Comet::PointJointComponent>().connected_body);
        ASSERT_TRUE(history.redo());
        ASSERT_NO_FATAL_FAILURE(choose(0));
        EXPECT_FALSE(entity.get_component<Comet::PointJointComponent>().connected_body);
        ASSERT_TRUE(history.undo());

        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        ASSERT_NO_FATAL_FAILURE(choose(0));
        EXPECT_FALSE(entity.get_component<Comet::PointJointComponent>().connected_body);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, EntityParameterSelectionUsesHistoryAndSurvivesRenameAndMissingTarget) {
        auto script = Comet::Script::create("return {properties = {player = {type = 'entity'}}}");
        ASSERT_TRUE(script);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        entity.get_component<Comet::NameComponent>().name = "ZOwner";
        auto target = scene.create_entity("ATarget");
        const auto target_id = target.get_uuid();
        frame();
        choose_entity(1);
        ASSERT_TRUE(binding.parameters.contains("player"));
        EXPECT_EQ(std::get<Comet::EntityUuid>(binding.parameters.at("player")), target_id);
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_FALSE(edit.active());
        ASSERT_TRUE(history.undo());
        EXPECT_TRUE(binding.parameters.empty());
        ASSERT_TRUE(history.redo());
        target.get_component<Comet::NameComponent>().name = "Renamed";
        scene.destroy_entity(target);
        frame();
        EXPECT_EQ(std::get<Comet::EntityUuid>(binding.parameters.at("player")), target_id);
        EXPECT_EQ(history.undo_size(), 1u);
        choose_entity(0);
        EXPECT_FALSE(std::get<Comet::EntityUuid>(binding.parameters.at("player")));
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(std::get<Comet::EntityUuid>(binding.parameters.at("player")), target_id);
        ASSERT_NO_FATAL_FAILURE(parameter_menu(entity_parameter_point(), "player"));
        EXPECT_FALSE(binding.parameters.contains("player"));
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(std::get<Comet::EntityUuid>(binding.parameters.at("player")), target_id);
    }

    TEST_F(EditingUiTest, EntityParameterDropsRejectStaleAndMalformedPayloadsAndPlayHistory) {
        auto script = Comet::Script::create("return {properties = {player = {type = 'entity'}}}");
        ASSERT_TRUE(script);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        entity.get_component<Comet::NameComponent>().name = "ZOwner";
        const auto target = scene.create_entity("ATarget").get_uuid();
        frame();
        drop_entity({target, history.generation() + 1});
        drop_entity({Comet::EntityUuid::generate(), history.generation()});
        drop_entity({target, history.generation()}, sizeof(EntityDragPayload) - 1);
        EXPECT_TRUE(binding.parameters.empty());
        EXPECT_FALSE(history.can_undo());
        drop_entity({target, history.generation()});
        ASSERT_TRUE(binding.parameters.contains("player"));
        EXPECT_EQ(std::get<Comet::EntityUuid>(binding.parameters.at("player")), target);
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());

        Comet::SceneRuntime runtime;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime_assets);
        ASSERT_TRUE(runtime.add_system(std::move(candidate_system)));
        observe_runtime(runtime);
        ASSERT_TRUE(runtime.start(scene));
        state.mode = EditorMode::Play;
        drop_entity({target, history.generation()});
        EXPECT_TRUE(binding.parameters.empty());
        choose_entity(1);
        ASSERT_TRUE(binding.parameters.contains("player"));
        EXPECT_EQ(std::get<Comet::EntityUuid>(binding.parameters.at("player")), target);
        EXPECT_FALSE(history.can_undo());
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(EditingUiTest, ScriptDefaultsStayImplicitUntilAnUndoableParameterEdit) {
        auto script = Comet::Script::create("return {properties = {speed = 100, enabled = true}}");
        ASSERT_TRUE(script);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        bool change = false;
        bool rendered = false;
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::Float, [&](const Comet::PropertyDescriptor&, void* value) {
                rendered = true;
                auto& speed = *static_cast<float*>(value);
                EXPECT_EQ(speed, 100);
                if(!change)
                    return PropertyEditResult{};
                speed = 50.0f;
                change = false;
                return PropertyEditResult{.changed = true, .finished = true};
            }));
        frame();
        EXPECT_TRUE(rendered);
        EXPECT_TRUE(binding.parameters.empty());
        change = true;
        frame();
        EXPECT_EQ(std::get<float>(binding.parameters.at("speed")), 50);
        EXPECT_EQ(binding.parameters.size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_TRUE(binding.parameters.empty());
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(std::get<float>(binding.parameters.at("speed")), 50);
        const auto updated =
            Comet::Script::create("return {properties = {speed = 200, enabled = false}}");
        ASSERT_TRUE(updated);
        const auto effective = updated.value()->resolve_parameters(binding.parameters);
        ASSERT_TRUE(effective);
        EXPECT_EQ(std::get<float>(effective.value().at("speed")), 50);
        EXPECT_FALSE(std::get<bool>(effective.value().at("enabled")));
    }

    TEST_F(
        EditingUiTest, RemovingIncompatibleScriptOverridesPreservesCompatibleValuesAndIsUndoable) {
        auto original = Comet::Script::create(R"(return {properties = {
            speed = 100, removed = true, changed = false,
            player = {type = 'entity'},
            score_color = {type = 'color', default = {1, 1, 1, 1}}
        }})");
        ASSERT_TRUE(original);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, original.value()));
        const auto player = scene.create_entity("Player").get_uuid();
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        const Comet::ParameterMap authored{{"speed", 250.0f}, {"removed", false}, {"changed", true},
            {"player", player}, {"score_color", Comet::Math::Vec4(2, 0.25f, -0.5f, 0.75f)}};
        ASSERT_TRUE(edit.apply({entity.get_uuid(), "script", "parameters"}, authored));
        frame();
        const auto before = history.state_id();
        const auto undo_count = history.undo_size();

        auto changed = Comet::Script::create(R"(return {properties = {
            speed = 200, changed = 'new type', player = {type = 'entity'},
            score_color = {0, 0, 0, 1}
        }})");
        ASSERT_TRUE(changed);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, changed.value()));
        frame();
        EXPECT_EQ(binding.parameters, authored);
        EXPECT_EQ(history.state_id(), before);
        EXPECT_FALSE(changed.value()->validate_overrides(binding.parameters));

        remove_incompatible_parameters();
        auto retained = authored;
        retained.erase("removed");
        retained.erase("changed");
        ASSERT_EQ(binding.parameters, retained);
        EXPECT_TRUE(changed.value()->validate_overrides(binding.parameters));
        EXPECT_EQ(history.undo_size(), undo_count + 1);
        EXPECT_FALSE(edit.active());
        const auto effective = changed.value()->resolve_parameters(binding.parameters);
        ASSERT_TRUE(effective);
        EXPECT_EQ(std::get<std::string>(effective.value().at("changed")), "new type");
        const auto repaired = history.state_id();
        remove_incompatible_parameters();
        frame();
        EXPECT_EQ(binding.parameters, retained);
        EXPECT_EQ(history.state_id(), repaired);

        ASSERT_TRUE(history.undo());
        frame();
        EXPECT_EQ(binding.parameters, authored);
        EXPECT_EQ(history.state_id(), before);
        ASSERT_TRUE(history.redo());
        frame();
        EXPECT_EQ(binding.parameters, retained);
        EXPECT_EQ(history.undo_size(), undo_count + 1);
    }

    TEST_F(EditingUiTest, OneParameterCanResumeDefaultInheritanceWithoutResettingItsPeers) {
        const Comet::AssetHandle handle{1234};
        auto script = Comet::Script::create("return {properties = {speed = 100, enabled = true}}");
        ASSERT_TRUE(script);
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        binding.parameters = {{"speed", 12.0f}, {"enabled", false}};
        const auto before = binding.parameters;
        ImVec2 label_point;
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Float,
            [&, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                const auto result = builtin.edit_property(property, value);
                const auto maximum = ImGui::GetItemRectMax();
                label_point = {maximum.x - 2, maximum.y - ImGui::GetTextLineHeight() * 0.5f};
                return result;
            }));
        frame();
        ASSERT_NO_FATAL_FAILURE(parameter_menu(label_point, "speed"));
        EXPECT_EQ(binding.parameters, Comet::ParameterMap({{"enabled", false}}));
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(binding.parameters, before);
        ASSERT_TRUE(history.redo());
        EXPECT_FALSE(binding.parameters.contains("speed"));

        auto changed = Comet::Script::create("return {properties = {speed = 250, enabled = true}}");
        ASSERT_TRUE(changed);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, changed.value()));
        frame();
        auto effective = changed.value()->resolve_parameters(binding.parameters);
        ASSERT_TRUE(effective);
        EXPECT_FLOAT_EQ(std::get<float>(effective.value().at("speed")), 250);
        EXPECT_FALSE(std::get<bool>(effective.value().at("enabled")));
        const auto history_state = history.state_id();
        ASSERT_NO_FATAL_FAILURE(parameter_menu(label_point, "speed"));
        EXPECT_EQ(history.state_id(), history_state);
        EXPECT_FALSE(binding.parameters.contains("speed"));
    }

    TEST_F(EditingUiTest, PlayParameterDefaultUsesActiveDefinitionWithoutEditingTheDocument) {
        const Comet::AssetHandle handle{1234};
        auto script = Comet::Script::create("return {properties = {speed = 100, enabled = true}}");
        ASSERT_TRUE(script);
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& authored = entity.add_component<Comet::ScriptComponent>();
        authored.asset = handle;
        authored.parameters = {{"speed", 12.0f}, {"enabled", false}};
        auto cloned = Comet::SceneSerializer(components).clone(scene);
        ASSERT_TRUE(cloned);
        auto runtime_entity = cloned.value()->find_entity(entity.get_uuid());
        auto& binding = runtime_entity.get_component<Comet::ScriptComponent>();
        Comet::SceneRuntime runtime;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime_assets);
        ASSERT_TRUE(runtime.add_system(std::move(candidate_system)));
        observe_runtime(runtime);
        ASSERT_TRUE(runtime.start(*cloned.value(), Comet::SceneRuntime::State::Paused));
        state.mode = EditorMode::Play;
        selection.set_scene(*cloned.value());
        selection.select_entity(runtime_entity.get_id());
        ImVec2 label_point;
        float displayed = 0;
        int parameter_draws = 0;
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Float,
            [&, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                if(property.id == "speed") {
                    displayed = *static_cast<float*>(value);
                    ++parameter_draws;
                }
                const auto result = builtin.edit_property(property, value);
                const auto maximum = ImGui::GetItemRectMax();
                label_point = {maximum.x - 2, maximum.y - ImGui::GetTextLineHeight() * 0.5f};
                return result;
            }));
        auto candidate =
            Comet::Script::create("return {properties = {speed = 250, enabled = true}}");
        ASSERT_TRUE(candidate);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, candidate.value()));
        const auto history_state = history.state_id();
        frame();
        ASSERT_NO_FATAL_FAILURE(parameter_menu(label_point, "speed"));
        frame();
        EXPECT_FLOAT_EQ(displayed, 100);
        EXPECT_EQ(binding.parameters, Comet::ParameterMap({{"enabled", false}}));
        EXPECT_EQ(runtime.find_system<Comet::ScriptSystem>()->running_script(runtime_entity),
            script.value());
        EXPECT_FLOAT_EQ(std::get<float>(authored.parameters.at("speed")), 12);
        EXPECT_EQ(history.state_id(), history_state);
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(edit.active());
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        frame();
        EXPECT_FLOAT_EQ(displayed, 250);
        EXPECT_EQ(runtime.find_system<Comet::ScriptSystem>()->running_script(runtime_entity),
            candidate.value());
        EXPECT_FALSE(binding.parameters.contains("speed"));
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.clear_systems());
        parameter_draws = 0;
        frame();
        EXPECT_EQ(parameter_draws, 0);
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::ScriptSystem>(runtime_assets)));
        ASSERT_TRUE(runtime.start(*cloned.value(), Comet::SceneRuntime::State::Paused));
        frame();
        EXPECT_GT(parameter_draws, 0);
        EXPECT_FLOAT_EQ(displayed, 250);
        EXPECT_EQ(history.state_id(), history_state);
        ASSERT_TRUE(runtime.stop());
        selection.set_scene(scene);
        selection.select_entity(entity.get_id());
        state.mode = EditorMode::Edit;
    }

    TEST_F(EditingUiTest, ScriptDefinitionChangeCancelsPendingParameterGesture) {
        const Comet::AssetHandle handle{1234};
        auto original = Comet::Script::create("return {properties = {speed = 100}}");
        ASSERT_TRUE(original);
        ASSERT_TRUE(runtime_assets.register_asset(handle, original.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::Float, [](const Comet::PropertyDescriptor&, void* value) {
                *static_cast<float*>(value) = 25;
                return PropertyEditResult{.changed = true, .active = true, .began = true};
            }));
        const auto before = history.state_id();
        frame();
        EXPECT_TRUE(edit.active());
        EXPECT_EQ(std::get<float>(binding.parameters.at("speed")), 25);
        auto changed = Comet::Script::create("return {properties = {speed = 'new type'}}");
        ASSERT_TRUE(changed);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, changed.value()));
        frame();
        EXPECT_FALSE(edit.active());
        EXPECT_TRUE(binding.parameters.empty());
        EXPECT_EQ(history.state_id(), before);
        remove_incompatible_parameters();
        EXPECT_TRUE(binding.parameters.empty());
        EXPECT_EQ(history.state_id(), before);
    }

    TEST_F(EditingUiTest, ScriptColorPickerUsesExportedDefaultAndRecordsOneUndoableGesture) {
        auto script = Comet::Script::create(COLOR_SCRIPT_SOURCE);
        ASSERT_TRUE(script);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        frame();
        EXPECT_TRUE(binding.parameters.empty());

        click(color_item_point(), ImGuiMouseButton_Right);
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        EXPECT_EQ(GImGui->OpenPopupStack.back().PopupId, color_item_id("context"));
        ImGui::ClosePopupToLevel(0, true);
        frame();
        EXPECT_FALSE(history.can_undo());

        click(color_item_point("##ColorButton"));
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        EXPECT_FLOAT_EQ(GImGui->ColorPickerRef.x, 2.0f);
        EXPECT_FLOAT_EQ(GImGui->ColorPickerRef.y, 0.25f);
        EXPECT_FLOAT_EQ(GImGui->ColorPickerRef.z, -0.5f);
        EXPECT_FLOAT_EQ(GImGui->ColorPickerRef.w, 0.75f);
        frame();
        auto* picker = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(picker, nullptr);
        const ImVec2 picker_point{picker->DC.CursorStartPos.x + 30,
            picker->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing() + 40};
        ImGui::GetIO().AddMousePosEvent(picker_point.x, picker_point.y);
        frame();
        EXPECT_EQ(GImGui->HoveredId, ImHashStr("sv", 0, picker->GetID("##picker")));
        drag_at(picker_point);
        EXPECT_TRUE(edit.active());
        EXPECT_EQ(history.undo_size(), 0u);
        ASSERT_TRUE(binding.parameters.contains("score_color"));
        const auto picked = std::get<Comet::Math::Vec4>(binding.parameters.at("score_color"));
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(history.undo_size(), 1u);
        frame();
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_TRUE(binding.parameters.empty());
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(std::get<Comet::Math::Vec4>(binding.parameters.at("score_color")), picked);
        ASSERT_TRUE(history.undo());
        ImGui::ClosePopupToLevel(0, true);
        frame();
        EXPECT_TRUE(binding.parameters.empty());
        EXPECT_FALSE(history.can_undo());

        drag_at(color_item_point());
        EXPECT_TRUE(edit.active());
        EXPECT_EQ(history.undo_size(), 0u);
        ASSERT_TRUE(binding.parameters.contains("score_color"));
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(history.undo_size(), 1u);
        const auto edited = std::get<Comet::Math::Vec4>(binding.parameters.at("score_color"));
        EXPECT_GT(edited.x, 2.0f);
        EXPECT_FLOAT_EQ(edited.y, 0.25f);
        EXPECT_FLOAT_EQ(edited.z, -0.5f);
        EXPECT_FLOAT_EQ(edited.w, 0.75f);
        ASSERT_TRUE(history.undo());
        EXPECT_TRUE(binding.parameters.empty());
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(std::get<Comet::Math::Vec4>(binding.parameters.at("score_color")), edited);

        restore_parameters();
        EXPECT_TRUE(binding.parameters.empty());
        EXPECT_EQ(history.undo_size(), 2u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(std::get<Comet::Math::Vec4>(binding.parameters.at("score_color")), edited);
        ASSERT_TRUE(history.redo());
        EXPECT_TRUE(binding.parameters.empty());
        ASSERT_TRUE(history.undo());
        auto label_point = color_item_point("##ColorButton");
        label_point.x += ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + 3;
        ASSERT_NO_FATAL_FAILURE(parameter_menu(label_point, "score_color"));
        EXPECT_FALSE(binding.parameters.contains("score_color"));
        EXPECT_EQ(history.undo_size(), 2u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(std::get<Comet::Math::Vec4>(binding.parameters.at("score_color")), edited);
        ASSERT_TRUE(history.undo());
        EXPECT_TRUE(binding.parameters.empty());
    }

    TEST_F(EditingUiTest, ScriptColorAlphaGestureCanBeCanceledWithoutClamping) {
        auto script = Comet::Script::create(COLOR_SCRIPT_SOURCE);
        ASSERT_TRUE(script);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        const Comet::Math::Vec4 original{3, -1, 0.5f, 2};
        binding.parameters.emplace("score_color", original);
        frame();
        drag_at(color_item_point("##W"));
        ASSERT_TRUE(edit.active());
        const auto edited = std::get<Comet::Math::Vec4>(binding.parameters.at("score_color"));
        EXPECT_GT(edited.w, 2.0f);
        EXPECT_FLOAT_EQ(edited.x, original.x);
        EXPECT_FLOAT_EQ(edited.y, original.y);
        EXPECT_FLOAT_EQ(edited.z, original.z);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(std::get<Comet::Math::Vec4>(binding.parameters.at("score_color")), original);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, PlainVec4UsesVectorWidgetEvenWhenItsNameContainsColor) {
        auto script =
            Comet::Script::create("return {properties = {score_color = {2, -1, 0.5, 3}}}");
        ASSERT_TRUE(script);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        bool rendered = false;
        ImVec2 vector_point;
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::Vec4, [&, builtin = create_property_editor_registry(assets)](
                                           const Comet::PropertyDescriptor& property, void* value) {
                rendered = true;
                const auto result = builtin.edit_property(property, value);
                const auto start = ImGui::GetItemRectMin();
                vector_point = {start.x + 20, start.y + 8};
                return result;
            }));
        frame();
        ASSERT_TRUE(rendered);
        EXPECT_TRUE(binding.parameters.empty());
        drag_at(vector_point);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        ASSERT_TRUE(binding.parameters.contains("score_color"));
        const auto edited = std::get<Comet::Math::Vec4>(binding.parameters.at("score_color"));
        EXPECT_GT(edited.x, 2.0f);
        EXPECT_FLOAT_EQ(edited.y, -1.0f);
        EXPECT_FLOAT_EQ(edited.z, 0.5f);
        EXPECT_FLOAT_EQ(edited.w, 3.0f);
        EXPECT_EQ(history.undo_size(), 1u);
    }

    TEST_F(EditingUiTest, PlayColorEditsUseRunningMetadataAndLeaveEditSceneAndHistoryUntouched) {
        auto script = Comet::Script::create(COLOR_SCRIPT_SOURCE);
        ASSERT_TRUE(script);
        const Comet::AssetHandle handle{1234};
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto& edit_binding = entity.add_component<Comet::ScriptComponent>();
        edit_binding.asset = handle;
        const Comet::Math::Vec4 original{3, -1, 0.5f, 2};
        edit_binding.parameters.emplace("score_color", original);
        Comet::SceneSerializer serializer(components);
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned);
        auto& runtime_scene = *cloned.value();
        auto runtime_entity = runtime_scene.find_entity(entity.get_uuid());
        auto& runtime_binding = runtime_entity.get_component<Comet::ScriptComponent>();
        Comet::SceneRuntime runtime;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime_assets);
        ASSERT_TRUE(runtime.add_system(std::move(candidate_system)));
        observe_runtime(runtime);
        ASSERT_TRUE(runtime.start(runtime_scene));
        state.mode = EditorMode::Play;
        selection.set_scene(runtime_scene);
        selection.select_entity(runtime_entity.get_id());
        auto replacement =
            Comet::Script::create("return {properties = {score_color = {1, 1, 1, 1}}}");
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, replacement.value()));
        const auto before = history.state_id();
        frame();
        drag_at(color_item_point());
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_GT(std::get<Comet::Math::Vec4>(runtime_binding.parameters.at("score_color")).x, 3);
        EXPECT_EQ(std::get<Comet::Math::Vec4>(edit_binding.parameters.at("score_color")), original);
        EXPECT_EQ(history.state_id(), before);
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(runtime.find_system<Comet::ScriptSystem>()->running_script(runtime_entity),
            script.value());
        restore_parameters();
        EXPECT_TRUE(runtime_binding.parameters.empty());
        EXPECT_EQ(std::get<Comet::Math::Vec4>(edit_binding.parameters.at("score_color")), original);
        EXPECT_EQ(history.state_id(), before);
        ASSERT_TRUE(runtime.stop());
        selection.set_scene(scene);
        selection.select_entity(entity.get_id());
        state.mode = EditorMode::Edit;
    }

    TEST_F(EditingUiTest, PlayParameterReloadCancelsOldGestureWithoutChangingEditOrOtherPanels) {
        const Comet::AssetHandle handle{1234};
        auto original = Comet::Script::create("return {properties = {speed = 100}}");
        ASSERT_TRUE(original);
        ASSERT_TRUE(runtime_assets.register_asset(handle, original.value()));
        auto& edit_binding = entity.add_component<Comet::ScriptComponent>();
        edit_binding.asset = handle;
        edit_binding.parameters.emplace("speed", 12.0f);
        ASSERT_TRUE(edit.apply({entity.get_uuid(), "name", "name"}, std::string("Authored")));
        const auto before = history.state_id();
        auto cloned = Comet::SceneSerializer(components).clone(scene);
        ASSERT_TRUE(cloned);
        auto runtime_entity = cloned.value()->find_entity(entity.get_uuid());
        auto& binding = runtime_entity.get_component<Comet::ScriptComponent>();
        Comet::SceneRuntime runtime;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime_assets);
        ASSERT_TRUE(runtime.add_system(std::move(candidate_system)));
        observe_runtime(runtime);
        ASSERT_TRUE(runtime.start(*cloned.value()));
        state.mode = EditorMode::Play;
        selection.set_scene(*cloned.value());
        selection.select_entity(runtime_entity.get_id());
        ImVec2 parameter_point;
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Float,
            [&, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                const auto result = builtin.edit_property(property, value);
                const auto start = ImGui::GetItemRectMin();
                parameter_point = {start.x + 20, start.y + 8};
                return result;
            }));
        std::string displayed_string;
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::String,
            [&, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                displayed_string = *static_cast<std::string*>(value);
                return builtin.edit_property(property, value);
            }));
        frame();
        drag_at(parameter_point);
        ASSERT_NE(ImGui::GetActiveID(), 0u);
        ASSERT_GT(std::get<float>(binding.parameters.at("speed")), 12.0f);
        EXPECT_FALSE(edit.active());

        auto changed = Comet::Script::create("return {properties = {speed = 'new type'}}");
        ASSERT_TRUE(changed);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, changed.value()));
        const auto old_parameters = binding.parameters;
        remove_incompatible_parameters();
        EXPECT_EQ(binding.parameters, old_parameters);
        EXPECT_EQ(runtime.find_system<Comet::ScriptSystem>()->running_script(runtime_entity),
            original.value());
        EXPECT_TRUE(displayed_string.empty());
        EXPECT_NE(ImGui::GetActiveID(), 0u);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(runtime.find_system<Comet::ScriptSystem>()->running_script(runtime_entity),
            changed.value());
        frame();
        EXPECT_EQ(displayed_string, "new type");
        EXPECT_EQ(ImGui::GetActiveID(), 0u);
        EXPECT_FALSE(binding.parameters.contains("speed"));
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(parameter_point.x + 90, parameter_point.y);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(binding.parameters.contains("speed"));

        focus_text_input();
        const auto other_active_item = ImGui::GetActiveID();
        ASSERT_NE(other_active_item, 0u);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, original.value()));
        ASSERT_TRUE(runtime.advance(0));
        frame();
        EXPECT_EQ(ImGui::GetActiveID(), other_active_item);
        EXPECT_FLOAT_EQ(std::get<float>(edit_binding.parameters.at("speed")), 12.0f);
        EXPECT_EQ(history.state_id(), before);
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_NE(entity.get_component<Comet::NameComponent>().name, "Authored");
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(runtime.find_system<Comet::ScriptSystem>()->running_script(runtime_entity));
        selection.set_scene(scene);
        state.mode = EditorMode::Edit;
    }

    TEST_F(EditingUiTest, PlayScriptReloadPreservesUnrelatedInspectorGesture) {
        const Comet::AssetHandle handle{1234};
        auto original = Comet::Script::create("return {properties = {speed = 100}}");
        ASSERT_TRUE(original);
        ASSERT_TRUE(runtime_assets.register_asset(handle, original.value()));
        entity.add_component<Comet::ScriptComponent>().asset = handle;
        Comet::SceneRuntime runtime;
        auto candidate_system = std::make_unique<Comet::ScriptSystem>(runtime_assets);
        ASSERT_TRUE(runtime.add_system(std::move(candidate_system)));
        observe_runtime(runtime);
        ASSERT_TRUE(runtime.start(scene));
        state.mode = EditorMode::Play;
        frame();
        drag();
        const auto transform_item = ImGui::GetActiveID();
        ASSERT_NE(transform_item, 0u);
        const float before = x();

        auto changed = Comet::Script::create("return {properties = {speed = 'new type'}}");
        ASSERT_TRUE(changed);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, changed.value()));
        ASSERT_TRUE(runtime.advance(0));
        frame();
        EXPECT_EQ(ImGui::GetActiveID(), transform_item);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(drag_point.x + 90, drag_point.y);
        frame();
        EXPECT_GT(x(), before);
        io.AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(edit.active());
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(EditingUiTest, CameraInputsStayCompactAndLeaveRoomForLabels) {
        entity.add_component<Comet::CameraComponent>();
        std::vector<std::pair<float, float>> bounds;
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Float,
            [&bounds, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                const auto result = builtin.edit_property(property, value);
                const float end = ImGui::GetItemRectMax().x;
                const float field_width =
                    ImGui::GetItemRectSize().x
                    - ImGui::CalcTextSize(property.display_name.c_str(), nullptr, true).x
                    - ImGui::GetStyle().ItemInnerSpacing.x;
                bounds.emplace_back(end, field_width);
                return result;
            }));
        for(const float width : {260.0f, 350.0f, 700.0f}) {
            inspector_width = width;
            frame();
            bounds.clear();
            frame();
            const auto* window = ImGui::FindWindowByName("Inspector");
            ASSERT_NE(window, nullptr);
            ASSERT_EQ(bounds.size(), 4);
            for(const auto& [end, field_width] : bounds) {
                EXPECT_LE(end, window->WorkRect.Max.x + 1);
                EXPECT_GT(field_width, 0);
                EXPECT_LE(field_width, ImGui::GetFontSize() * 9 + 1);
            }
        }
    }

    class HierarchyUiTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui;
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity();
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        CommandHistory history;
        SelectionService selection{scene};
        EditorState state;
        SceneCommands::EntityClipboard clipboard;
        HierarchyPanel hierarchy{selection, history, state, clipboard};

        void SetUp() override {
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            draw();
            draw();
        }
        void draw() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 400));
            hierarchy.render();
            ImGui::Render();
        }
    };

    TEST_F(HierarchyUiTest, RendersNewSelectionSceneAfterSwitch) {
        auto* window = ImGui::FindWindowByName("Hierarchy");
        ASSERT_NE(window, nullptr);
        const float first_scene_last_row = window->DC.CursorPosPrevLine.y;

        Comet::Scene next_scene;
        next_scene.create_entity("First");
        next_scene.create_entity("Second");
        selection.set_scene(next_scene);
        history.bind_scene(&next_scene);
        hierarchy.reset_for_scene_change();
        draw();

        EXPECT_GT(window->DC.CursorPosPrevLine.y, first_scene_last_row);
        EXPECT_EQ(&selection.get_scene(), &next_scene);
    }

    TEST_F(HierarchyUiTest, HierarchyQueuesOneRequestWithoutMutatingDuringUiTraversal) {
        auto* window = ImGui::FindWindowByName("Hierarchy");
        ASSERT_NE(window, nullptr);
        const ImVec2 entity_point(window->WorkRect.Min.x + 70,
            window->DC.CursorPosPrevLine.y + ImGui::GetTextLineHeight() * 0.5f);
        const auto choose = [&](const char* action, const bool scene_root = false) {
            if(!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                ImGui::ClosePopupToLevel(0, true);
            auto& io = ImGui::GetIO();
            const bool create = std::string_view(action) == "Create Entity";
            ImVec2 point = create ? ImVec2(300, 350) : entity_point;
            if(scene_root)
                point = ImVec2(window->WorkRect.Min.x + 70,
                    window->WorkRect.Min.y + ImGui::GetTextLineHeight() * 0.5f);
            io.AddMousePosEvent(point.x, point.y);
            draw();
            io.AddMouseButtonEvent(1, true);
            draw();
            io.AddMouseButtonEvent(1, false);
            draw();
            draw();
            auto& context = *ImGui::GetCurrentContext();
            ASSERT_EQ(context.OpenPopupStack.Size, 1);
            auto* popup = context.OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID(action));
        };
        choose("Create Entity");
        draw();
        EXPECT_EQ(scene.entity_count(), 1);
        auto request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Create);
        EXPECT_EQ(request->generation, history.generation());
        EXPECT_FALSE(request->parent);
        EXPECT_FALSE(hierarchy.take_request());
        selection.clear();
        choose("Create Child");
        draw();
        request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Create);
        EXPECT_EQ(request->parent, entity.get_uuid());
        EXPECT_FALSE(request->entity);
        EXPECT_EQ(scene.entity_count(), 1);
        choose("Delete");
        draw();
        request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Delete);
        EXPECT_EQ(request->entity, entity.get_uuid());
        EXPECT_TRUE(entity);
        state.mode = EditorMode::Play;
        choose("Create Child");
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        choose("Create Entity");
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        choose("Delete");
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        state.mode = EditorMode::Edit;
        history.bind_scene(nullptr);
        choose("Create Child");
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        choose("Create Entity");
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        choose("Delete");
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        history.bind_scene(&scene);
        choose("Create Entity");
        draw();
        selection.set_scene(scene);
        hierarchy.reset_for_scene_change();
        EXPECT_FALSE(hierarchy.take_request());
        choose("Create Entity");
        draw();
        history.bind_scene(&scene);
        // 同一个 Scene 地址也可能已经开始了新的文档历史。
        EXPECT_FALSE(hierarchy.take_request());
        choose("Create Entity");
        draw();
        request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->generation, history.generation());
        EXPECT_EQ(scene.entity_count(), 1);
        choose("Create Entity", true);
        draw();
        request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Create);
        EXPECT_FALSE(request->parent);
        EXPECT_EQ(scene.entity_count(), 1);
        const float previous_last_row = window->DC.CursorPosPrevLine.y;
        choose("Create Child");
        draw();
        request = hierarchy.take_request();
        ASSERT_TRUE(request);
        const auto child =
            SceneCommands::create_entity(history, components, "Entity", request->parent);
        ASSERT_TRUE(child);
        selection.select_entity(scene.find_entity(child).get_id());
        draw();
        draw();
        EXPECT_EQ(scene.get_parent(scene.find_entity(child)), entity);
        EXPECT_GT(window->DC.CursorPosPrevLine.y, previous_last_row);
    }

    TEST_F(HierarchyUiTest, HierarchyContextMenuQueuesDuplicateForClickedEntity) {
        selection.clear();
        auto* window = ImGui::FindWindowByName("Hierarchy");
        ASSERT_NE(window, nullptr);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(window->WorkRect.Min.x + 70,
            window->DC.CursorPosPrevLine.y + ImGui::GetTextLineHeight() * 0.5f);
        draw();
        io.AddMouseButtonEvent(1, true);
        draw();
        io.AddMouseButtonEvent(1, false);
        draw();
        draw();
        auto& context = *ImGui::GetCurrentContext();
        ASSERT_EQ(context.OpenPopupStack.Size, 1);
        auto* popup = context.OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Duplicate"));
        draw();
        const auto request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Duplicate);
        EXPECT_EQ(request->entity, entity.get_uuid());
        EXPECT_EQ(scene.entity_count(), 1);
        EXPECT_FALSE(selection.get_selected_entity());
        EXPECT_FALSE(hierarchy.take_request());

        state.mode = EditorMode::Play;
        draw();
        io.AddMouseButtonEvent(1, true);
        draw();
        io.AddMouseButtonEvent(1, false);
        draw();
        draw();
        ASSERT_EQ(context.OpenPopupStack.Size, 1);
        popup = context.OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Duplicate"));
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        EXPECT_EQ(scene.entity_count(), 1);
    }

    TEST_F(HierarchyUiTest, BatchPlacementDialogCapturesTargetsAndRejectsPlay) {
        const auto open = [&] {
            auto* window = ImGui::FindWindowByName("Hierarchy");
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(window->WorkRect.Min.x + 70,
                window->DC.CursorPosPrevLine.y + ImGui::GetTextLineHeight() * 0.5f);
            draw();
            io.AddMouseButtonEvent(1, true);
            draw();
            io.AddMouseButtonEvent(1, false);
            draw();
            draw();
            ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
            auto* popup = GImGui->OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID("Place Copies"));
            draw();
            draw();
        };
        ASSERT_NO_FATAL_FAILURE(open());
        auto* dialog = ImGui::FindWindowByName("Place Copies");
        ASSERT_NE(dialog, nullptr);
        ASSERT_TRUE(dialog->Active);
        EXPECT_FALSE(hierarchy.take_request());
        selection.clear();
        ImGui::ActivateItemByID(dialog->GetID("Place"));
        draw();
        const auto request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Duplicate);
        EXPECT_EQ(request->entities, (std::vector{entity.get_uuid()}));
        EXPECT_EQ(request->duplicate.copies, 3);
        EXPECT_EQ(request->duplicate.world_step, Comet::Math::Vec3(2, 0, 0));
        EXPECT_EQ(scene.entity_count(), 1u);
        draw();
        ASSERT_NO_FATAL_FAILURE(open());
        state.mode = EditorMode::Play;
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        EXPECT_TRUE(GImGui->OpenPopupStack.empty());
    }

    TEST_F(HierarchyUiTest, PrefabRequestUsesClickedEntityAndRejectsPlayOrChangedScene) {
        selection.clear();
        const auto choose = [&](const char* action) {
            if(!GImGui->OpenPopupStack.empty())
                ImGui::ClosePopupToLevel(0, true);
            auto* window = ImGui::FindWindowByName("Hierarchy");
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(window->WorkRect.Min.x + 70,
                window->DC.CursorPosPrevLine.y + ImGui::GetTextLineHeight() * 0.5f);
            draw();
            io.AddMouseButtonEvent(1, true);
            draw();
            io.AddMouseButtonEvent(1, false);
            draw();
            draw();
            ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
            auto* popup = GImGui->OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID(action));
            draw();
        };
        choose("Save Prefab");
        const auto request = hierarchy.take_prefab_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->entity, entity.get_uuid());
        EXPECT_EQ(request->generation, history.generation());
        EXPECT_FALSE(hierarchy.take_prefab_request());
        EXPECT_FALSE(selection.get_selected_entity());
        EXPECT_EQ(history.undo_size(), 0);
        const auto prefab = Comet::Prefab::capture(scene, components, entity);
        ASSERT_TRUE(prefab);
        entity.add_component<Comet::PrefabInstanceComponent>(Comet::PrefabInstanceComponent{
            Comet::AssetHandle{13}, std::make_shared<const Comet::Prefab>(prefab.value()),
            {{entity.get_uuid(), entity.get_uuid()}}});
        choose("Update Prefab");
        auto structural = hierarchy.take_request();
        ASSERT_TRUE(structural);
        EXPECT_EQ(structural->type, HierarchyPanel::Request::Type::UpdatePrefab);
        EXPECT_EQ(structural->entity, entity.get_uuid());
        choose("Detach Prefab");
        structural = hierarchy.take_request();
        ASSERT_TRUE(structural);
        EXPECT_EQ(structural->type, HierarchyPanel::Request::Type::DetachPrefab);
        for(const auto& [label, type] :
            {std::pair{"Apply Prefab", HierarchyPanel::Request::Type::ApplyPrefab},
                std::pair{"Revert Prefab", HierarchyPanel::Request::Type::RevertPrefab}}) {
            choose(label);
            structural = hierarchy.take_request();
            ASSERT_TRUE(structural);
            EXPECT_EQ(structural->type, type);
            EXPECT_EQ(structural->entity, entity.get_uuid());
        }
        EXPECT_TRUE(entity.has_component<Comet::PrefabInstanceComponent>());
        entity.get_component<Comet::PrefabInstanceComponent>().source_type =
            Comet::AssetType::Model;
        choose("Apply Prefab");
        EXPECT_FALSE(hierarchy.take_request());
        for(const auto& [label, type] :
            {std::pair{"Update Prefab", HierarchyPanel::Request::Type::UpdatePrefab},
                std::pair{"Revert Prefab", HierarchyPanel::Request::Type::RevertPrefab},
                std::pair{"Detach Prefab", HierarchyPanel::Request::Type::DetachPrefab}}) {
            choose(label);
            structural = hierarchy.take_request();
            ASSERT_TRUE(structural);
            EXPECT_EQ(structural->type, type);
        }
        state.mode = EditorMode::Play;
        choose("Save Prefab");
        EXPECT_FALSE(hierarchy.take_prefab_request());
        choose("Update Prefab");
        EXPECT_FALSE(hierarchy.take_request());
        choose("Detach Prefab");
        EXPECT_FALSE(hierarchy.take_request());
        for(const auto* label : {"Apply Prefab", "Revert Prefab"}) {
            choose(label);
            EXPECT_FALSE(hierarchy.take_request());
        }
        state.mode = EditorMode::Edit;
        choose("Save Prefab");
        Comet::Scene next;
        history.bind_scene(&next);
        EXPECT_FALSE(hierarchy.take_prefab_request());
        EXPECT_EQ(scene.entity_count(), 1);
    }

    TEST_F(HierarchyUiTest, PasteContextMenuTargetsTheCurrentScene) {
        auto* window = ImGui::FindWindowByName("Hierarchy");
        ASSERT_NE(window, nullptr);
        ASSERT_TRUE(clipboard.copy(scene, components, entity.get_uuid()));
        const auto choose = [&](const ImVec2 point, const char* action) {
            if(!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                ImGui::ClosePopupToLevel(0, true);
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            draw();
            io.AddMouseButtonEvent(1, true);
            draw();
            io.AddMouseButtonEvent(1, false);
            draw();
            draw();
            auto& context = *ImGui::GetCurrentContext();
            ASSERT_EQ(context.OpenPopupStack.Size, 1);
            auto* popup = context.OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID(action));
            draw();
        };

        Comet::Scene next_scene;
        selection.set_scene(next_scene);
        history.bind_scene(&next_scene);
        hierarchy.reset_for_scene_change();
        draw();
        window = ImGui::FindWindowByName("Hierarchy");
        const ImVec2 root_point(window->WorkRect.Min.x + 70,
            window->WorkRect.Min.y + ImGui::GetTextLineHeight() * 0.5f);
        choose(root_point, "Paste");
        auto request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Paste);
        EXPECT_FALSE(request->parent);
        EXPECT_EQ(request->generation, history.generation());
    }

    TEST_F(HierarchyUiTest, RenameIsRequestedFromContextMenuOnlyAfterConfirmation) {
        auto* window = ImGui::FindWindowByName("Hierarchy");
        ASSERT_NE(window, nullptr);
        auto& io = ImGui::GetIO();
        const ImVec2 entity_point(window->WorkRect.Min.x + 70,
            window->DC.CursorPosPrevLine.y + ImGui::GetTextLineHeight() * 0.5f);
        const auto open_rename = [&] {
            io.AddMousePosEvent(entity_point.x, entity_point.y);
            draw();
            io.AddMouseButtonEvent(1, true);
            draw();
            io.AddMouseButtonEvent(1, false);
            draw();
            draw();
            auto& context = *ImGui::GetCurrentContext();
            ASSERT_EQ(context.OpenPopupStack.Size, 1);
            auto* popup = context.OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID("Rename"));
            draw();
            draw();
        };

        open_rename();
        auto* dialog = ImGui::FindWindowByName("Rename Entity");
        ASSERT_NE(dialog, nullptr);
        ImGui::ActivateItemByID(dialog->GetID("Cancel"));
        draw();
        EXPECT_FALSE(hierarchy.take_rename_request());
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "Entity");

        open_rename();
        dialog = ImGui::FindWindowByName("Rename Entity");
        ASSERT_NE(dialog, nullptr);
        const auto text = "新名称" + std::string(512, 'n');
        ImGui::ActivateItemByID(dialog->GetID("Name"));
        draw();
        const auto modifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
        io.AddKeyEvent(modifier, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        draw();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(modifier, false);
        draw();
        io.AddInputCharactersUTF8(text.c_str());
        draw();
        EXPECT_FALSE(hierarchy.take_rename_request());
        io.AddKeyEvent(ImGuiKey_Enter, true);
        draw();
        auto request = hierarchy.take_rename_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->entity, entity.get_uuid());
        EXPECT_EQ(request->name, text);
        EXPECT_EQ(request->generation, history.generation());
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "Entity");
    }

    TEST_F(EditingUiTest, AddComponentMenuUsesHistoryAndIsDisabledInPlay) {
        auto* window = ImGui::FindWindowByName("Inspector");
        ASSERT_NE(window, nullptr);
        ImGui::ActivateItemByID(window->GetID("Add Component"));
        frame();
        frame();
        auto& context = *ImGui::GetCurrentContext();
        ASSERT_EQ(context.OpenPopupStack.Size, 1);
        auto* popup = context.OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Camera"));
        frame();
        EXPECT_TRUE(entity.has_component<Comet::CameraComponent>());
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
        ASSERT_TRUE(history.redo());
        EXPECT_TRUE(entity.has_component<Comet::CameraComponent>());
        state.mode = EditorMode::Play;
        frame();
        ImGui::ActivateItemByID(window->GetID("Add Component"));
        frame();
        EXPECT_EQ(context.OpenPopupStack.Size, 0);
        EXPECT_FALSE(entity.has_component<Comet::MeshRendererComponent>());
    }

    TEST_F(EditingUiTest, ComponentHeaderContextMenuRemovesAndRestoresCamera) {
        entity.add_component<Comet::CameraComponent>(Comet::CameraComponent{.fov = 63});
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "camera", "fov"}));
        ASSERT_TRUE(edit.preview(72.0f));
        frame();
        auto* window = ImGui::FindWindowByName("Inspector");
        ASSERT_NE(window, nullptr);
        // 此 fixture 未注册 Bool/Float 控件，Camera 标题紧邻 Add 按钮上方。
        const ImVec2 header(window->WorkRect.Min.x + 30, window->DC.CursorPosPrevLine.y
                                                             - ImGui::GetStyle().ItemSpacing.y
                                                             - ImGui::GetFrameHeight() * 0.5f);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(header.x, header.y);
        frame();
        io.AddMouseButtonEvent(1, true);
        frame();
        io.AddMouseButtonEvent(1, false);
        frame();
        frame();
        auto& context = *ImGui::GetCurrentContext();
        ASSERT_EQ(context.OpenPopupStack.Size, 1);
        auto* popup = context.OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Remove Component"));
        frame();
        EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(history.undo_size(), 2);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 72);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 63);
    }

    TEST_F(EditingUiTest, ComponentMenuRequiresMatchingHistoryScene) {
        const auto try_open = [&] {
            frame();
            auto* window = ImGui::FindWindowByName("Inspector");
            ASSERT_NE(window, nullptr);
            ImGui::ActivateItemByID(window->GetID("Add Component"));
            frame();
            EXPECT_EQ(ImGui::GetCurrentContext()->OpenPopupStack.Size, 0);
            EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
        };
        history.bind_scene(nullptr);
        try_open();
        Comet::Scene other;
        auto same_uuid = other.create_entity_with_uuid(entity.get_uuid());
        ASSERT_TRUE(same_uuid);
        history.bind_scene(&other);
        try_open();
        EXPECT_FALSE(same_uuid.has_component<Comet::CameraComponent>());
        EXPECT_FALSE(history.can_undo());
        history.bind_scene(&scene);
    }

    TEST_F(EditingUiTest, MultiSelectionDragPreservesOtherAxesAndMakesOneUndoRecord) {
        auto peer = scene.create_entity();
        entity.edit_transform([](auto& value) { value.translation = {1, 2, 3}; });
        peer.edit_transform([](auto& value) { value.translation = {10, 20, 30}; });
        selection.select_entities(std::array{peer.get_uuid(), entity.get_uuid()});
        frame();
        drag();
        const auto primary_after = entity.get_component<Comet::TransformComponent>().translation;
        const auto peer_after = peer.get_component<Comet::TransformComponent>().translation;
        EXPECT_NE(primary_after.x, 1);
        EXPECT_EQ(primary_after, Comet::Math::Vec3(primary_after.x, 2, 3));
        EXPECT_EQ(peer_after, Comet::Math::Vec3(primary_after.x, 20, 30));
        EXPECT_EQ(history.undo_size(), 0u);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        ASSERT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(1, 2, 3));
        EXPECT_EQ(peer.get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(10, 20, 30));
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(entity.get_component<Comet::TransformComponent>().translation, primary_after);
        EXPECT_EQ(peer.get_component<Comet::TransformComponent>().translation, peer_after);
    }

    TEST_F(EditingUiTest, MultiSelectionEscapeRestoresAllWithoutHistory) {
        auto peer = scene.create_entity();
        peer.edit_transform([](auto& value) { value.translation = {10, 20, 30}; });
        selection.select_entities(std::array{peer.get_uuid(), entity.get_uuid()});
        frame();
        drag();
        EXPECT_NE(x(), 0);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_EQ(peer.get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(10, 20, 30));
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(edit.active());
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
    }

    TEST_F(EditingUiTest, MultiSelectionPlayEditsAllWithoutHistory) {
        auto peer = scene.create_entity();
        peer.edit_transform([](auto& value) { value.translation = {10, 20, 30}; });
        selection.select_entities(std::array{peer.get_uuid(), entity.get_uuid()});
        frame();
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        drag();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_NE(x(), 0);
        EXPECT_EQ(peer.get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(x(), 20, 30));
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, MultiSelectionOnlyShowsCommonComponentsAndApplicableFields) {
        auto peer = scene.create_entity();
        entity.add_component<Comet::ColliderComponent>(
            Comet::ColliderComponent{.shape = Comet::ColliderShape::Capsule});
        selection.select_entities(std::array{peer.get_uuid(), entity.get_uuid()});
        std::vector<std::string> fields;
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::Float, [&](const Comet::PropertyDescriptor& property, void*) {
                fields.push_back(property.id);
                return PropertyEditResult{};
            }));
        frame();
        EXPECT_TRUE(fields.empty());
        peer.add_component<Comet::ColliderComponent>(
            Comet::ColliderComponent{.shape = Comet::ColliderShape::Sphere});
        frame();
        EXPECT_NE(std::find(fields.begin(), fields.end(), "radius"), fields.end());
        EXPECT_EQ(std::find(fields.begin(), fields.end(), "height"), fields.end());
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, ChangingSelectionCommitsTheOldGroupWithoutEditingTheNewTarget) {
        auto peer = scene.create_entity();
        auto next = scene.create_entity();
        selection.select_entities(std::array{peer.get_uuid(), entity.get_uuid()});
        frame();
        drag();
        const auto changed = x();
        ASSERT_NE(changed, 0);
        selection.select_entity(next.get_id());
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(edit.active());
        EXPECT_FLOAT_EQ(next.get_component<Comet::TransformComponent>().translation.x, 0);
        EXPECT_FLOAT_EQ(peer.get_component<Comet::TransformComponent>().translation.x, changed);
        ASSERT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_FLOAT_EQ(peer.get_component<Comet::TransformComponent>().translation.x, 0);
    }

    TEST_F(EditingUiTest, MultiSelectionJointAlignmentUsesEachObjectsOwnAnchor) {
        auto peer = scene.create_entity();
        entity.edit_transform([](auto& value) { value.translation = {1, 2, 3}; });
        peer.edit_transform([](auto& value) { value.translation = {10, 20, 30}; });
        entity.add_component<Comet::PointJointComponent>(
            Comet::PointJointComponent{.anchor = {1, 0, 0}});
        peer.add_component<Comet::PointJointComponent>(
            Comet::PointJointComponent{.anchor = {0, 2, 0}});
        selection.select_entities(std::array{peer.get_uuid(), entity.get_uuid()});
        frame();
        ASSERT_NO_FATAL_FAILURE(align_joint_anchor());
        EXPECT_EQ(entity.get_component<Comet::PointJointComponent>().connected_anchor,
            Comet::Math::Vec3(2, 2, 3));
        EXPECT_EQ(peer.get_component<Comet::PointJointComponent>().connected_anchor,
            Comet::Math::Vec3(10, 22, 30));
        ASSERT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<Comet::PointJointComponent>().connected_anchor,
            Comet::Math::Vec3(0));
        EXPECT_EQ(peer.get_component<Comet::PointJointComponent>().connected_anchor,
            Comet::Math::Vec3(0));
    }

    TEST_F(EditingUiTest, MultiSelectionParametersPreservePeersAndResetEvenWhenPrimaryInherits) {
        const Comet::AssetHandle handle{1234};
        auto script = Comet::Script::create("return {properties = {speed = 100, enabled = true}}");
        ASSERT_TRUE(script);
        ASSERT_TRUE(runtime_assets.register_asset(handle, script.value()));
        auto peer = scene.create_entity();
        entity.add_component<Comet::ScriptComponent>().asset = handle;
        peer.add_component<Comet::ScriptComponent>().asset = handle;
        auto& primary = entity.get_component<Comet::ScriptComponent>();
        auto& other = peer.get_component<Comet::ScriptComponent>();
        primary.parameters = {{"enabled", false}};
        other.parameters = {{"speed", 200.0f}, {"enabled", true}};
        const auto primary_before = primary.parameters;
        const auto peer_before = other.parameters;
        selection.select_entities(std::array{peer.get_uuid(), entity.get_uuid()});
        ImVec2 label_point;
        bool assign = false;
        bool mixed = false;
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Float,
            [&, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                mixed = (GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0;
                auto result = builtin.edit_property(property, value);
                const auto maximum = ImGui::GetItemRectMax();
                label_point = {maximum.x - 2, maximum.y - ImGui::GetTextLineHeight() * 0.5f};
                if(std::exchange(assign, false)) {
                    // Choosing the displayed value still changes peers with another value.
                    *static_cast<float*>(value) = 100.0f;
                    result = {.changed = true, .finished = true};
                }
                return result;
            }));
        frame();
        EXPECT_TRUE(mixed);
        ASSERT_NO_FATAL_FAILURE(parameter_menu(label_point, "speed"));
        EXPECT_EQ(primary.parameters, primary_before);
        EXPECT_FALSE(other.parameters.contains("speed"));
        EXPECT_TRUE(std::get<bool>(other.parameters.at("enabled")));
        ASSERT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(other.parameters, peer_before);
        frame();
        assign = true;
        frame();
        EXPECT_EQ(std::get<float>(primary.parameters.at("speed")), 100);
        EXPECT_EQ(std::get<float>(other.parameters.at("speed")), 100);
        EXPECT_FALSE(std::get<bool>(primary.parameters.at("enabled")));
        EXPECT_TRUE(std::get<bool>(other.parameters.at("enabled")));
        ASSERT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(primary.parameters, primary_before);
        EXPECT_EQ(other.parameters, peer_before);
        frame();
        restore_parameters();
        EXPECT_TRUE(primary.parameters.empty());
        EXPECT_TRUE(other.parameters.empty());
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(primary.parameters, primary_before);
        EXPECT_EQ(other.parameters, peer_before);
    }

    TEST_F(EditingUiTest, DragFloat3CommitsOneRecordOnRelease) {
        drag();
        EXPECT_NE(x(), 0);
        EXPECT_EQ(history.undo_size(), 0);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(history.undo_size(), 1);
        const float after = x();
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 0);
        ASSERT_TRUE(history.redo());
        EXPECT_FLOAT_EQ(x(), after);
    }

    TEST_F(EditingUiTest, CompoundEditorDoesNotDependOnLastImGuiItem) {
        draw_trailing_item = true;
        frame();
        drag();
        EXPECT_NE(x(), 0);
        EXPECT_EQ(history.undo_size(), 0);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        ASSERT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 0);
    }

    TEST_F(EditingUiTest, EntityNameIsNotEditedInInspector) {
        bool string_editor_called = false;
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::String, [&](const Comet::PropertyDescriptor&, void*) {
                string_editor_called = true;
                return PropertyEditResult{};
            }));
        frame();
        EXPECT_FALSE(string_editor_called);
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "Entity");
    }

    TEST_F(EditingUiTest, MissingHistoryIsNotTreatedAsPlayMode) {
        history.bind_scene(nullptr);
        drag();
        EXPECT_FLOAT_EQ(x(), 0);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, MenuReopensClosedPanelWithOneClick) {
        menu.register_panel(*inspector);
        menu.register_panel(*inspector);
        inspector->set_visible(false);
        frame();
        const auto click = [&](ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        const auto& style = ImGui::GetStyle();
        const float view_x = ImGui::CalcTextSize("文件").x + ImGui::CalcTextSize("项目").x
                             + ImGui::CalcTextSize("编辑").x + 6.0f * style.ItemSpacing.x + 10.0f;
        click({view_x, ImGui::GetFrameHeight() * 0.5f});
        frame();
        ImGuiWindow* popup = nullptr;
        for(auto* window : ImGui::GetCurrentContext()->Windows)
            if(window->Active && (window->Flags & ImGuiWindowFlags_ChildMenu))
                popup = window;
        ASSERT_NE(popup, nullptr);
        const auto menu_bar_id = ImGui::FindWindowByName("##MainMenuBar")->ID;
        ASSERT_EQ(popup->PopupId, ImHashStr("View", 0, ImHashStr("##MenuBar", 0, menu_bar_id)));
        click({popup->DC.CursorStartPos.x + 30,
            popup->DC.CursorStartPos.y + ImGui::GetTextLineHeight() * 0.5f});
        EXPECT_TRUE(inspector->is_open());
    }

    TEST_F(EditingUiTest, EscapeRestoresGestureWithoutRecordingOrReactivation) {
        drag();
        ASSERT_NE(x(), 0);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_FALSE(edit.active());
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, HidingPanelFinishesGesture) {
        drag();
        ASSERT_NE(x(), 0);
        inspector->set_visible(false);
        frame();
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 0);
    }

    TEST_F(EditingUiTest, EnumComboSelectsTypedLightAndRecordsOneUndo) {
        entity.add_component<Comet::LightComponent>();
        ImVec2 combo{};
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::Enum, [&combo, builtin = create_property_editor_registry(assets)](
                                           const Comet::PropertyDescriptor& property, void* value) {
                combo = ImGui::GetCursorScreenPos();
                return builtin.edit_property(property, value);
            }));
        frame();
        frame();
        auto click = [&](ImVec2 position) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(position.x, position.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        click({combo.x + 30, combo.y + 8});
        frame();
        auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
        ASSERT_FALSE(popups.empty());
        const auto* popup = popups.back().Window;
        ASSERT_NE(popup, nullptr);
        click({popup->Pos.x + popup->WindowPadding.x + 30,
            popup->Pos.y + popup->WindowPadding.y + ImGui::GetTextLineHeightWithSpacing()
                + ImGui::GetTextLineHeight() * 0.5f});
        frame();
        EXPECT_EQ(entity.get_component<Comet::LightComponent>().type, Comet::LightType::Point);
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_FALSE(edit.active());
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(
            entity.get_component<Comet::LightComponent>().type, Comet::LightType::Directional);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(entity.get_component<Comet::LightComponent>().type, Comet::LightType::Point);
    }

    TEST_F(EditingUiTest, ContinuousCollisionCheckboxUsesUndoAndSupportsPlayEditing) {
        entity.add_component<Comet::RigidBodyComponent>();
        ImGuiID checkbox = 0;
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::Bool, [&, builtin = create_property_editor_registry(assets)](
                                           const Comet::PropertyDescriptor& property, void* value) {
                auto result = builtin.edit_property(property, value);
                if(property.id == "continuous_collision") {
                    EXPECT_TRUE(property.display_name.starts_with("连续碰撞 (CCD)###"));
                    checkbox = ImGui::GetItemID();
                }
                return result;
            }));
        frame();
        ASSERT_NE(checkbox, 0u);
        ImGui::ActivateItemByID(checkbox);
        frame();
        frame();
        EXPECT_TRUE(entity.get_component<Comet::RigidBodyComponent>().continuous_collision);
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(entity.get_component<Comet::RigidBodyComponent>().continuous_collision);
        ASSERT_TRUE(history.redo());
        EXPECT_TRUE(entity.get_component<Comet::RigidBodyComponent>().continuous_collision);
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        ImGui::ActivateItemByID(checkbox);
        frame();
        frame();
        EXPECT_FALSE(entity.get_component<Comet::RigidBodyComponent>().continuous_collision);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, MassModeSwitchesInputAndSupportsUndoAndPlayEditing) {
        entity.add_component<Comet::RigidBodyComponent>();
        ImVec2 mode_point{}, density_point{};
        bool mass_drawn = false, density_drawn = false;
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::Enum, [&, builtin = create_property_editor_registry(assets)](
                                           const Comet::PropertyDescriptor& property, void* value) {
                const auto result = builtin.edit_property(property, value);
                if(property.id == "mass_mode") {
                    EXPECT_TRUE(property.display_name.starts_with("质量计算###"));
                    const auto start = ImGui::GetItemRectMin();
                    mode_point = {start.x + 20, start.y + 8};
                }
                return result;
            }));
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Float,
            [&, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                const auto result = builtin.edit_property(property, value);
                mass_drawn |= property.id == "mass";
                if(property.id == "density") {
                    density_drawn = true;
                    EXPECT_TRUE(property.display_name.starts_with("密度 (kg/m³)###"));
                    const auto start = ImGui::GetItemRectMin();
                    density_point = {start.x + 20, start.y + 8};
                }
                return result;
            }));
        const auto expect_fields = [&](bool density) {
            mass_drawn = density_drawn = false;
            frame();
            EXPECT_EQ(mass_drawn, !density);
            EXPECT_EQ(density_drawn, density);
        };
        const auto choose_mode = [&](int row) {
            click(mode_point);
            frame();
            const auto* popup = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(popup, nullptr);
            ASSERT_TRUE(popup->Active);
            click({popup->DC.CursorStartPos.x + 20,
                popup->DC.CursorStartPos.y + row * ImGui::GetTextLineHeightWithSpacing()
                    + ImGui::GetTextLineHeight() * 0.5f});
        };
        expect_fields(false);
        choose_mode(1);
        expect_fields(true);
        EXPECT_EQ(entity.get_component<Comet::RigidBodyComponent>().mass_mode,
            Comet::BodyMassMode::Density);
        EXPECT_EQ(history.undo_size(), 1u);
        drag_at(density_point);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        const auto density = entity.get_component<Comet::RigidBodyComponent>().density;
        EXPECT_GT(density, 1000);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::RigidBodyComponent>().density, 1000);
        ASSERT_TRUE(history.undo());
        expect_fields(false);
        ASSERT_TRUE(history.redo());
        expect_fields(true);
        ASSERT_TRUE(history.redo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::RigidBodyComponent>().density, density);

        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        choose_mode(0);
        expect_fields(false);
        EXPECT_EQ(
            entity.get_component<Comet::RigidBodyComponent>().mass_mode, Comet::BodyMassMode::Mass);
        choose_mode(1);
        expect_fields(true);
        drag_at(density_point);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_GT(entity.get_component<Comet::RigidBodyComponent>().density, density);
        EXPECT_FLOAT_EQ(entity.get_component<Comet::RigidBodyComponent>().mass, 1);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, ColliderSurfaceWidgetsUseBoundsUndoAndPlayEditing) {
        entity.add_component<Comet::ColliderComponent>();
        ImVec2 friction_point{}, restitution_point{};
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Float,
            [&, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                auto result = builtin.edit_property(property, value);
                const auto start = ImGui::GetItemRectMin();
                if(property.id == "friction") {
                    EXPECT_TRUE(property.display_name.starts_with("摩擦系数###"));
                    friction_point = {start.x + 20, start.y + 8};
                }
                if(property.id == "restitution") {
                    EXPECT_TRUE(property.display_name.starts_with("弹性系数###"));
                    restitution_point = {start.x + 20, start.y + 8};
                }
                return result;
            }));
        frame();
        frame();
        for(const auto& [member, point] :
            {std::pair{&Comet::ColliderComponent::friction, friction_point},
                std::pair{&Comet::ColliderComponent::restitution, restitution_point}}) {
            const auto before = entity.get_component<Comet::ColliderComponent>().*member;
            drag_at(point);
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            frame();
            const auto after = entity.get_component<Comet::ColliderComponent>().*member;
            EXPECT_GT(after, before);
            EXPECT_LE(after, 1);
            ASSERT_TRUE(history.undo());
            EXPECT_FLOAT_EQ(entity.get_component<Comet::ColliderComponent>().*member, before);
            ASSERT_TRUE(history.redo());
            EXPECT_FLOAT_EQ(entity.get_component<Comet::ColliderComponent>().*member, after);
        }
        EXPECT_EQ(history.undo_size(), 2u);
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        drag_at(friction_point);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FLOAT_EQ(entity.get_component<Comet::ColliderComponent>().friction, 1);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, PlayEditsRuntimeWithoutAddingHistory) {
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        drag();
        EXPECT_NE(x(), 0);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(EditingUiTest, ConfiguredShortcutReplacesDefaultAndKeepsContextGuards) {
        auto parsed = EditorShortcuts::parse(
            R"({"editor": {"shortcuts": {"scene.save": ["Primary+Shift+S"]}}})");
        ASSERT_TRUE(parsed);
        shortcuts = std::move(parsed).value();
        auto& io = ImGui::GetIO();
        const auto modifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
        frame();
        frame();
        io.AddKeyEvent(modifier, true);
        io.AddKeyEvent(ImGuiKey_S, true);
        frame();
        EXPECT_FALSE(menu.take_request());
        io.AddKeyEvent(ImGuiKey_S, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Shift, true);
        io.AddKeyEvent(ImGuiKey_S, true);
        frame();
        EXPECT_EQ(menu.take_request(), (MenuBar::Request{MenuBar::Command::SaveScene, {}}));
        EXPECT_FALSE(menu.take_request());
        io.AddKeyEvent(ImGuiKey_S, false);
        frame();
        state.mode = EditorMode::Play;
        io.AddKeyEvent(ImGuiKey_S, true);
        frame();
        EXPECT_FALSE(menu.take_request());
        io.AddKeyEvent(ImGuiKey_S, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        io.AddKeyEvent(modifier, false);
        state.mode = EditorMode::Edit;
        frame();
        focus_text_input();
        io.AddKeyEvent(modifier, true);
        io.AddKeyEvent(ImGuiMod_Shift, true);
        io.AddKeyEvent(ImGuiKey_S, true);
        frame();
        EXPECT_FALSE(menu.take_request());
    }

    TEST_F(EditingUiTest, ShortcutsUsePlatformModifierAndConsumeRequestOnce) {
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "transform", "translation"}));
        ASSERT_TRUE(edit.preview(Comet::Math::Vec3(2)));
        ASSERT_TRUE(edit.commit());
        auto& io = ImGui::GetIO();
        for(const bool mac : {false, true}) {
            io.ConfigMacOSXBehaviors = mac;
            frame();
            frame();
            io.AddKeyEvent(mac ? ImGuiMod_Super : ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_Z, true);
            frame();
            EXPECT_EQ(menu.take_request(), (MenuBar::Request{MenuBar::Command::Undo, {}}));
            EXPECT_FALSE(menu.take_request());
            io.AddKeyEvent(mac ? ImGuiMod_Super : ImGuiMod_Ctrl, false);
            io.AddKeyEvent(ImGuiKey_Z, false);
            frame();
        }
    }

    TEST_F(EditingUiTest, EntityCopyPasteShortcutsRespectPlatformAndTextInput) {
        auto& io = ImGui::GetIO();
        for(const bool mac : {false, true}) {
            io.ConfigMacOSXBehaviors = mac;
            frame();
            frame();
            const auto modifier = mac ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(modifier, true);
            io.AddKeyEvent(ImGuiKey_C, true);
            frame();
            EXPECT_EQ(menu.take_request(), (MenuBar::Request{MenuBar::Command::CopyEntity, {}}));
            EXPECT_FALSE(menu.take_request());
            io.AddKeyEvent(ImGuiKey_C, false);
            frame();
            io.AddKeyEvent(ImGuiKey_V, true);
            frame();
            EXPECT_EQ(menu.take_request(), (MenuBar::Request{MenuBar::Command::PasteEntity, {}}));
            io.AddKeyEvent(ImGuiKey_V, false);
            io.AddKeyEvent(modifier, false);
            frame();
        }

        io.ConfigMacOSXBehaviors = false;
        focus_text_input();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_C, true);
        frame();
        EXPECT_FALSE(menu.take_request());
        io.AddKeyEvent(ImGuiKey_C, false);
        io.AddKeyEvent(ImGuiKey_V, true);
        frame();
        EXPECT_FALSE(menu.take_request());
        io.AddKeyEvent(ImGuiKey_V, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
    }

    TEST_F(EditingUiTest, EntityDeleteShortcutRespectsPlatformAndTextInput) {
        auto& io = ImGui::GetIO();
        for(const bool mac : {false, true}) {
            io.ConfigMacOSXBehaviors = mac;
            frame();
            frame();
            const auto modifier = mac ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(modifier, true);
            io.AddKeyEvent(ImGuiKey_Backspace, true);
            frame();
            EXPECT_EQ(
                menu.take_request(), (MenuBar::Request{MenuBar::Command::DeleteSelection, {}}));
            EXPECT_FALSE(menu.take_request());
            io.AddKeyEvent(ImGuiKey_Backspace, false);
            io.AddKeyEvent(modifier, false);
            frame();
        }

        io.ConfigMacOSXBehaviors = true;
        focus_text_input();
        io.AddKeyEvent(ImGuiMod_Super, true);
        io.AddKeyEvent(ImGuiKey_Backspace, true);
        frame();
        EXPECT_FALSE(menu.take_request());
        io.AddKeyEvent(ImGuiKey_Backspace, false);
        io.AddKeyEvent(ImGuiMod_Super, false);
    }

    TEST(MenuBarTest, StartupSceneMenuSupportsCurrentUnindexedScene) {
        Comet::Tests::ImGuiTestContext imgui;
        EditorState state;
        CommandHistory history;
        EditorShortcuts shortcuts;
        MenuBar menu(state, history, shortcuts);
        const auto frame = [&](const std::filesystem::path& current,
                               const std::filesystem::path& startup) {
            ImGui::NewFrame();
            menu.render(current, startup);
            ImGui::Render();
        };
        const auto open_project_menu = [&] {
            const auto* bar = ImGui::FindWindowByName("##MainMenuBar");
            ASSERT_NE(bar, nullptr);
            ImGui::ActivateItemByID(ImHashStr("Project", 0, ImHashStr("##MenuBar", 0, bar->ID)));
        };

        frame("scenes/current.scene", "scenes/other.scene");
        open_project_menu();
        frame("scenes/current.scene", "scenes/other.scene");
        frame("scenes/current.scene", "scenes/other.scene");
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Startup Scene"));
        frame("scenes/current.scene", "scenes/other.scene");
        frame("scenes/current.scene", "scenes/other.scene");
        popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("scenes/current.scene"));
        frame("scenes/current.scene", "scenes/other.scene");
        EXPECT_EQ(menu.take_request(),
            (MenuBar::Request{MenuBar::Command::SetStartupScene, "scenes/current.scene"}));
        EXPECT_FALSE(menu.take_request());
    }

    TEST(MenuBarTest, SceneSwitchSelectsOtherProjectSceneAndKeepsCurrentSceneDisabled) {
        Comet::Tests::ImGuiTestContext imgui;
        EditorState state;
        CommandHistory history;
        EditorShortcuts shortcuts;
        MenuBar menu(state, history, shortcuts);
        const std::filesystem::path current = "scenes/main.scene";
        const std::filesystem::path other = "levels/main.scene";
        menu.set_available_scenes({current, other});
        const auto frame = [&] {
            ImGui::NewFrame();
            menu.render(current, current);
            ImGui::Render();
        };
        frame();
        const auto* bar = ImGui::FindWindowByName("##MainMenuBar");
        ASSERT_NE(bar, nullptr);
        ImGui::ActivateItemByID(ImHashStr("File", 0, ImHashStr("##MenuBar", 0, bar->ID)));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Switch Scene"));
        frame();
        frame();
        ASSERT_EQ(GImGui->OpenPopupStack.Size, 2);
        popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID(current.generic_string().c_str()));
        frame();
        EXPECT_FALSE(menu.take_request());
        ImGui::ActivateItemByID(popup->GetID(other.generic_string().c_str()));
        frame();
        EXPECT_EQ(menu.take_request(), (MenuBar::Request{MenuBar::Command::OpenScene, other}));
        EXPECT_FALSE(menu.take_request());
    }

    TEST(MenuBarTest, RecentProjectSelectionProvidesPath) {
        Comet::Tests::ImGuiTestContext imgui;
        EditorState state;
        CommandHistory history;
        EditorShortcuts shortcuts;
        MenuBar menu(state, history, shortcuts);
        const std::filesystem::path project = "/projects/example";
        const std::vector recent{project};
        const auto frame = [&] {
            ImGui::NewFrame();
            menu.render({}, {}, recent);
            ImGui::Render();
        };

        frame();
        const auto* bar = ImGui::FindWindowByName("##MainMenuBar");
        ASSERT_NE(bar, nullptr);
        ImGui::ActivateItemByID(ImHashStr("File", 0, ImHashStr("##MenuBar", 0, bar->ID)));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Recent Projects"));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID(project.generic_string().c_str()));
        frame();
        EXPECT_EQ(menu.take_request(), (MenuBar::Request{MenuBar::Command::OpenProject, project}));
        EXPECT_FALSE(menu.take_request());
    }

    TEST(MenuBarTest, NewProjectOpensFromFileMenu) {
        Comet::Tests::ImGuiTestContext imgui;
        EditorState state;
        CommandHistory history;
        EditorShortcuts shortcuts;
        MenuBar menu(state, history, shortcuts);
        const auto frame = [&] {
            ImGui::NewFrame();
            menu.render();
            ImGui::Render();
        };
        frame();
        const auto* bar = ImGui::FindWindowByName("##MainMenuBar");
        ASSERT_NE(bar, nullptr);
        ImGui::ActivateItemByID(ImHashStr("File", 0, ImHashStr("##MenuBar", 0, bar->ID)));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("New Project"));
        frame();
        EXPECT_EQ(menu.take_request(), (MenuBar::Request{MenuBar::Command::NewProject, {}}));
    }

    TEST(MenuBarTest, ProjectRenameOpensFromProjectMenu) {
        Comet::Tests::ImGuiTestContext imgui;
        EditorState state;
        CommandHistory history;
        EditorShortcuts shortcuts;
        MenuBar menu(state, history, shortcuts);
        const auto frame = [&] {
            ImGui::NewFrame();
            menu.render();
            ImGui::Render();
        };
        frame();
        const auto* bar = ImGui::FindWindowByName("##MainMenuBar");
        ASSERT_NE(bar, nullptr);
        ImGui::ActivateItemByID(ImHashStr("Project", 0, ImHashStr("##MenuBar", 0, bar->ID)));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Rename Project..."));
        frame();
        EXPECT_EQ(menu.take_request(), (MenuBar::Request{MenuBar::Command::RenameProject, {}}));
    }

    TEST(MenuBarTest, ProjectInputSettingsOpensFromSettingsMenu) {
        Comet::Tests::ImGuiTestContext imgui;
        EditorState state;
        CommandHistory history;
        EditorShortcuts shortcuts;
        MenuBar menu(state, history, shortcuts);
        const auto frame = [&] {
            ImGui::NewFrame();
            menu.render();
            ImGui::Render();
        };
        frame();
        const auto* bar = ImGui::FindWindowByName("##MainMenuBar");
        ASSERT_NE(bar, nullptr);
        ImGui::ActivateItemByID(ImHashStr("Project", 0, ImHashStr("##MenuBar", 0, bar->ID)));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Settings"));
        frame();
        frame();
        popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Input"));
        frame();
        EXPECT_EQ(
            menu.take_request(), (MenuBar::Request{MenuBar::Command::ProjectInputSettings, {}}));
    }

    TEST(MenuBarTest, StartupSceneCanBeChosenWithoutOpeningIt) {
        Comet::Tests::ImGuiTestContext imgui;
        EditorState state;
        CommandHistory history;
        EditorShortcuts shortcuts;
        MenuBar menu(state, history, shortcuts);
        const std::filesystem::path scene = "scenes/other.scene";
        const std::vector available{scene};
        menu.set_available_scenes(available);
        const auto frame = [&] {
            ImGui::NewFrame();
            menu.render();
            ImGui::Render();
        };
        frame();
        const auto* bar = ImGui::FindWindowByName("##MainMenuBar");
        ASSERT_NE(bar, nullptr);
        ImGui::ActivateItemByID(ImHashStr("Project", 0, ImHashStr("##MenuBar", 0, bar->ID)));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Startup Scene"));
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID(scene.generic_string().c_str()));
        frame();
        EXPECT_EQ(
            menu.take_request(), (MenuBar::Request{MenuBar::Command::SetStartupScene, scene}));
        EXPECT_FALSE(menu.take_request());
    }
}
#endif
