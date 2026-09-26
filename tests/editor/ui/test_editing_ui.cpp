#ifdef COMET_TEST_EDITOR_UI
#include "scene/command_history.h"
#include "scene/scene_commands.h"
#include "ui/menu_bar.h"
#include "inspector/inspector.h"
#include "scene/hierarchy.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "asset/registry.h"
#include "render/material/material_programs.h"
#include "scripting/script.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include "scene/scene_runtime.h"

#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>
#include <vector>

namespace CometEditor::Tests {
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
        ImVec2 drag_point{};
        ImVec2 text_point{};
        float inspector_width = 700;
        bool draw_trailing_item = false;
        bool show_text_input = false;
        char text_buffer[32]{};

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
            menu.render();
            ImGui::SetNextWindowPos(ImVec2(20, 40));
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
        void drag() {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(drag_point.x, drag_point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMousePosEvent(drag_point.x + 30, drag_point.y);
            frame();
            io.AddMousePosEvent(drag_point.x + 60, drag_point.y);
            frame();
        }
    };

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
    }

    TEST_F(EditingUiTest, PlayParameterEditorUsesActiveVersionNotReloadedAsset) {
        const Comet::AssetHandle handle{1234};
        auto original = Comet::Script::create("return {properties = {speed = 100}}");
        ASSERT_TRUE(original);
        ASSERT_TRUE(runtime_assets.register_asset(handle, original.value()));
        auto& binding = entity.add_component<Comet::ScriptComponent>();
        binding.asset = handle;
        Comet::SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::ScriptSystem>(runtime_assets)));
        ASSERT_TRUE(runtime.start(scene));
        state.mode = EditorMode::Play;
        auto changed = Comet::Script::create("return {properties = {speed = 'new type'}}");
        ASSERT_TRUE(changed);
        ASSERT_TRUE(runtime_assets.replace_asset(handle, changed.value()));
        bool rendered = false;
        ASSERT_TRUE(widgets.register_editor(
            Comet::PropertyType::Float, [&](const Comet::PropertyDescriptor&, void* value) {
                rendered = true;
                EXPECT_EQ(*static_cast<float*>(value), 100);
                *static_cast<float*>(value) = 25;
                return PropertyEditResult{.changed = true, .finished = true};
            }));
        const auto before = history.state_id();
        frame();
        EXPECT_TRUE(rendered);
        EXPECT_EQ(history.state_id(), before);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(binding.running_script(), original.value());
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(binding.running_script());
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
        const float view_x = ImGui::CalcTextSize("File").x + ImGui::CalcTextSize("Edit").x
                             + 4.0f * style.ItemSpacing.x + 10.0f;
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
        EXPECT_FALSE(menu.take_command());
        io.AddKeyEvent(ImGuiKey_S, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Shift, true);
        io.AddKeyEvent(ImGuiKey_S, true);
        frame();
        EXPECT_EQ(menu.take_command(), MenuBar::Command::SaveScene);
        EXPECT_FALSE(menu.take_command());
        io.AddKeyEvent(ImGuiKey_S, false);
        frame();
        state.mode = EditorMode::Play;
        io.AddKeyEvent(ImGuiKey_S, true);
        frame();
        EXPECT_FALSE(menu.take_command());
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
        EXPECT_FALSE(menu.take_command());
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
            EXPECT_EQ(menu.take_command(), MenuBar::Command::Undo);
            EXPECT_FALSE(menu.take_command());
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
            EXPECT_EQ(menu.take_command(), MenuBar::Command::CopyEntity);
            EXPECT_FALSE(menu.take_command());
            io.AddKeyEvent(ImGuiKey_C, false);
            frame();
            io.AddKeyEvent(ImGuiKey_V, true);
            frame();
            EXPECT_EQ(menu.take_command(), MenuBar::Command::PasteEntity);
            io.AddKeyEvent(ImGuiKey_V, false);
            io.AddKeyEvent(modifier, false);
            frame();
        }

        io.ConfigMacOSXBehaviors = false;
        focus_text_input();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_C, true);
        frame();
        EXPECT_FALSE(menu.take_command());
        io.AddKeyEvent(ImGuiKey_C, false);
        io.AddKeyEvent(ImGuiKey_V, true);
        frame();
        EXPECT_FALSE(menu.take_command());
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
            EXPECT_EQ(menu.take_command(), MenuBar::Command::DeleteSelection);
            EXPECT_FALSE(menu.take_command());
            io.AddKeyEvent(ImGuiKey_Backspace, false);
            io.AddKeyEvent(modifier, false);
            frame();
        }

        io.ConfigMacOSXBehaviors = true;
        focus_text_input();
        io.AddKeyEvent(ImGuiMod_Super, true);
        io.AddKeyEvent(ImGuiKey_Backspace, true);
        frame();
        EXPECT_FALSE(menu.take_command());
        io.AddKeyEvent(ImGuiKey_Backspace, false);
        io.AddKeyEvent(ImGuiMod_Super, false);
    }

    TEST(MenuBarTest, StartupSceneCommandsRequireSavedScene) {
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
        const auto open_file_menu = [&] {
            const auto* bar = ImGui::FindWindowByName("##MainMenuBar");
            ASSERT_NE(bar, nullptr);
            ImGui::ActivateItemByID(ImHashStr("File", 0, ImHashStr("##MenuBar", 0, bar->ID)));
        };

        frame({}, {});
        open_file_menu();
        frame({}, {});
        frame({}, {});
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Set Current Scene as Startup"));
        frame({}, {});
        EXPECT_FALSE(menu.take_command());

        ImGui::ActivateItemByID(popup->GetID("Set Current Scene as Startup"));
        frame("scenes/current.scene", "scenes/other.scene");
        EXPECT_EQ(menu.take_command(), MenuBar::Command::SetStartupScene);

        frame("scenes/current.scene", "scenes/other.scene");
        open_file_menu();
        frame("scenes/current.scene", "scenes/other.scene");
        frame("scenes/current.scene", "scenes/other.scene");
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Clear Startup Scene"));
        frame("scenes/current.scene", "scenes/other.scene");
        EXPECT_EQ(menu.take_command(), MenuBar::Command::ClearStartupScene);
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
        EXPECT_EQ(menu.take_command(), MenuBar::Command::OpenProject);
        EXPECT_EQ(menu.take_project_path(), project);
    }

    TEST(MenuBarTest, ProjectRenameOpensFromFileMenu) {
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
        ImGui::ActivateItemByID(popup->GetID("Rename Project..."));
        frame();
        EXPECT_EQ(menu.take_command(), MenuBar::Command::RenameProject);
    }
}
#endif
