#ifdef COMET_TEST_EDITOR_UI
#include "panels/inspector.h"
#include "panels/project.h"
#include "property_editor_registry.h"
#include "selection.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>

namespace CometEditor::Tests {
    class AssetEditingUiTest: public ::testing::Test {
    protected:
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_asset_editing_"
                + std::to_string(Comet::AssetHandle::generate().value()));
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
        const Comet::AssetHandle mesh{42}, material{43}, texture{44}, second_texture{45};
        AssetDragPayload payload;
        bool dragging = false;
        int material_updates = 0;
        bool material_update_success = true;
        Comet::MaterialData submitted_material;

        void SetUp() override {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {800, 600};
            io.DeltaTime = 1.0f / 60;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            std::filesystem::create_directories(paths.assets());
            const auto add_asset = [&](const char* name, Comet::AssetHandle handle,
                                       Comet::AssetType type) {
                std::ofstream(paths.assets() / name) << "{}";
                Comet::AssetMetadataSerializer{}.save(
                    {.handle = handle,
                        .type = type,
                        .import_settings = Comet::make_default_import_settings(type)},
                    Comet::metadata_path(paths.assets() / name));
            };
            add_asset("mesh.gltf", mesh, Comet::AssetType::Mesh);
            add_asset("texture.png", texture, Comet::AssetType::Texture);
            add_asset("second.png", second_texture, Comet::AssetType::Texture);
            add_asset("material.mat", material, Comet::AssetType::Material);
            Comet::MaterialSerializer{}.save(
                {.template_name = "cube_texture",
                    .texture_properties = {{"u_Texture0", texture},
                        {"u_Texture1", texture}}},
                paths.assets() / "material.mat");
            ASSERT_TRUE(database.scan().succeeded());
            auto builtins = Comet::create_scene_component_registry();
            ASSERT_TRUE(
                registry.register_component(*builtins.find_component("mesh_renderer")));
            entity.add_component<Comet::MeshRendererComponent>();
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            inspector = std::make_unique<InspectorPanel>(
                selection, history, edit, registry, widgets, database, paths.assets(),
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

        void TearDown() override {
            inspector.reset();
            ImGui::DestroyContext();
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        void frame() {
            ImGui::NewFrame();
            if(dragging && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload(
                    AssetDragPayload::TYPE, &payload, sizeof(payload), ImGuiCond_Once);
                ImGui::TextUnformatted("Asset");
                ImGui::EndDragDropSource();
            }
            ImGui::SetNextWindowPos({20, 40});
            ImGui::SetNextWindowSize({700, 500});
            inspector->render();
            ImGui::Render();
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
        ProjectPanel project(database, {}, nullptr, nullptr, selection, history);
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

    TEST_F(AssetEditingUiTest, TypedDropQueuesIdentityAndUsesSharedPropertyHistory) {
        for(const int index : {0, 1}) {
            const auto handle = index == 0 ? mesh : material;
            const auto type =
                index == 0 ? Comet::AssetType::Mesh : Comet::AssetType::Material;
            payload = {handle, history.generation(), type};
            const auto request = drop(property_point(index));
            ASSERT_TRUE(request);
            EXPECT_EQ(request->target.entity, entity.get_uuid());
            EXPECT_EQ(request->target.component, "mesh_renderer");
            EXPECT_EQ(request->target.property, index == 0 ? "mesh" : "material");
            EXPECT_EQ(request->asset.handle, handle);
            EXPECT_EQ(history.undo_size(), 0);
            ASSERT_TRUE(edit.begin(request->target));
            ASSERT_TRUE(edit.preview(request->asset.handle));
            ASSERT_TRUE(edit.commit());
            EXPECT_EQ(history.undo_size(), 1);
            ASSERT_TRUE(history.undo());
            auto& renderer = entity.get_component<Comet::MeshRendererComponent>();
            EXPECT_FALSE(renderer.mesh);
            EXPECT_FALSE(renderer.material);
            history.clear();
        }
    }

    TEST_F(AssetEditingUiTest, TextureSlotUsesMaterialUpdateAndRestoresFailedChanges) {
        selection.select_asset(material);
        frame();
        frame();
        const auto point = material_point("u_Texture0", "Texture 0");
        payload = {second_texture, history.generation(), Comet::AssetType::Texture};
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
                      .texture_properties.at("u_Texture0"),
            texture);
    }

    TEST_F(AssetEditingUiTest, LayoutDefaultsDoNotPublishUntilScalarActuallyChanges) {
        selection.select_asset(material);
        frame();
        frame();
        const auto point = material_point("blend", "Blend");
        EXPECT_EQ(material_updates, 0);
        EXPECT_TRUE(Comet::MaterialSerializer{}
                .load(paths.assets() / "material.mat")
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
        // 两次独立拖动，不触发 DragFloat 的双击文字输入。
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
        Comet::MaterialSerializer{}.save(
            {.template_name = "cube_texture"}, paths.assets() / "material.mat");
        selection.select_asset(material);
        frame();
        frame();
        payload = {texture, history.generation(), Comet::AssetType::Texture};
        EXPECT_FALSE(drop(material_point("u_Texture0", "Texture 0")));
        EXPECT_EQ(material_updates, 0);
        payload = {second_texture, history.generation(), Comet::AssetType::Texture};
        EXPECT_FALSE(drop(material_point("u_Texture1", "Texture 1")));
        ASSERT_EQ(material_updates, 1);
        EXPECT_EQ(submitted_material.texture_properties.at("u_Texture0"), texture);
        EXPECT_EQ(submitted_material.texture_properties.at("u_Texture1"), second_texture);
        frame();
        EXPECT_EQ(material_updates, 1);
    }

    TEST_F(AssetEditingUiTest, SolidLayoutNeedsNoTextureAndPublishesNumericParameter) {
        Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_color"}, paths.assets() / "material.mat");
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
        Comet::MaterialSerializer{}.save(
            {.template_name = "unlit_color"}, paths.assets() / "material.mat");
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
        Comet::MaterialSerializer{}.save(
            {.template_name = "cube_texture"}, paths.assets() / "material.mat");
        selection.select_asset(material);
        frame();
        frame();
        payload = {texture, history.generation(), Comet::AssetType::Texture};
        EXPECT_FALSE(drop(material_point("u_Texture0", "Texture 0")));
        EXPECT_EQ(material_updates, 0);
        selection.select_asset(texture);
        frame();
        selection.select_asset(material);
        frame();
        frame();
        payload = {second_texture, history.generation(), Comet::AssetType::Texture};
        EXPECT_FALSE(drop(material_point("u_Texture1", "Texture 1")));
        EXPECT_EQ(material_updates, 0);
        EXPECT_TRUE(Comet::MaterialSerializer{}
                .load(paths.assets() / "material.mat")
                .texture_properties.empty());
    }

    TEST_F(AssetEditingUiTest, RejectsWrongTypeMissingAssetAndStaleDocument) {
        const auto point = property_point(1);
        payload = {mesh, history.generation(), Comet::AssetType::Mesh};
        EXPECT_FALSE(drop(point));
        payload = {mesh, history.generation(), Comet::AssetType::Material};
        EXPECT_FALSE(drop(point));
        payload = {
            Comet::AssetHandle(999), history.generation(), Comet::AssetType::Material};
        EXPECT_FALSE(drop(point));
        payload = {material, history.generation(), Comet::AssetType::Material};
        history.clear();
        EXPECT_FALSE(drop(point));
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_FALSE(entity.get_component<Comet::MeshRendererComponent>().material);
    }
}
#endif
