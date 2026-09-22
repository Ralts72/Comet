#ifdef COMET_TEST_EDITOR_UI
#include "inspector/inspector.h"
#include "render/material/material_layout.h"
#include "assets/project_panel.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "render/material/material.h"
#include "asset/registry.h"

#include "support/imgui_context.h"

#include "support/temporary_directory.h"
#include "support/hdr_image.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>

namespace CometEditor::Tests {
    class AssetEditingUiTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui;
        Comet::Tests::TemporaryDirectory directory;
        const std::filesystem::path root = directory.path();
        Comet::ProjectPaths paths{root};
        Comet::AssetDatabase database{paths};
        Comet::AssetRegistry runtime_assets;
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity();
        Comet::ComponentRegistry registry;
        PropertyEditorRegistry widgets;
        CommandHistory history;
        PropertyEditTransaction edit{history, registry};
        SelectionService selection{scene};
        std::unique_ptr<InspectorPanel> inspector;
        std::unique_ptr<ProjectPanel> project;
        const Comet::AssetHandle mesh{42}, material{43}, texture{44}, second_texture{45};
        AssetDragPayload payload{};
        EditorState state;
        std::size_t payload_size = sizeof(AssetDragPayload);
        bool dragging = false;
        int material_updates = 0;
        bool material_update_success = true;
        Comet::MaterialData submitted_material;

        void SetUp() override {
            std::filesystem::create_directories(paths.assets());
            const auto add_asset = [&](const char* name, Comet::AssetHandle handle,
                                       Comet::AssetType type) {
                std::ofstream(paths.assets() / name) << "{}";
                EXPECT_TRUE(Comet::MetadataSerializer{}.save(
                    {.handle = handle,
                        .type = type,
                        .import_settings = Comet::make_default_import_settings(type)},
                    Comet::metadata_path(paths.assets() / name)));
            };
            add_asset("mesh.gltf", mesh, Comet::AssetType::Mesh);
            add_asset("texture.png", texture, Comet::AssetType::Texture);
            add_asset("second.png", second_texture, Comet::AssetType::Texture);
            add_asset("material.mat", material, Comet::AssetType::Material);
            EXPECT_TRUE(Comet::MaterialSerializer{}.save(
                {.template_name = "pbr", .texture_properties = {{"base_color_texture", texture}}},
                paths.assets() / "material.mat"));
            ASSERT_TRUE(database.scan().succeeded());
            auto builtins = Comet::create_scene_component_registry();
            ASSERT_TRUE(registry.register_component(*builtins.find_component("mesh_renderer")));
            entity.add_component<Comet::MeshRendererComponent>();
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            inspector = std::make_unique<InspectorPanel>(
                state, selection, history, edit, registry, widgets, database, runtime_assets);
            frame();
            frame();
        }

        void TearDown() override { inspector.reset(); }

        void require_texture_pair() {
            auto layout = Comet::MaterialLayout::create(
                "required_textures", {{"base_color_texture", 1, "Base Color Texture", ""},
                                         {"detail_texture", 2, "Detail Texture", ""}});
            ASSERT_TRUE(layout);
            inspector->asset_inspector().set_material_layouts(
                {std::make_shared<Comet::MaterialLayout>(std::move(layout).value())});
            ASSERT_TRUE(Comet::MaterialSerializer{}.save(
                {.template_name = "required_textures"}, paths.assets() / "material.mat"));
        }

        void frame() {
            ImGui::NewFrame();
            if(dragging && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload(
                    AssetDragPayload::TYPE, &payload, payload_size, ImGuiCond_Once);
                ImGui::TextUnformatted("Asset");
                ImGui::EndDragDropSource();
            }
            ImGui::SetNextWindowPos({20, 40});
            ImGui::SetNextWindowSize({project ? 400.0f : 700.0f, 500});
            inspector->render();
            if(project) {
                ImGui::SetNextWindowPos({440, 40});
                ImGui::SetNextWindowSize({340, 500});
                project->render();
            }
            ImGui::Render();
            if(const auto request = inspector->asset_inspector().take_asset_read())
                inspector->asset_inspector().complete_asset_read(
                    *request, Comet::MaterialSerializer{}.load(
                                  paths.assets() / database.find(request->handle)->path));
            if(const auto request = inspector->asset_inspector().take_asset_edit()) {
                EXPECT_EQ(request->handle, material);
                EXPECT_EQ(request->revision, database.get_revision(material));
                const auto* update = std::get_if<MaterialEdit>(&request->value);
                ASSERT_NE(update, nullptr);
                ++material_updates;
                submitted_material = update->after;
                inspector->asset_inspector().complete_asset_edit(*request, material_update_success);
                EXPECT_FALSE(inspector->asset_inspector().take_asset_edit());
            }
        }

        std::optional<InspectorPanel::AssetAssignment> click(ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            return inspector->take_asset_assignment();
        }

        std::optional<InspectorPanel::AssetAssignment> choose(int property, int row) {
            EXPECT_FALSE(click(property_point(property)));
            frame();
            const auto* popup = ImGui::FindWindowByName("##Combo_00");
            EXPECT_NE(popup, nullptr);
            if(!popup || !popup->Active)
                return std::nullopt;
            auto result = click({popup->DC.CursorStartPos.x + 20,
                popup->DC.CursorStartPos.y + row * ImGui::GetTextLineHeightWithSpacing()
                    + ImGui::GetTextLineHeight() * 0.5f});
            frame();
            return result;
        }

        AssetDragPayload drag_asset(Comet::AssetHandle handle, Comet::AssetType type) {
            return {handle, database.get_revision(handle), history.generation(), type};
        }

        ImVec2 property_point(int index) {
            auto* window = ImGui::FindWindowByName("Inspector");
            // 此 fixture 只注册 MeshRenderer：Entity ID、组件标题、两个引用控件。
            return {window->DC.CursorStartPos.x + 50,
                window->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing()
                    + (index + 1) * ImGui::GetFrameHeightWithSpacing()
                    + ImGui::GetFrameHeight() * 0.5f};
        }

        ImVec2 material_point(const char* name, const char* label, const char* child = nullptr) {
            auto* window = ImGui::FindWindowByName("Inspector");
            const ImGuiID group = window->GetID(name);
            ImGuiID id = ImHashStr(label, 0, group);
            if(child)
                id = ImHashStr(child, 0, id);
            for(float y = window->WorkRect.Min.y; y < window->WorkRect.Max.y; y += 4) {
                const ImVec2 point{window->WorkRect.Min.x + 40, y};
                ImGui::GetIO().AddMousePosEvent(point.x, point.y);
                frame();
                if(ImGui::GetCurrentContext()->HoveredId == id)
                    return point;
            }
            ADD_FAILURE() << "Material widget not found: " << name;
            return {};
        }

        void begin_value_drag(ImVec2 point, float distance) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMousePosEvent(point.x + distance / 2, point.y);
            frame();
            io.AddMousePosEvent(point.x + distance, point.y);
            frame();
        }

        void drag_value(ImVec2 point, float distance) {
            begin_value_drag(point, distance);
            auto& io = ImGui::GetIO();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
        }

        ImVec2 widget_point(const char* window_name, const char* label) {
            auto* window = ImGui::FindWindowByName(window_name);
            if(!window) {
                ADD_FAILURE() << "Window not found: " << window_name;
                return {};
            }
            const auto id = window->GetID(label);
            for(float y = window->WorkRect.Min.y; y < window->WorkRect.Max.y; y += 3) {
                const ImVec2 point{window->WorkRect.Min.x + 15, y};
                ImGui::GetIO().AddMousePosEvent(point.x, point.y);
                frame();
                if(ImGui::GetCurrentContext()->HoveredId == id)
                    return point;
            }
            ADD_FAILURE() << "Widget not found: " << window_name << "/" << label;
            return {};
        }

        void select_template(const char* name) {
            click(widget_point("Inspector", "Template"));
            frame();
            click(widget_point("##Combo_00", name));
            frame();
        }

        std::optional<InspectorPanel::AssetAssignment> drop(ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            dragging = true;
            io.AddMouseButtonEvent(0, true);
            frame();
            frame();
            EXPECT_FALSE(inspector->take_asset_assignment());
            io.AddMouseButtonEvent(0, false);
            frame();
            auto request = inspector->take_asset_assignment();
            EXPECT_FALSE(inspector->take_asset_assignment());
            dragging = false;
            frame();
            frame();
            return request;
        }
    };

    TEST_F(AssetEditingUiTest, SceneEnvironmentPickerAndBackgroundUseSceneHistory) {
        Comet::Tests::write_hdr(paths.assets() / "studio.hdr");
        ASSERT_TRUE(database.scan().succeeded());
        const auto environment = database.find("studio.hdr")->handle;
        selection.select_scene();
        frame();
        click(widget_point("Inspector", "HDR map"));
        frame();
        const std::string item = "studio.hdr###" + std::to_string(environment.value());
        click(widget_point("##Combo_00", item.c_str()));
        EXPECT_EQ(scene.get_environment().asset, environment);
        EXPECT_EQ(history.undo_size(), 1u);
        click(widget_point("Inspector", "Background"));
        EXPECT_TRUE(scene.get_environment().background);
        EXPECT_EQ(history.undo_size(), 2u);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(scene.get_environment().background);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(scene.get_environment().asset);
        ASSERT_TRUE(history.redo());
        ASSERT_TRUE(history.redo());
        state.mode = EditorMode::Play;
        frame();
        EXPECT_EQ(scene.get_environment().asset, environment);
        EXPECT_EQ(history.undo_size(), 2u);
    }

    TEST_F(AssetEditingUiTest, BloomTogglePreservesParametersAndPlayIsReadOnly) {
        const Comet::PostProcessSettings settings{
            .exposure = 2, .bloom_enabled = true, .bloom_strength = 0.75f, .bloom_threshold = 3};
        ASSERT_TRUE(scene.set_post_process(settings));
        selection.select_scene();
        frame();
        click(widget_point("Inspector", "Bloom"));
        EXPECT_FALSE(scene.get_post_process().bloom_enabled);
        EXPECT_FLOAT_EQ(scene.get_post_process().bloom_strength, settings.bloom_strength);
        EXPECT_FLOAT_EQ(scene.get_post_process().bloom_threshold, settings.bloom_threshold);
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(scene.get_post_process(), settings);
        ASSERT_TRUE(history.redo());
        state.mode = EditorMode::Play;
        frame();
        const auto before = scene.get_post_process();
        click(widget_point("Inspector", "Bloom"));
        begin_value_drag(widget_point("Inspector", "Exposure"), 30);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(scene.get_post_process(), before);
        EXPECT_EQ(history.undo_size(), 1u);
    }

    TEST_F(AssetEditingUiTest, BloomDragPreviewsCommitsOnceAndEscapeRestoresOnlyCurrentEdit) {
        ASSERT_TRUE(scene.set_post_process({.bloom_enabled = true}));
        selection.select_scene();
        frame();
        auto& io = ImGui::GetIO();
        begin_value_drag(widget_point("Inspector", "Bloom strength"), 30);
        EXPECT_GT(scene.get_post_process().bloom_strength, 0.15f);
        EXPECT_EQ(history.undo_size(), 0u);
        const auto preview = scene.get_post_process();
        io.AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(scene.get_post_process().bloom_strength, 0.15f);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(scene.get_post_process(), preview);

        begin_value_drag(widget_point("Inspector", "Bloom threshold"), 30);
        EXPECT_GT(scene.get_post_process().bloom_threshold, preview.bloom_threshold);
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        io.AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(scene.get_post_process(), preview);
        EXPECT_EQ(history.undo_size(), 1u);
    }

    TEST_F(AssetEditingUiTest, PostProcessNumberInputAndSelectionChangeFinishDocumentEdit) {
        selection.select_scene();
        frame();
        auto& io = ImGui::GetIO();
        const auto point = widget_point("Inspector", "Exposure");
        click(point);
        click(point);
        ASSERT_TRUE(
            ImGui::TempInputIsActive(ImGui::FindWindowByName("Inspector")->GetID("Exposure")));
        io.AddInputCharactersUTF8("2.5");
        frame();
        EXPECT_EQ(history.undo_size(), 0u);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        EXPECT_FLOAT_EQ(scene.get_post_process().exposure, 2.5f);
        EXPECT_EQ(history.undo_size(), 1u);

        for(int index = 0; index < 20; ++index)
            frame();
        begin_value_drag(widget_point("Inspector", "Exposure"), 30);
        EXPECT_GT(scene.get_post_process().exposure, 2.5f);
        selection.select_entity(entity.get_id());
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(history.undo_size(), 2u);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(scene.get_post_process().exposure, 2.5f);
    }

    TEST_F(AssetEditingUiTest, EnvironmentLightingHasIndependentControlsAndOneUndoPerDrag) {
        selection.select_scene();
        frame();
        click(widget_point("Inspector", "Lighting"));
        EXPECT_TRUE(scene.get_environment().lighting);
        EXPECT_FALSE(scene.get_environment().background);
        const auto before = scene.get_environment();
        const auto point = widget_point("Inspector", "Lighting intensity");
        begin_value_drag(point, 30);
        EXPECT_GT(scene.get_environment().lighting_intensity, before.lighting_intensity);
        EXPECT_FLOAT_EQ(scene.get_environment().intensity, before.intensity);
        EXPECT_EQ(history.undo_size(), 1u);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(history.undo_size(), 2u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(scene.get_environment(), before);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(scene.get_environment().lighting);
    }

    TEST_F(AssetEditingUiTest, EnvironmentNumberCommitsOnceAndEscapeDiscardsDraft) {
        selection.select_scene();
        frame();
        auto& io = ImGui::GetIO();
        const auto point = widget_point("Inspector", "Intensity");
        click(point);
        click(point);
        ASSERT_TRUE(
            ImGui::TempInputIsActive(ImGui::FindWindowByName("Inspector")->GetID("Intensity")));
        io.AddInputCharactersUTF8("2");
        frame();
        io.AddInputCharactersUTF8(".5");
        frame();
        EXPECT_EQ(history.undo_size(), 0u);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        EXPECT_FLOAT_EQ(scene.get_environment().intensity, 2.5f);
        EXPECT_EQ(history.undo_size(), 1u);
        for(int index = 0; index < 20; ++index)
            frame();
        click(point);
        click(point);
        io.AddInputCharactersUTF8("9");
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        EXPECT_FLOAT_EQ(scene.get_environment().intensity, 2.5f);
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(scene.get_environment().intensity, 1.0f);
    }

    TEST_F(AssetEditingUiTest, BackgroundColorPreviewsCancelsAndSharesSceneHistory) {
        selection.select_scene();
        frame();
        const auto point = material_point("Background color", "##X");
        begin_value_drag(point, 30);
        const auto preview = scene.get_environment();
        EXPECT_GT(preview.background_color.x, 0);
        EXPECT_EQ(history.undo_size(), 0u);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(history.undo_size(), 1u);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(scene.get_environment().background_color, Comet::Math::Vec3(0));
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(scene.get_environment(), preview);

        begin_value_drag(point, 30);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(scene.get_environment(), preview);
        EXPECT_EQ(history.undo_size(), 1u);
    }

    TEST_F(AssetEditingUiTest, EnvironmentDragPreviewsAndRecordsOneUndoEntry) {
        selection.select_scene();
        frame();
        const auto point = widget_point("Inspector", "Intensity");
        begin_value_drag(point, 30);
        EXPECT_GT(scene.get_environment().intensity, 1.0f);
        EXPECT_EQ(history.undo_size(), 0u);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x + 60, point.y);
        frame();
        const auto preview = scene.get_environment();
        EXPECT_EQ(history.undo_size(), 0u);
        io.AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_EQ(scene.get_environment(), preview);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(scene.get_environment().intensity, 1.0f);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(scene.get_environment(), preview);
    }

    TEST_F(AssetEditingUiTest, EnvironmentRotationWrapsAndEscapeRestoresPreview) {
        auto before = scene.get_environment();
        before.rotation = 179;
        ASSERT_TRUE(scene.set_environment(before));
        selection.select_scene();
        frame();
        begin_value_drag(widget_point("Inspector", "Rotation"), 30);
        EXPECT_LT(scene.get_environment().rotation, 0.0f);
        EXPECT_GE(scene.get_environment().rotation, -180.0f);
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        io.AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(scene.get_environment(), before);
        EXPECT_EQ(history.undo_size(), 0u);
    }

    TEST_F(AssetEditingUiTest, EnvironmentDragFinishesBeforeDocumentActions) {
        selection.select_scene();
        frame();
        begin_value_drag(widget_point("Inspector", "Intensity"), 30);
        const auto preview = scene.get_environment();
        ASSERT_TRUE(inspector->finish_edit());
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_EQ(scene.get_environment(), preview);
        ASSERT_TRUE(history.undo());
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_FLOAT_EQ(scene.get_environment().intensity, 1.0f);
        EXPECT_EQ(history.undo_size(), 0u);
    }

    TEST_F(AssetEditingUiTest, EnvironmentDragCommitsWhenSelectionChanges) {
        selection.select_scene();
        frame();
        begin_value_drag(widget_point("Inspector", "Intensity"), 30);
        const auto preview = scene.get_environment();
        selection.select_entity(entity.get_id());
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_EQ(scene.get_environment(), preview);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(scene.get_environment().intensity, 1.0f);
    }

    TEST_F(AssetEditingUiTest, EnvironmentIntensityClampsAndModeChangeCancelsPreview) {
        auto before = scene.get_environment();
        before.intensity = 63.9f;
        ASSERT_TRUE(scene.set_environment(before));
        selection.select_scene();
        frame();
        begin_value_drag(widget_point("Inspector", "Intensity"), 100);
        EXPECT_FLOAT_EQ(scene.get_environment().intensity, 64.0f);
        state.mode = EditorMode::Play;
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        EXPECT_EQ(scene.get_environment(), before);
        EXPECT_EQ(history.undo_size(), 0u);
    }

    TEST_F(AssetEditingUiTest, TemplateSwitchRequiresConfirmationAndRestoresOnFailure) {
        selection.select_asset(material);
        frame();
        select_template("unlit_color");
        EXPECT_EQ(material_updates, 0);
        ASSERT_NE(ImGui::FindWindowByName("Change Material Template"), nullptr);
        click(widget_point("Change Material Template", "Switch"));
        ASSERT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.template_name, "unlit_color");
        EXPECT_TRUE(submitted_material.texture_properties.empty());
        EXPECT_EQ(submitted_material.scalar_properties.at("intensity"), 1);
        material_update_success = false;
        select_template("pbr");
        click(widget_point("Change Material Template", "Switch"));
        ASSERT_EQ(material_updates, 2);
        EXPECT_EQ(submitted_material.template_name, "pbr");
        // 发布失败必须同时恢复旧模板及其控件，不能只恢复文件。
        drag_value(material_point("intensity", "Intensity"), 20);
        EXPECT_EQ(submitted_material.template_name, "unlit_color");
    }

    TEST_F(AssetEditingUiTest, CancellingTemplateSwitchDoesNotPublishOrDropTexture) {
        selection.select_asset(material);
        frame();
        select_template("unlit_color");
        auto* popup = ImGui::FindWindowByName("Change Material Template");
        ASSERT_NE(popup, nullptr);
        const auto cancel = popup->GetID("Cancel");
        bool cancelled = false;
        for(float y = popup->WorkRect.Min.y; y < popup->WorkRect.Max.y && !cancelled; y += 4) {
            for(float x = popup->WorkRect.Min.x; x < popup->WorkRect.Max.x; x += 4) {
                ImGui::GetIO().AddMousePosEvent(x, y);
                frame();
                if(ImGui::GetHoveredID() == cancel) {
                    click({x, y});
                    cancelled = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(cancelled);
        EXPECT_EQ(material_updates, 0);
        drag_value(material_point("roughness", "Roughness"), 20);
        ASSERT_GT(material_updates, 0);
        EXPECT_EQ(submitted_material.template_name, "pbr");
        EXPECT_EQ(submitted_material.texture_properties.at("base_color_texture"), texture);
    }

    TEST_F(AssetEditingUiTest, ProjectCreatesMaterialThroughRequestAndSelectsCommittedAsset) {
        project = std::make_unique<ProjectPanel>(
            database, paths.assets(), Comet::AssetScanReport{}, selection, history);
        project->set_material_layouts({Comet::MaterialLayout::find_builtin("pbr")});
        frame();
        frame();
        auto* window = ImGui::FindWindowByName("Project");
        const auto point = ImVec2(window->WorkRect.Min.x + 20, window->WorkRect.Max.y - 20);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(1, true);
        frame();
        io.AddMouseButtonEvent(1, false);
        frame();
        frame();
        ASSERT_FALSE(GImGui->OpenPopupStack.empty());
        const auto* popup = GImGui->OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        click(widget_point(popup->Name, "New Material..."));
        frame();
        click(widget_point("New Material", "Name"));
        io.AddInputCharactersUTF8("authored");
        frame();
        click(widget_point("New Material", "Create"));
        const auto request = project->take_create_material_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->destination, "authored.mat");
        EXPECT_EQ(request->data.template_name, "pbr");
        EXPECT_TRUE(request->data.texture_properties.empty());
        EXPECT_FALSE(project->take_create_material_request());
        EXPECT_FALSE(database.find(request->destination));
        Comet::AssetScanReport failed;
        failed.issues.push_back({request->destination, "Write denied"});
        project->complete_create_material(*request, std::move(failed));
        frame();
        EXPECT_NE(selection.get_selected_asset(), material);
        ASSERT_TRUE(
            Comet::MaterialSerializer{}.save(request->data, paths.assets() / request->destination));
        project->complete_create_material(*request, database.scan());
        EXPECT_EQ(selection.get_selected_asset(), database.find(request->destination)->handle);
        frame();
        EXPECT_FALSE(ImGui::IsPopupOpen("New Material", ImGuiPopupFlags_AnyPopupId));
    }

    TEST_F(AssetEditingUiTest, ProjectDragKeepsSelectionAndOriginalDocumentGeneration) {
        ProjectPanel project(database, paths.assets(), {}, selection, history);
        const auto draw = [&]() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({20, 40});
            ImGui::SetNextWindowSize({700, 500});
            project.render();
            ImGui::Render();
        };
        draw();
        draw();
        auto* window = ImGui::FindWindowByName("Project");
        ASSERT_NE(window, nullptr);
        // 无目录的排序列表最后一行是 texture.png；由实际布局取得 Y。
        const ImVec2 point{
            window->Pos.x + 50, window->DC.CursorPosPrevLine.y + ImGui::GetTextLineHeight() * 0.5f};
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        draw();
        io.AddMouseButtonEvent(0, true);
        draw();
        EXPECT_EQ(selection.get_selected_entity(), entity);
        io.AddMousePosEvent(point.x + 40, point.y);
        draw();
        const auto* data = ImGui::GetDragDropPayload();
        ASSERT_NE(data, nullptr);
        ASSERT_TRUE(data->IsDataType(AssetDragPayload::TYPE));
        ASSERT_EQ(data->DataSize, sizeof(AssetDragPayload));
        const auto first = *static_cast<const AssetDragPayload*>(data->Data);
        EXPECT_EQ(first.handle, texture);
        EXPECT_EQ(first.type, Comet::AssetType::Texture);
        EXPECT_EQ(first.revision, database.get_revision(texture));
        EXPECT_EQ(first.generation, history.generation());
        EXPECT_EQ(selection.get_selected_entity(), entity);
        history.clear();
        draw();
        data = ImGui::GetDragDropPayload();
        ASSERT_NE(data, nullptr);
        EXPECT_EQ(static_cast<const AssetDragPayload*>(data->Data)->generation, first.generation);
        EXPECT_NE(first.generation, history.generation());
    }

    TEST_F(AssetEditingUiTest, ProjectToInspectorDragKeepsTheTargetVisible) {
        project = std::make_unique<ProjectPanel>(
            database, paths.assets(), Comet::AssetScanReport{}, selection, history);
        frame();
        frame();
        const auto* window = ImGui::FindWindowByName("Project");
        ASSERT_NE(window, nullptr);
        const ImVec2 source{window->WorkRect.Min.x + 70,
            window->WorkRect.Min.y + 2 * ImGui::GetTextLineHeightWithSpacing()
                + ImGui::GetTextLineHeight() * 0.5f};
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(source.x, source.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        EXPECT_EQ(selection.get_selected_entity(), entity);
        io.AddMousePosEvent(source.x + 30, source.y);
        frame();
        ASSERT_TRUE(ImGui::IsDragDropActive());
        const auto target = property_point(0);
        io.AddMousePosEvent(target.x, target.y);
        frame();
        frame();
        EXPECT_EQ(selection.get_selected_entity(), entity);
        io.AddMouseButtonEvent(0, false);
        frame();
        const auto request = inspector->take_asset_assignment();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->asset.handle, mesh);
        EXPECT_EQ(request->target.entity, entity.get_uuid());
        EXPECT_FALSE(entity.get_component<Comet::MeshRendererComponent>().mesh);
    }

    TEST_F(AssetEditingUiTest, PickerFiltersTypesAndDistinguishesSameFilenamePaths) {
        std::filesystem::create_directories(paths.assets() / "nested");
        std::ofstream(paths.assets() / "nested/mesh.gltf") << R"({"asset":{"version":"2.0"}})";
        ASSERT_TRUE(database.scan().succeeded());
        auto request = choose(0, 2);
        ASSERT_TRUE(request);
        EXPECT_EQ(request->asset.handle, database.find("nested/mesh.gltf")->handle);
        EXPECT_EQ(request->asset.type, Comet::AssetType::Mesh);
        EXPECT_NE(request->asset.handle, mesh);
        request = choose(1, 1);
        ASSERT_TRUE(request);
        EXPECT_EQ(request->asset.handle, material);
        EXPECT_EQ(request->asset.revision, database.get_revision(material));
        EXPECT_EQ(request->asset.type, Comet::AssetType::Material);
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_FALSE(entity.get_component<Comet::MeshRendererComponent>().material);
    }

    TEST_F(AssetEditingUiTest, PickerPreservesMissingReferenceAndClearCanBeUndone) {
        auto& renderer = entity.get_component<Comet::MeshRendererComponent>();
        renderer.mesh = Comet::AssetHandle(999);
        frame();
        frame();
        EXPECT_EQ(renderer.mesh, Comet::AssetHandle(999));
        const auto request = choose(0, 0);
        ASSERT_TRUE(request);
        EXPECT_FALSE(request->asset.handle);
        EXPECT_EQ(request->asset.revision, Comet::INVALID_ASSET_REVISION);
        ASSERT_TRUE(edit.apply(request->target, request->asset.handle));
        EXPECT_FALSE(renderer.mesh);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(renderer.mesh, Comet::AssetHandle(999));
        renderer.mesh = mesh;
        EXPECT_FALSE(choose(0, 1));
    }

    TEST_F(AssetEditingUiTest, PlayPickerQueuesRuntimeRequestButRejectsDrop) {
        state.mode = EditorMode::Play;
        history.bind_scene(nullptr);
        const auto request = choose(1, 1);
        ASSERT_TRUE(request);
        EXPECT_EQ(request->asset.handle, material);
        EXPECT_EQ(request->asset.generation, history.generation());
        payload = drag_asset(material, Comet::AssetType::Material);
        EXPECT_FALSE(drop(property_point(1)));
        EXPECT_EQ(history.undo_size(), 0);
    }

    TEST_F(AssetEditingUiTest, TypedDropQueuesIdentityAndUsesSharedPropertyHistory) {
        for(const int index : {0, 1}) {
            const auto handle = index == 0 ? mesh : material;
            const auto type = index == 0 ? Comet::AssetType::Mesh : Comet::AssetType::Material;
            payload = drag_asset(handle, type);
            const auto request = drop(property_point(index));
            ASSERT_TRUE(request);
            EXPECT_EQ(request->target.entity, entity.get_uuid());
            EXPECT_EQ(request->target.component, "mesh_renderer");
            EXPECT_EQ(request->target.property, index == 0 ? "mesh" : "material");
            EXPECT_EQ(request->asset.handle, handle);
            EXPECT_EQ(history.undo_size(), 0);
            ASSERT_TRUE(edit.apply(request->target, request->asset.handle));
            EXPECT_EQ(history.undo_size(), 1);
            ASSERT_TRUE(history.undo());
            auto& renderer = entity.get_component<Comet::MeshRendererComponent>();
            EXPECT_FALSE(renderer.mesh);
            EXPECT_FALSE(renderer.material);
            ASSERT_TRUE(history.redo());
            if(index == 0)
                EXPECT_EQ(renderer.mesh, handle);
            else
                EXPECT_EQ(renderer.material, handle);
            EXPECT_FALSE(drop(property_point(index)));
            ASSERT_TRUE(history.undo());
            history.clear();
        }
    }

    TEST_F(AssetEditingUiTest, TextureSlotUsesMaterialUpdateAndRestoresFailedChanges) {
        selection.select_asset(material);
        frame();
        frame();
        const auto point = material_point("base_color_texture", "Base Color Texture");
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        material_update_success = false;
        EXPECT_FALSE(drop(point));
        ASSERT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.texture_properties.at("base_color_texture"), second_texture);
        material_update_success = true;
        EXPECT_FALSE(drop(point));
        EXPECT_EQ(material_updates, 2);
        EXPECT_FALSE(drop(point));
        EXPECT_EQ(material_updates, 2);
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_EQ(Comet::MaterialSerializer{}
                      .load(paths.assets() / "material.mat")
                      .value()
                      .texture_properties.at("base_color_texture"),
            texture);
    }

    TEST_F(AssetEditingUiTest, SelectedMaterialReloadsOnlyWhenItsRevisionChanges) {
        selection.select_asset(material);
        frame();
        const auto slot_point = [&]() {
            return material_point("base_color_texture", "Base Color Texture");
        };
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(slot_point()));
        ASSERT_EQ(material_updates, 1);

        // 测试回调只修改内存；无关扫描不应重读磁盘、丢掉当前面板值。
        const auto revision = database.get_revision(material);
        std::ofstream(paths.assets() / "mesh.gltf") << "changed mesh";
        ASSERT_TRUE(database.scan().succeeded());
        ASSERT_EQ(database.get_revision(material), revision);
        frame();
        EXPECT_FALSE(drop(slot_point()));
        EXPECT_EQ(material_updates, 1);

        inspector->set_visible(false);
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "pbr",
                .texture_properties = {{"base_color_texture", texture}},
                .scalar_properties = {{"roughness", 0.25f}}},
            paths.assets() / "material.mat"));
        ASSERT_TRUE(database.scan().succeeded());
        ASSERT_NE(database.get_revision(material), revision);
        frame();
        inspector->set_visible(true);
        frame();
        frame();
        EXPECT_FALSE(drop(slot_point()));
        EXPECT_EQ(material_updates, 2);
        EXPECT_FLOAT_EQ(submitted_material.scalar_properties.at("roughness"), 0.25f);
    }

    TEST_F(AssetEditingUiTest, FailedMaterialLoadRetriesAfterAssetRevisionChanges) {
        std::ofstream(paths.assets() / "material.mat") << "invalid material";
        static_cast<void>(database.scan());
        selection.select_asset(material);
        frame();

        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "pbr", .texture_properties = {{"base_color_texture", texture}}},
            paths.assets() / "material.mat"));
        ASSERT_TRUE(database.scan().succeeded());
        frame();
        frame();
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("base_color_texture", "Base Color Texture")));
        EXPECT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.template_name, "pbr");
    }

    TEST_F(AssetEditingUiTest, LayoutDefaultsDoNotPublishUntilScalarActuallyChanges) {
        selection.select_asset(material);
        frame();
        frame();
        const auto point = material_point("roughness", "Roughness");
        EXPECT_EQ(material_updates, 0);
        EXPECT_TRUE(Comet::MaterialSerializer{}
                .load(paths.assets() / "material.mat")
                .value()
                .scalar_properties.empty());
        drag_value(point, 30);
        ASSERT_GT(material_updates, 0);
        EXPECT_GT(submitted_material.scalar_properties.at("roughness"), 0.5f);
        EXPECT_LE(submitted_material.scalar_properties.at("roughness"), 1.0f);
        EXPECT_TRUE(submitted_material.vector_properties.empty());
        const auto updates = material_updates;
        for(int index = 0; index < 10; ++index)
            frame();
        EXPECT_EQ(material_updates, updates);
        EXPECT_EQ(history.undo_size(), 0);
    }

    TEST_F(AssetEditingUiTest, FailedScalarUpdateRestoresValuesAndAllowsRetry) {
        selection.select_asset(material);
        frame();
        frame();
        const auto point = material_point("roughness", "Roughness");
        material_update_success = false;
        drag_value(point, 20);
        ASSERT_GT(material_updates, 0);
        const auto failed_updates = material_updates;
        // 隔开两次手势，避免触发 DragFloat 的双击文本编辑。
        for(int index = 0; index < 20; ++index)
            frame();
        EXPECT_EQ(material_updates, failed_updates);
        material_update_success = true;
        drag_value(point, -20);
        EXPECT_GT(material_updates, failed_updates);
        EXPECT_NEAR(submitted_material.scalar_properties.at("roughness"), 0.3f, 0.001f);
        const auto updates = material_updates;
        frame();
        frame();
        EXPECT_EQ(material_updates, updates);
    }

    TEST_F(AssetEditingUiTest, RepairsMissingTextureSlotsWithoutAnApplyButton) {
        require_texture_pair();
        selection.select_asset(material);
        frame();
        frame();
        payload = drag_asset(texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("base_color_texture", "Base Color Texture")));
        EXPECT_EQ(material_updates, 0);
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("detail_texture", "Detail Texture")));
        ASSERT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.texture_properties.at("base_color_texture"), texture);
        EXPECT_EQ(submitted_material.texture_properties.at("detail_texture"), second_texture);
        frame();
        EXPECT_EQ(material_updates, 1);
    }

    TEST_F(AssetEditingUiTest, PbrParametersPublishOnChangeWithoutIdleWrites) {
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "pbr"}, paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        EXPECT_EQ(material_updates, 0);
        drag_value(material_point("metallic", "Metallic"), 20);
        ASSERT_GT(material_updates, 0);
        EXPECT_GT(submitted_material.scalar_properties.at("metallic"), 0);
        EXPECT_LE(submitted_material.scalar_properties.at("metallic"), 1);
        const auto first = material_updates;
        drag_value(material_point("roughness", "Roughness"), -20);
        EXPECT_GT(material_updates, first);
        EXPECT_GE(submitted_material.scalar_properties.at("roughness"), 0.045f);
        EXPECT_LT(submitted_material.scalar_properties.at("roughness"), 0.5f);
        EXPECT_TRUE(submitted_material.texture_properties.empty());
        const auto updates = material_updates;
        frame();
        frame();
        EXPECT_EQ(material_updates, updates);
    }

    TEST_F(AssetEditingUiTest, PbrOptionalTextureCanBeAssignedReplacedAndCleared) {
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "pbr"}, paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        payload = drag_asset(texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("base_color_texture", "Base Color Texture")));
        ASSERT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.texture_properties.at("base_color_texture"), texture);
        const auto choose_texture = [&](int row) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(5, 5);
            // 独立点击，避免与上一轮模拟拖放被识别成双击。
            for(float elapsed = 0; elapsed <= io.MouseDoubleClickTime; elapsed += io.DeltaTime)
                frame();
            EXPECT_FALSE(click(material_point("base_color_texture", "Base Color Texture")));
            frame();
            const auto* popup = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(popup, nullptr);
            ASSERT_TRUE(popup->Active);
            EXPECT_FALSE(click({popup->DC.CursorStartPos.x + 20,
                popup->DC.CursorStartPos.y + row * ImGui::GetTextLineHeightWithSpacing()
                    + ImGui::GetTextLineHeight() * 0.5f}));
        };
        choose_texture(1); // None 后是按路径排序的 second.png。
        ASSERT_EQ(material_updates, 2);
        EXPECT_EQ(submitted_material.texture_properties.at("base_color_texture"), second_texture);
        choose_texture(0);
        EXPECT_EQ(material_updates, 3);
        EXPECT_TRUE(submitted_material.texture_properties.empty());
        EXPECT_TRUE(Comet::MaterialSerializer{}.serialize(submitted_material));
        frame();
        EXPECT_EQ(material_updates, 3);
    }

    TEST_F(AssetEditingUiTest, SolidLayoutNeedsNoTextureAndPublishesNumericParameter) {
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_color"}, paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        EXPECT_EQ(material_updates, 0);
        drag_value(material_point("intensity", "Intensity"), 20);
        ASSERT_GT(material_updates, 0);
        EXPECT_GT(submitted_material.scalar_properties.at("intensity"), 1.0f);
        EXPECT_TRUE(submitted_material.texture_properties.empty());
    }

    TEST_F(AssetEditingUiTest, ColorMetadataUsesAnEditableColorWidgetWithoutIdleWrites) {
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_color"}, paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        const auto point = material_point("color", "Color", "##X");
        EXPECT_EQ(material_updates, 0);
        drag_value(point, -40);
        ASSERT_GT(material_updates, 0);
        const auto color = submitted_material.vector_properties.at("color");
        EXPECT_LT(color[0], 1.0f);
        EXPECT_FLOAT_EQ(color[1], 1.0f);
        EXPECT_TRUE(submitted_material.scalar_properties.empty());
        const auto updates = material_updates;
        for(int index = 0; index < 10; ++index)
            frame();
        EXPECT_EQ(material_updates, updates);
    }

    TEST_F(AssetEditingUiTest, IncompleteTextureDraftDoesNotLeakToAnotherAsset) {
        require_texture_pair();
        selection.select_asset(material);
        frame();
        frame();
        payload = drag_asset(texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("base_color_texture", "Base Color Texture")));
        EXPECT_EQ(material_updates, 0);
        inspector->set_visible(false);
        selection.select_asset(texture);
        frame();
        selection.select_asset(material);
        frame();
        inspector->set_visible(true);
        frame();
        frame();
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("detail_texture", "Detail Texture")));
        EXPECT_EQ(material_updates, 0);
        EXPECT_TRUE(Comet::MaterialSerializer{}
                .load(paths.assets() / "material.mat")
                .value()
                .texture_properties.empty());
    }

    TEST_F(AssetEditingUiTest, EditingKnownPropertyRepairsItsMismatchedType) {
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_color", .scalar_properties = {{"color", 0.5f}}},
            paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        EXPECT_EQ(material_updates, 0);
        drag_value(material_point("color", "Color", "##X"), -40);
        ASSERT_GT(material_updates, 0);
        EXPECT_FALSE(submitted_material.scalar_properties.contains("color"));
        EXPECT_TRUE(submitted_material.vector_properties.contains("color"));
        EXPECT_TRUE(Comet::MaterialSerializer{}.serialize(submitted_material));
    }

    TEST_F(AssetEditingUiTest, PublishedLayoutChangesWidgetsWithoutResettingMaterialDraft) {
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_color", .scalar_properties = {{"intensity", 0.5f}}},
            paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        auto layout = Comet::MaterialLayout::create("unlit_color", {}, 48,
            {{"intensity", 0, 1, 0, 10, 0.1f, "Power"}},
            {{"color", 32, {1, 1, 1, 1}, Comet::MaterialLayout::VectorProperty::Semantic::Color,
                "Color"}},
            5);
        ASSERT_TRUE(layout) << layout.error();
        const auto published = std::make_shared<Comet::MaterialLayout>(std::move(layout).value());
        inspector->asset_inspector().set_material_layouts({published});
        frame();
        EXPECT_EQ(material_updates, 0);
        drag_value(material_point("intensity", "Power"), 20);
        ASSERT_GT(material_updates, 0);
        EXPECT_GT(submitted_material.scalar_properties.at("intensity"), 0.5f);
        const auto edited = submitted_material;
        const auto updates = material_updates;
        inspector->asset_inspector().set_material_layouts({published});
        for(int index = 0; index < 5; ++index)
            frame();
        EXPECT_EQ(material_updates, updates);
        EXPECT_EQ(submitted_material, edited);
    }

    TEST_F(AssetEditingUiTest, UnknownPropertiesPreventPublicationWithoutBeingDeleted) {
        const Comet::MaterialData original{
            .template_name = "unlit_color", .scalar_properties = {{"custom", 2.0f}}};
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(original, paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        drag_value(material_point("intensity", "Intensity"), 20);
        EXPECT_EQ(material_updates, 0);
        EXPECT_EQ(
            Comet::MaterialSerializer{}.load(paths.assets() / "material.mat").value(), original);
    }

    TEST_F(AssetEditingUiTest, RejectsWrongTypeMissingAssetAndStaleDocument) {
        const auto point = property_point(1);
        payload = drag_asset(mesh, Comet::AssetType::Mesh);
        EXPECT_FALSE(drop(point));
        payload = drag_asset(mesh, Comet::AssetType::Material);
        EXPECT_FALSE(drop(point));
        payload = drag_asset(Comet::AssetHandle(999), Comet::AssetType::Material);
        EXPECT_FALSE(drop(point));
        payload = drag_asset(material, Comet::AssetType::Material);
        ++payload.revision;
        EXPECT_FALSE(drop(point));
        payload = drag_asset(material, Comet::AssetType::Material);
        payload_size = sizeof(std::uint64_t);
        EXPECT_FALSE(drop(point));
        payload_size = sizeof(AssetDragPayload);
        history.clear();
        EXPECT_FALSE(drop(point));
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_FALSE(entity.get_component<Comet::MeshRendererComponent>().material);
    }
}
#endif
