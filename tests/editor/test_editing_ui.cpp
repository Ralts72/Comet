#ifdef COMET_TEST_EDITOR_UI
#include "command_history.h"
#include "menu_bar.h"
#include "panels/inspector.h"
#include "panels/hierarchy.h"
#include "panels/project.h"
#include "asset/serialization/metadata_serializer.h"
#include "scene_commands.h"
#include "property_editor_registry.h"
#include "selection.h"

#include <gtest/gtest.h>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>

namespace CometEditor::Tests {
    class EditingUiTest: public ::testing::Test {
    protected:
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity();
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        Comet::AssetDatabase assets{Comet::ProjectPaths(PROJECT_ROOT_DIR)};
        CommandHistory history;
        PropertyEditTransaction edit{history, components};
        SelectionService selection{scene};
        PropertyEditorRegistry widgets;
        EditorState state;
        MenuBar menu{state, history};
        std::unique_ptr<InspectorPanel> inspector;
        ImVec2 drag_point{};
        ImVec2 name_point{};

        void SetUp() override {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(800, 600);
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::String,
                [this, builtin = create_property_editor_registry()](
                    const Comet::PropertyDescriptor& property, void* value) {
                    const bool changed = builtin.edit_property(property, value);
                    const auto start = ImGui::GetItemRectMin();
                    name_point = ImVec2(start.x + 20, start.y + 8);
                    return changed;
                }));
            ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Vec3,
                [this](const Comet::PropertyDescriptor& property, void* value) {
                    auto& vector = *static_cast<Comet::Math::Vec3*>(value);
                    const bool changed =
                        ImGui::DragFloat3(property.display_name.c_str(), &vector.x, 0.1f);
                    if(property.id == "translation") {
                        const auto start = ImGui::GetItemRectMin();
                        drag_point = ImVec2(start.x + 20, start.y + 8);
                    }
                    return changed;
                }));
            inspector = std::make_unique<InspectorPanel>(selection, history, edit,
                components, widgets, assets,
                Comet::ProjectPaths(PROJECT_ROOT_DIR).assets(), nullptr, nullptr);
            frame();
            frame();
        }

        void TearDown() override {
            inspector.reset();
            ImGui::DestroyContext();
        }
        void frame() {
            ImGui::NewFrame();
            menu.render();
            ImGui::SetNextWindowPos(ImVec2(20, 40));
            ImGui::SetNextWindowSize(ImVec2(700, 500));
            inspector->render();
            menu.collect_shortcuts();
            ImGui::Render();
        }
        float x() {
            return entity.get_component<Comet::TransformComponent>().translation.x;
        }
        void type_name(const std::string& text) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(name_point.x, name_point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            const auto modifier =
                io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(modifier, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(modifier, false);
            frame();
            io.AddInputCharactersUTF8(text.c_str());
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

    TEST_F(EditingUiTest, ProjectImportIsAHandleRequestAndBusyStatesDisableIt) {
        struct TemporaryAssets {
            std::filesystem::path root =
                std::filesystem::temp_directory_path()
                / ("comet_import_ui_"
                    + std::to_string(Comet::AssetHandle::generate().value()));
            ~TemporaryAssets() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } temporary;
        Comet::ProjectPaths paths(temporary.root);
        std::filesystem::create_directories(paths.assets());
        const auto source = paths.assets() / "mesh.gltf";
        {
            std::ofstream output(source);
            output << "{}";
        }
        const Comet::AssetHandle handle(42);
        Comet::AssetMetadataSerializer{}.save(
            {.handle = handle, .type = Comet::AssetType::Mesh},
            Comet::metadata_path(source));
        Comet::AssetDatabase database(paths);
        auto report = database.scan();
        ASSERT_TRUE(report.succeeded());
        selection.select_asset(handle);
        ProjectPanel project(database, report, nullptr, nullptr, selection);
        const auto draw = [&]() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(600, 400));
            project.render();
            ImGui::Render();
        };
        draw();
        draw();
        auto* window = ImGui::FindWindowByName("Project");
        ASSERT_NE(window, nullptr);
        using State = Comet::AssetManager::MeshImportState;
        for(const auto state : {State::Checking, State::Importing}) {
            project.set_mesh_import_state(handle, state);
            ImGui::ActivateItemByID(window->GetID("###MeshImport"));
            draw();
            EXPECT_FALSE(project.take_mesh_import_request());
        }
        for(const auto state :
            {State::Missing, State::Stale, State::Ready, State::Failed}) {
            project.set_mesh_import_state(handle, state);
            ImGui::ActivateItemByID(window->GetID("###MeshImport"));
            draw();
            const auto request = project.take_mesh_import_request();
            ASSERT_TRUE(request);
            EXPECT_EQ(*request, handle);
            EXPECT_FALSE(project.take_mesh_import_request());
            EXPECT_FALSE(std::filesystem::exists(paths.cache()));
        }
    }

    TEST_F(EditingUiTest, HierarchyQueuesOneRequestWithoutMutatingDuringUiTraversal) {
        HierarchyPanel hierarchy(scene, selection, history);
        const auto draw = [&]() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 400));
            hierarchy.render();
            ImGui::Render();
        };
        draw();
        draw();
        auto* window = ImGui::FindWindowByName("Hierarchy");
        ASSERT_NE(window, nullptr);
        ImGui::ActivateItemByID(window->GetID("+"));
        draw();
        EXPECT_EQ(scene.entity_count(), 1);
        auto request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Create);
        EXPECT_EQ(request->generation, history.generation());
        EXPECT_FALSE(hierarchy.take_request());
        ImGui::ActivateItemByID(window->GetID("-"));
        draw();
        request = hierarchy.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->type, HierarchyPanel::Request::Type::Delete);
        EXPECT_EQ(request->entity, entity.get_uuid());
        EXPECT_TRUE(entity);
        history.bind_scene(nullptr);
        ImGui::ActivateItemByID(window->GetID("+"));
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        ImGui::ActivateItemByID(window->GetID("-"));
        draw();
        EXPECT_FALSE(hierarchy.take_request());
        history.bind_scene(&scene);
        ImGui::ActivateItemByID(window->GetID("+"));
        draw();
        hierarchy.set_scene(scene);
        EXPECT_FALSE(hierarchy.take_request());
    }

    TEST_F(EditingUiTest, HierarchyContextMenuQueuesDuplicateForClickedEntity) {
        HierarchyPanel hierarchy(scene, selection, history);
        const auto draw = [&]() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 400));
            hierarchy.render();
            ImGui::Render();
        };
        draw();
        draw();
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
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        frame();
        ImGui::ActivateItemByID(window->GetID("Add Component"));
        frame();
        EXPECT_EQ(context.OpenPopupStack.Size, 0);
        EXPECT_FALSE(entity.has_component<Comet::MeshRendererComponent>());
    }

    TEST_F(EditingUiTest, ComponentHeaderContextMenuRemovesAndRestoresCamera) {
        entity.add_component<Comet::CameraComponent>().fov = 63;
        frame();
        auto* window = ImGui::FindWindowByName("Inspector");
        ASSERT_NE(window, nullptr);
        // 此 fixture 未注册 Bool/Float 控件，Camera 标题紧邻 Add 按钮上方。
        const ImVec2 header(window->WorkRect.Min.x + 30,
            window->DC.CursorPosPrevLine.y - ImGui::GetStyle().ItemSpacing.y
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
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 63);
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

    TEST_F(EditingUiTest, NameInputCommitsOneRecordWithoutTruncatingUtf8) {
        const std::string name = "新名称" + std::string(512, 'n');
        type_name(name);
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, name);
        EXPECT_FALSE(history.can_undo());
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "Entity");
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, name);
    }

    TEST_F(EditingUiTest, EscapeRestoresNameAndPlayDoesNotRecordNameChanges) {
        type_name("Cancelled");
        ASSERT_EQ(entity.get_component<Comet::NameComponent>().name, "Cancelled");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "Entity");
        EXPECT_FALSE(history.can_undo());
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        type_name("Runtime");
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "Runtime");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        EXPECT_FALSE(history.can_undo());
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

    TEST_F(EditingUiTest, PlayEditsRuntimeWithoutAddingHistory) {
        history.bind_scene(nullptr);
        state.mode = EditorMode::Play;
        drag();
        EXPECT_NE(x(), 0);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FALSE(history.can_undo());
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
}
#endif
