#ifdef COMET_TEST_EDITOR_UI
#include "panels/project.h"
#include "selection.h"
#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "render/resource/resource_factory.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <memory>

namespace CometEditor::Tests {
    class ProjectPanelTest: public ::testing::Test {
    protected:
        class Factory: public Comet::RenderResourceFactory {
        public:
            Comet::GpuResourceResult<std::shared_ptr<Comet::Mesh>> try_create_mesh(
                const Comet::MeshData&) override {
                ADD_FAILURE() << "Asset file operations must not create GPU resources";
                return Comet::GpuResourceResult<std::shared_ptr<Comet::Mesh>>::failure(
                    vk::Result::eErrorUnknown);
            }
            Comet::GpuResourceResult<std::shared_ptr<Comet::Texture>> try_create_texture(
                const Comet::TextureData&) override {
                ADD_FAILURE() << "Asset file operations must not create GPU resources";
                return Comet::GpuResourceResult<std::shared_ptr<Comet::Texture>>::failure(
                    vk::Result::eErrorUnknown);
            }
        } factory;
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_project_ui_"
                + std::to_string(Comet::AssetHandle::generate().value()));
        Comet::ProjectPaths paths{root};
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler{1};
        Comet::AssetManager manager{paths, registry, factory, scheduler};
        const Comet::AssetDatabase& database = manager.get_database();
        Comet::Scene scene;
        SelectionService selection{scene};
        std::unique_ptr<ProjectPanel> project;
        int move_count = 0;
        int refresh_count = 0;
        Comet::AssetHandle moved_handle;
        std::filesystem::path destination;

        void SetUp() override {
            std::filesystem::create_directories(paths.assets() / "folder");
            std::ofstream(paths.assets() / "a.png") << "a";
            std::ofstream(paths.assets() / "b.png") << "b";
            std::ofstream(paths.assets() / "folder/c.png") << "c";
            auto report = manager.scan();
            ASSERT_TRUE(report.succeeded());
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(800, 600);
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            project = std::make_unique<ProjectPanel>(
                database, std::move(report),
                [this]() {
                    ++refresh_count;
                    project->update_scan_report(manager.scan());
                },
                [this](const Comet::AssetHandle handle,
                    const std::filesystem::path& target) {
                    ++move_count;
                    moved_handle = handle;
                    destination = target;
                    auto result = manager.move_asset(handle, target);
                    project->update_scan_report(result);
                    return result;
                },
                selection);
            frame();
            frame();
        }

        void TearDown() override {
            project.reset();
            ImGui::DestroyContext();
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        void frame() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(420, 400));
            project->render();
            ImGui::Render();
        }

        // 初始顺序：assets、folder、c.png、a.png、b.png。
        ImVec2 row_point(int row) {
            const auto* window = ImGui::FindWindowByName("Project");
            return {window->WorkRect.Min.x + 70,
                window->WorkRect.Min.y + row * ImGui::GetTextLineHeightWithSpacing()
                    + ImGui::GetTextLineHeight() * 0.5f};
        }

        void click(ImVec2 point, int button = 0) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(button, true);
            frame();
            io.AddMouseButtonEvent(button, false);
            frame();
            frame();
        }

        void open_rename(int row) {
            click(row_point(row), 1);
            auto& context = *ImGui::GetCurrentContext();
            ASSERT_EQ(context.OpenPopupStack.Size, 1);
            auto* popup = context.OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID("Rename"));
            frame();
            frame();
            auto* dialog = ImGui::FindWindowByName("Rename Asset");
            ASSERT_NE(dialog, nullptr);
            ASSERT_TRUE(dialog->Active);
        }

        void rename(const char* name) {
            auto* dialog = ImGui::FindWindowByName("Rename Asset");
            ASSERT_NE(dialog, nullptr);
            click({dialog->WorkRect.Min.x + 40, dialog->WorkRect.Min.y + 8});
            auto& io = ImGui::GetIO();
            const auto primary =
                io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(primary, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(primary, false);
            frame();
            io.AddInputCharactersUTF8(name);
            frame();
            ImGui::ActivateItemByID(dialog->GetID("Rename"));
            frame();
            frame();
        }

        void begin_drag(int row) {
            const auto point = row_point(row);
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMousePosEvent(point.x + 25, point.y);
            frame();
            frame();
            ASSERT_TRUE(ImGui::GetCurrentContext()->DragDropActive);
        }

        void drop(int row) {
            const auto point = row_point(row);
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
        }
    };

    TEST_F(ProjectPanelTest, ContextMenuRefreshesOnceAndCanReplaceTree) {
        const auto selected = database.find("b.png")->handle;
        selection.select_asset(selected);
        for(const ImVec2 point : {ImVec2(300, 350), row_point(3)}) {
            click(point, 1);
            auto& context = *ImGui::GetCurrentContext();
            ASSERT_EQ(context.OpenPopupStack.Size, 1);
            auto* popup = context.OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            const auto previous_count = refresh_count;
            ImGui::ActivateItemByID(popup->GetID("Refresh"));
            frame();
            frame();
            EXPECT_EQ(refresh_count, previous_count + 1);
        }
        EXPECT_EQ(selection.get_selected_asset(), selected);
    }

    TEST_F(ProjectPanelTest, RenameUsesClickedAssetAndPreservesExtensionAndIdentity) {
        const auto source = database.find("a.png")->handle;
        const auto selected = database.find("b.png")->handle;
        selection.select_asset(selected);
        open_rename(3);
        rename("renamed");
        EXPECT_EQ(move_count, 1);
        EXPECT_EQ(moved_handle, source);
        EXPECT_EQ(destination, "renamed.png");
        EXPECT_EQ(database.find(source)->path, "renamed.png");
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "renamed.png.meta"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "a.png"));
        EXPECT_EQ(selection.get_selected_asset(), selected);
    }

    TEST_F(ProjectPanelTest, RenameRejectsPathsAndDoesNotOverwriteExistingAsset) {
        const auto source = database.find("a.png")->handle;
        open_rename(3);
        rename("../escape");
        EXPECT_EQ(move_count, 0);
        rename("b");
        EXPECT_EQ(move_count, 1);
        EXPECT_EQ(database.find(source)->path, "a.png");
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "a.png.meta"));
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "b.png.meta"));
        EXPECT_TRUE(ImGui::FindWindowByName("Rename Asset")->Active);
    }

    TEST_F(ProjectPanelTest, RenameSameNameAndCancelDoNotMove) {
        open_rename(3);
        rename("a");
        EXPECT_EQ(move_count, 0);
        open_rename(3);
        auto* dialog = ImGui::FindWindowByName("Rename Asset");
        ImGui::ActivateItemByID(dialog->GetID("Cancel"));
        frame();
        EXPECT_EQ(move_count, 0);
    }

    TEST_F(ProjectPanelTest, DragMovesToFolderAndBackToRootAfterTraversal) {
        const auto source = database.find("a.png")->handle;
        begin_drag(3);
        drop(1);
        EXPECT_EQ(move_count, 1);
        EXPECT_EQ(database.find(source)->path, "folder/a.png");
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "folder/a.png.meta"));
        EXPECT_EQ(selection.get_selected_asset(), source);
        begin_drag(2);
        drop(0);
        EXPECT_EQ(move_count, 2);
        EXPECT_EQ(database.find(source)->path, "a.png");
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "a.png.meta"));
    }

    TEST_F(ProjectPanelTest, DragAcceptsClosedFolderAndIgnoresSameDirectory) {
        begin_drag(3);
        drop(0);
        EXPECT_EQ(move_count, 0);
        click(row_point(1));
        begin_drag(2);
        drop(1);
        EXPECT_EQ(move_count, 1);
        EXPECT_EQ(destination, "folder/a.png");
    }

    TEST_F(ProjectPanelTest, MeshReimportUsesContextTargetWithoutSelectionStatusUi) {
        std::ofstream(paths.assets() / "model.gltf") << "{}";
        project->update_scan_report(manager.scan());
        const auto* record = database.find("model.gltf");
        ASSERT_NE(record, nullptr);
        const auto handle = record->handle;
        const auto texture = database.find("a.png")->handle;
        selection.select_asset(texture);
        frame();
        const auto* window = ImGui::FindWindowByName("Project");
        ASSERT_NE(window, nullptr);
        const auto height = window->DC.CursorMaxPos.y;
        selection.select_asset(handle);
        frame();
        EXPECT_EQ(window->DC.CursorMaxPos.y, height);
        EXPECT_FALSE(project->take_mesh_reimport_request());
        selection.select_asset(texture);
        click(row_point(5), 1);
        auto& context = *ImGui::GetCurrentContext();
        ASSERT_EQ(context.OpenPopupStack.Size, 1);
        auto* popup = context.OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Reimport"));
        frame();
        const auto request = project->take_mesh_reimport_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(*request, handle);
        EXPECT_EQ(selection.get_selected_asset(), texture);
        EXPECT_FALSE(project->take_mesh_reimport_request());
        EXPECT_FALSE(std::filesystem::exists(paths.cache()));

        click(row_point(3), 1);
        ASSERT_EQ(context.OpenPopupStack.Size, 1);
        popup = context.OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Reimport"));
        frame();
        EXPECT_FALSE(project->take_mesh_reimport_request());
    }

    TEST_F(ProjectPanelTest, DragRejectsAssetChangedDuringGesture) {
        begin_drag(3);
        std::ofstream(paths.assets() / "a.png") << "changed content";
        ASSERT_TRUE(manager.scan().succeeded());
        drop(1);
        EXPECT_EQ(move_count, 0);
        EXPECT_TRUE(database.find("a.png"));
        EXPECT_FALSE(database.find("folder/a.png"));
    }
}
#endif
