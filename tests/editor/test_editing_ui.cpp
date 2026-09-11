#ifdef COMET_TEST_EDITOR_UI
#include "command_history.h"
#include "scene_commands.h"
#include "menu_bar.h"
#include "panels/inspector.h"
#include "panels/hierarchy.h"
#include "property_editor_registry.h"
#include "selection.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <string_view>
#include <vector>

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
        float inspector_width = 700;
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
                components, widgets, assets,
                Comet::ProjectPaths(PROJECT_ROOT_DIR).assets(), nullptr, nullptr,
                prepare_asset);
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
            ImGui::SetNextWindowSize(ImVec2(inspector_width, 500));
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
            ASSERT_EQ(bounds.size(), 3);
            for(const auto& [end, field_width] : bounds) {
                EXPECT_LE(end, window->WorkRect.Max.x + 1);
                EXPECT_GT(field_width, 0);
                EXPECT_LE(field_width, ImGui::GetFontSize() * 9 + 1);
            }
        }
    }

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
            add_asset(
                "b.mat", "version: 1\ntemplate: unlit_texture_blend\nproperties: {}\n");
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

    class HierarchyUiTest: public ::testing::Test {
    protected:
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity();
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        CommandHistory history;
        SelectionService selection{scene};
        EditorState state;
        HierarchyPanel hierarchy{scene, selection, history, state};

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
            draw();
            draw();
        }
        void TearDown() override { ImGui::DestroyContext(); }
        void draw() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 400));
            hierarchy.render();
            ImGui::Render();
        }
    };

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
        hierarchy.set_scene(scene);
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
