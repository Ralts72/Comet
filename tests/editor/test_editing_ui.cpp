#ifdef COMET_TEST_EDITOR_UI
#include "command_history.h"
#include "menu_bar.h"
#include "panels/inspector.h"
#include "property_editor_registry.h"
#include "selection.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>

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
        InspectorPanel::PrepareAsset prepare_asset;
        bool draw_trailing_item = false;

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
                [this, builtin = create_property_editor_registry(assets)](
                    const Comet::PropertyDescriptor& property, void* value) {
                    const auto result = builtin.edit_property(property, value);
                    const auto start = ImGui::GetItemRectMin();
                    name_point = ImVec2(start.x + 20, start.y + 8);
                    return result;
                }));
            ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::Vec3,
                [this](const Comet::PropertyDescriptor& property, void* value) {
                    auto& vector = *static_cast<Comet::Math::Vec3*>(value);
                    const bool changed =
                        ImGui::DragFloat3(property.display_name.c_str(), &vector.x, 0.1f);
                    const auto result = PropertyEditResult::from_item(changed);
                    if(property.id == "translation") {
                        const auto start = ImGui::GetItemRectMin();
                        drag_point = ImVec2(start.x + 20, start.y + 8);
                    }
                    if(draw_trailing_item)
                        ImGui::TextUnformatted("Extra widget content");
                    return result;
                }));
            inspector =
                std::make_unique<InspectorPanel>(state, selection, edit, components,
                    widgets, assets, Comet::ProjectPaths(PROJECT_ROOT_DIR).assets(),
                    nullptr, nullptr, prepare_asset);
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

    class AssetReferenceUiTest: public EditingUiTest {
    protected:
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_asset_picker_"
                + std::to_string(Comet::AssetHandle::generate().value()));
        ImVec2 mesh_point{};
        ImVec2 material_point{};
        bool load_succeeds = true;
        int load_count = 0;
        Comet::AssetHandle last_loaded;

        void add_asset(const std::filesystem::path& path, const char* contents) {
            const auto source = Comet::ProjectPaths(root).assets() / path;
            std::filesystem::create_directories(source.parent_path());
            std::ofstream(source) << contents;
        }

        void SetUp() override {
            add_asset("a.png", "texture");
            add_asset("b.mat", "version: 1\ntemplate: cube_texture\nproperties: {}\n");
            add_asset("one/shared.gltf", R"({"asset":{"version":"2.0"}})");
            add_asset("two/shared.gltf", R"({"asset":{"version":"2.0"}})");
            assets = Comet::AssetDatabase(Comet::ProjectPaths(root));
            ASSERT_TRUE(assets.scan().succeeded());
            entity.add_component<Comet::MeshRendererComponent>();
            prepare_asset = [this](Comet::AssetHandle handle, Comet::AssetType) {
                ++load_count;
                last_loaded = handle;
                return load_succeeds;
            };
            EditingUiTest::SetUp();
            ASSERT_TRUE(widgets.register_editor(Comet::PropertyType::AssetHandle,
                [this, builtin = create_property_editor_registry(assets)](
                    const Comet::PropertyDescriptor& property, void* value) {
                    const auto result = builtin.edit_property(property, value);
                    const auto start = ImGui::GetItemRectMin();
                    auto& point = property.id == "mesh" ? mesh_point : material_point;
                    point = {start.x + 20, start.y + 8};
                    return result;
                }));
            frame();
            frame();
        }

        void TearDown() override {
            EditingUiTest::TearDown();
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        void click(ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        }

        void choose(ImVec2 point, int row) {
            click(point);
            frame();
            const auto* popup = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(popup, nullptr);
            ASSERT_TRUE(popup->Active);
            click({popup->DC.CursorStartPos.x + 20,
                popup->DC.CursorStartPos.y + row * ImGui::GetTextLineHeightWithSpacing()
                    + ImGui::GetTextLineHeight() * 0.5f});
            frame();
        }

        Comet::MeshRendererComponent& renderer() {
            return entity.get_component<Comet::MeshRendererComponent>();
        }
    };

    TEST_F(AssetReferenceUiTest, MeshSelectionFiltersTypesAndSupportsUndoRedo) {
        choose(mesh_point, 1);
        const auto selected = assets.find("one/shared.gltf")->handle;
        EXPECT_EQ(renderer().mesh, selected);
        EXPECT_EQ(last_loaded, selected);
        EXPECT_EQ(load_count, 1);
        ASSERT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(renderer().mesh.is_valid());
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(renderer().mesh, selected);
        EXPECT_EQ(load_count, 1);
    }

    TEST_F(AssetReferenceUiTest, SameFilenameInDifferentFoldersKeepsDistinctIdentity) {
        choose(mesh_point, 2);
        EXPECT_EQ(renderer().mesh, assets.find("two/shared.gltf")->handle);
        EXPECT_NE(renderer().mesh, assets.find("one/shared.gltf")->handle);
    }

    TEST_F(AssetReferenceUiTest, MaterialSelectionFiltersTypes) {
        choose(material_point, 1);
        EXPECT_EQ(renderer().material, assets.find("b.mat")->handle);
        EXPECT_EQ(history.undo_size(), 1);
    }

    TEST_F(AssetReferenceUiTest, FailedLoadPreservesReferenceAndHistory) {
        const auto original = assets.find("two/shared.gltf")->handle;
        renderer().mesh = original;
        load_succeeds = false;
        frame();
        choose(mesh_point, 1);
        EXPECT_EQ(renderer().mesh, original);
        EXPECT_EQ(load_count, 1);
        EXPECT_EQ(history.undo_size(), 0);
    }

    TEST_F(AssetReferenceUiTest, MissingReferenceSurvivesFramesAndCanBeClearedWithUndo) {
        const Comet::AssetHandle missing = Comet::AssetHandle::generate();
        renderer().mesh = missing;
        frame();
        frame();
        EXPECT_EQ(renderer().mesh, missing);
        EXPECT_EQ(load_count, 0);
        choose(mesh_point, 0);
        EXPECT_FALSE(renderer().mesh.is_valid());
        EXPECT_EQ(load_count, 0);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(renderer().mesh, missing);
    }

    TEST_F(AssetReferenceUiTest, OpenPickerUsesRefreshedDatabase) {
        add_asset("three/new.gltf", R"({"asset":{"version":"2.0"}})");
        ASSERT_TRUE(assets.scan().succeeded());
        choose(mesh_point, 2);
        EXPECT_EQ(renderer().mesh, assets.find("three/new.gltf")->handle);
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
        ASSERT_EQ(
            popup->PopupId, ImHashStr("View", 0, ImHashStr("##MenuBar", 0, menu_bar_id)));
        click({popup->DC.CursorStartPos.x + 30,
            popup->DC.CursorStartPos.y + ImGui::GetTextLineHeight() * 0.5f});
        EXPECT_TRUE(inspector->is_open());
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
