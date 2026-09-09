#ifdef COMET_TEST_EDITOR_UI
#include "command_history.h"
#include "menu_bar.h"
#include "panels/inspector.h"
#include "property_editor_registry.h"
#include "selection.h"

#include <gtest/gtest.h>
#include <imgui.h>

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
        EditorShortcuts shortcuts;
        MenuBar menu{state, history, shortcuts};
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

    TEST_F(EditingUiTest, ConfiguredShortcutReplacesDefaultAndKeepsContextGuards) {
        shortcuts = EditorShortcuts::parse(
            "editor: {shortcuts: {scene.save: [Primary+Shift+S]}}");
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
        type_name("Editing");
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
}
#endif
