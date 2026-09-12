#ifdef COMET_TEST_EDITOR_UI
#include "inspector/inspector.h"
#include "assets/project.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"

#include "support/imgui_context.h"

#include "support/temporary_directory.h"

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
                {.template_name = "unlit_texture_blend",
                    .texture_properties = {{"u_Texture0", texture},
                        {"u_Texture1", texture}}},
                paths.assets() / "material.mat"));
            ASSERT_TRUE(database.scan().succeeded());
            auto builtins = Comet::create_scene_component_registry();
            ASSERT_TRUE(
                registry.register_component(*builtins.find_component("mesh_renderer")));
            entity.add_component<Comet::MeshRendererComponent>();
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            inspector = std::make_unique<InspectorPanel>(
                state, selection, history, edit, registry, widgets, database,
                paths.assets(),
                [this](Comet::AssetHandle handle, const Comet::MaterialData& data) {
                    EXPECT_EQ(handle, material);
                    ++material_updates;
                    submitted_material = data;
                    return material_update_success;
                },
                nullptr);
            frame();
            frame();
        }

        void TearDown() override { inspector.reset(); }

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

        ImVec2 material_point(
            const char* name, const char* label, const char* child = nullptr) {
            auto* window = ImGui::FindWindowByName("Inspector");
            const ImGuiID group = window->GetID(name);
            ImGuiID id = ImHashStr(label, 0, group);
            if(child)
                id = ImHashStr(child, 0, id);
            for(float y = window->WorkRect.Min.y; y < window->WorkRect.Max.y; y += 4) {
                const ImVec2 point{window->WorkRect.Min.x + 40, y};
                ImGui::GetIO().AddMousePosEvent(point.x, point.y);
                frame();
                if(ImGui::GetHoveredID() == id)
                    return point;
            }
            ADD_FAILURE() << "Material widget not found: " << name;
            return {};
        }

        void drag_value(ImVec2 point, float distance) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMousePosEvent(point.x + distance / 2, point.y);
            frame();
            io.AddMousePosEvent(point.x + distance, point.y);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
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

    TEST_F(AssetEditingUiTest, ProjectDragKeepsSelectionAndOriginalDocumentGeneration) {
        ProjectPanel project(
            database, paths.assets(), {}, nullptr, nullptr, selection, history);
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
        const ImVec2 point{window->Pos.x + 50,
            window->DC.CursorPosPrevLine.y + ImGui::GetTextLineHeight() * 0.5f};
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
        EXPECT_EQ(static_cast<const AssetDragPayload*>(data->Data)->generation,
            first.generation);
        EXPECT_NE(first.generation, history.generation());
    }

    TEST_F(AssetEditingUiTest, ProjectToInspectorDragKeepsTheTargetVisible) {
        project = std::make_unique<ProjectPanel>(database, paths.assets(),
            Comet::AssetScanReport{}, nullptr, nullptr, selection, history);
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
        std::ofstream(paths.assets() / "nested/mesh.gltf")
            << R"({"asset":{"version":"2.0"}})";
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
            const auto type =
                index == 0 ? Comet::AssetType::Mesh : Comet::AssetType::Material;
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
        const auto point = material_point("u_Texture0", "Texture 0");
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        material_update_success = false;
        EXPECT_FALSE(drop(point));
        ASSERT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.texture_properties.at("u_Texture0"), second_texture);
        material_update_success = true;
        EXPECT_FALSE(drop(point));
        EXPECT_EQ(material_updates, 2);
        EXPECT_FALSE(drop(point));
        EXPECT_EQ(material_updates, 2);
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_EQ(Comet::MaterialSerializer{}
                      .load(paths.assets() / "material.mat")
                      .value()
                      .texture_properties.at("u_Texture0"),
            texture);
    }

    TEST_F(AssetEditingUiTest, SelectedMaterialReloadsOnlyWhenItsRevisionChanges) {
        selection.select_asset(material);
        frame();
        const auto slot_point = [&]() {
            return material_point("u_Texture0", "Texture 0");
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
            {.template_name = "unlit_texture_blend",
                .texture_properties = {{"u_Texture0", texture}, {"u_Texture1", texture}},
                .scalar_properties = {{"blend", 0.25f}}},
            paths.assets() / "material.mat"));
        ASSERT_TRUE(database.scan().succeeded());
        ASSERT_NE(database.get_revision(material), revision);
        frame();
        inspector->set_visible(true);
        frame();
        frame();
        EXPECT_FALSE(drop(slot_point()));
        EXPECT_EQ(material_updates, 2);
        EXPECT_FLOAT_EQ(submitted_material.scalar_properties.at("blend"), 0.25f);
    }

    TEST_F(AssetEditingUiTest, FailedMaterialLoadRetriesAfterAssetRevisionChanges) {
        std::ofstream(paths.assets() / "material.mat") << "invalid material";
        static_cast<void>(database.scan());
        selection.select_asset(material);
        frame();

        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_texture_blend",
                .texture_properties = {{"u_Texture0", texture}, {"u_Texture1", texture}}},
            paths.assets() / "material.mat"));
        ASSERT_TRUE(database.scan().succeeded());
        frame();
        frame();
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("u_Texture0", "Texture 0")));
        EXPECT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.template_name, "unlit_texture_blend");
    }

    TEST_F(AssetEditingUiTest, LayoutDefaultsDoNotPublishUntilScalarActuallyChanges) {
        selection.select_asset(material);
        frame();
        frame();
        const auto point = material_point("blend", "Blend");
        EXPECT_EQ(material_updates, 0);
        EXPECT_TRUE(Comet::MaterialSerializer{}
                .load(paths.assets() / "material.mat")
                .value()
                .scalar_properties.empty());
        drag_value(point, 30);
        ASSERT_GT(material_updates, 0);
        EXPECT_GT(submitted_material.scalar_properties.at("blend"), 0.5f);
        EXPECT_LE(submitted_material.scalar_properties.at("blend"), 1.0f);
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
        const auto point = material_point("blend", "Blend");
        material_update_success = false;
        drag_value(point, 20);
        ASSERT_GT(material_updates, 0);
        const auto failed_updates = material_updates;
        // Separate gestures must not enter DragFloat's double-click text mode.
        for(int index = 0; index < 20; ++index)
            frame();
        EXPECT_EQ(material_updates, failed_updates);
        material_update_success = true;
        drag_value(point, -20);
        EXPECT_GT(material_updates, failed_updates);
        EXPECT_NEAR(submitted_material.scalar_properties.at("blend"), 0.3f, 0.001f);
        const auto updates = material_updates;
        frame();
        frame();
        EXPECT_EQ(material_updates, updates);
    }

    TEST_F(AssetEditingUiTest, RepairsMissingTextureSlotsWithoutAnApplyButton) {
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_texture_blend"}, paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        payload = drag_asset(texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("u_Texture0", "Texture 0")));
        EXPECT_EQ(material_updates, 0);
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("u_Texture1", "Texture 1")));
        ASSERT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.texture_properties.at("u_Texture0"), texture);
        EXPECT_EQ(submitted_material.texture_properties.at("u_Texture1"), second_texture);
        frame();
        EXPECT_EQ(material_updates, 1);
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
        ASSERT_TRUE(Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_texture_blend"}, paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        payload = drag_asset(texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("u_Texture0", "Texture 0")));
        EXPECT_EQ(material_updates, 0);
        selection.select_asset(texture);
        frame();
        selection.select_asset(material);
        frame();
        frame();
        payload = drag_asset(second_texture, Comet::AssetType::Texture);
        EXPECT_FALSE(drop(material_point("u_Texture1", "Texture 1")));
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

    TEST_F(AssetEditingUiTest, UnknownPropertiesPreventPublicationWithoutBeingDeleted) {
        const Comet::MaterialData original{
            .template_name = "unlit_color", .scalar_properties = {{"custom", 2.0f}}};
        ASSERT_TRUE(
            Comet::MaterialSerializer{}.save(original, paths.assets() / "material.mat"));
        selection.select_asset(material);
        frame();
        frame();
        drag_value(material_point("intensity", "Intensity"), 20);
        EXPECT_EQ(material_updates, 0);
        EXPECT_EQ(
            Comet::MaterialSerializer{}.load(paths.assets() / "material.mat").value(),
            original);
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
