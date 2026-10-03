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
#include "scripting/script.h"
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
        Comet::MaterialPrograms programs{runtime_assets};
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
        ImVec2 text_point{};
        float inspector_width = 700;
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
            ImGui::SetNextWindowSize(ImVec2(inspector_width, 500));
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
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::ScriptSystem>(runtime_assets)));
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
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::ScriptSystem>(runtime_assets)));
        ASSERT_TRUE(runtime.start(*cloned.value(), Comet::SceneRuntime::State::Paused));
        state.mode = EditorMode::Play;
        selection.set_scene(*cloned.value());
        selection.select_entity(runtime_entity.get_id());
        ImVec2 label_point;
        float displayed = 0;
        ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Float,
            [&, builtin = create_property_editor_registry(assets)](
                const Comet::PropertyDescriptor& property, void* value) {
                displayed = *static_cast<float*>(value);
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
        EXPECT_EQ(binding.running_script(), script.value());
        EXPECT_FLOAT_EQ(std::get<float>(authored.parameters.at("speed")), 12);
        EXPECT_EQ(history.state_id(), history_state);
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(edit.active());
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        frame();
        EXPECT_FLOAT_EQ(displayed, 250);
        EXPECT_EQ(binding.running_script(), candidate.value());
        EXPECT_FALSE(binding.parameters.contains("speed"));
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
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::ScriptSystem>(runtime_assets)));
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
        EXPECT_EQ(runtime_binding.running_script(), script.value());
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
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::ScriptSystem>(runtime_assets)));
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
        EXPECT_EQ(binding.running_script(), original.value());
        EXPECT_TRUE(displayed_string.empty());
        EXPECT_NE(ImGui::GetActiveID(), 0u);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(binding.running_script(), changed.value());
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
        EXPECT_FALSE(binding.running_script());
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
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::ScriptSystem>(runtime_assets)));
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
                const float field_width = ImGui::GetItemRectSize().x
                                          - ImGui::CalcTextSize(property.display_name.c_str()).x
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
        entity.add_component<Comet::CameraComponent>().fov = 63;
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
        const float view_x = ImGui::CalcTextSize("File").x + ImGui::CalcTextSize("Project").x
                             + ImGui::CalcTextSize("Edit").x + 6.0f * style.ItemSpacing.x + 10.0f;
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
        auto parsed =
            EditorShortcuts::parse("editor: {shortcuts: {scene.save: [Primary+Shift+S]}}");
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
