#ifdef COMET_TEST_EDITOR_UI
#include "assets/project_panel.h"
#include "scene/selection.h"
#include "scene/command_history.h"
#include "assets/asset_reference.h"
#include "assets/source_operations.h"

#include "support/imgui_context.h"

#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <memory>

namespace CometEditor::Tests {
    class ProjectPanelTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui;
        Comet::Tests::TemporaryDirectory directory;
        const std::filesystem::path root = directory.path();
        Comet::ProjectPaths paths{root};
        Comet::AssetDatabase database{paths};
        Comet::Scene scene;
        CommandHistory history;
        SelectionService selection{scene};
        std::unique_ptr<ProjectPanel> project;
        int move_count = 0;
        int refresh_count = 0;
        bool consume_requests = true;
        Comet::AssetHandle moved_handle;
        std::filesystem::path destination;

        void SetUp() override {
            std::filesystem::create_directories(paths.assets() / "folder");
            std::ofstream(paths.assets() / "a.png") << "a";
            std::ofstream(paths.assets() / "b.png") << "b";
            std::ofstream(paths.assets() / "folder/c.png") << "c";
            auto report = database.scan();
            ASSERT_TRUE(report.succeeded());
            history.bind_scene(&scene);
            project = std::make_unique<ProjectPanel>(
                database, paths.assets(), std::move(report), selection, history);
            frame();
            frame();
        }

        void TearDown() override { project.reset(); }

        Comet::Result<void> move_to_fake_trash(const std::filesystem::path& entry) const {
            const auto destination = root / "fake-system-trash" / entry.filename();
            std::error_code error;
            std::filesystem::create_directories(destination.parent_path(), error);
            if(!error)
                std::filesystem::rename(entry, destination, error);
            if(error)
                return Comet::Result<void>::failure(error.message());
            return Comet::Result<void>::success();
        }

        void frame() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(420, 400));
            project->render();
            ImGui::Render();
            if(consume_requests) {
                if(const auto request = project->take_move_request()) {
                    ++move_count;
                    moved_handle = request->handle;
                    destination = request->destination;
                    project->complete_move(*request, AssetSourceOperations::move(database,
                                                         request->handle, request->destination));
                }
                if(project->take_refresh_request()) {
                    ++refresh_count;
                    project->update_scan_report(database.scan());
                }
            }
        }

        // 搜索框之后的顺序：assets、folder、c.png、a.png、b.png。
        ImVec2 row_point(int row) {
            const auto* window = ImGui::FindWindowByName("Project");
            return {window->WorkRect.Min.x + 70,
                window->WorkRect.Min.y + (row + 1) * ImGui::GetTextLineHeightWithSpacing()
                    + ImGui::GetTextLineHeight() * 0.5f};
        }

        void search(const char* query) {
            auto* window = ImGui::FindWindowByName("Project");
            ASSERT_NE(window, nullptr);
            ImGui::ActivateItemByID(window->GetID("##asset_search"));
            frame();
            auto& io = ImGui::GetIO();
            const auto primary = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(primary, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(primary, false);
            frame();
            if(*query)
                io.AddInputCharactersUTF8(query);
            else {
                io.AddKeyEvent(ImGuiKey_Backspace, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Backspace, false);
            }
            frame();
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
            const auto primary = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
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

    TEST_F(ProjectPanelTest, ExternalFileDropUsesFolderRowsAssetParentsAndRootBackground) {
        const auto directory_at = [&](const ImVec2 point) {
            return project->file_drop_directory({point.x, point.y});
        };
        EXPECT_EQ(directory_at(row_point(0)), std::filesystem::path{});
        EXPECT_EQ(directory_at(row_point(1)), std::filesystem::path("folder"));
        EXPECT_EQ(directory_at(row_point(2)), std::filesystem::path("folder"));
        EXPECT_EQ(directory_at({300, 350}), std::filesystem::path{});
        EXPECT_FALSE(directory_at({600, 350}));
        EXPECT_FALSE(directory_at({100, 5}));
        click(row_point(1));
        EXPECT_EQ(directory_at(row_point(1)), std::filesystem::path("folder"));
        project->set_visible(false);
        frame();
        EXPECT_FALSE(directory_at(row_point(1)));
    }

    TEST_F(ProjectPanelTest, SearchFiltersAssetsAndPreservesDirectoryMatches) {
        click(row_point(1));
        search("b.png");
        click(row_point(1));
        EXPECT_EQ(selection.get_selected_asset(), database.find("b.png")->handle);

        search("folder");
        click(row_point(2));
        EXPECT_EQ(selection.get_selected_asset(), database.find("folder/c.png")->handle);

        search("missing");
        EXPECT_EQ(selection.get_selected_asset(), database.find("folder/c.png")->handle);

        search("");
        click(row_point(3));
        EXPECT_EQ(selection.get_selected_asset(), database.find("a.png")->handle);
    }

    TEST_F(ProjectPanelTest, ExternalFileDropTargetsEmptyDirectoriesAndRejectsPopups) {
        std::filesystem::create_directories(paths.assets() / "empty");
        project->update_scan_report(database.scan());
        frame();
        const auto point = row_point(1);
        EXPECT_EQ(project->file_drop_directory({point.x, point.y}), std::filesystem::path("empty"));
        click({300, 350}, 1);
        EXPECT_FALSE(project->file_drop_directory({point.x, point.y}));
    }

    TEST_F(ProjectPanelTest, DeleteRequiresConfirmationAndClearsRemovedSelection) {
        const auto* record = database.find("a.png");
        ASSERT_NE(record, nullptr);
        const auto handle = record->handle;
        click(row_point(3));
        ASSERT_EQ(selection.get_selected_asset(), handle);

        const auto open_delete = [&] {
            click(row_point(3), 1);
            auto& context = *ImGui::GetCurrentContext();
            ASSERT_FALSE(context.OpenPopupStack.empty());
            auto* popup = context.OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            ImGui::ActivateItemByID(popup->GetID("Delete"));
            frame();
            frame();
            auto* dialog = ImGui::FindWindowByName("Delete Asset");
            ASSERT_NE(dialog, nullptr);
            ASSERT_TRUE(dialog->Active);
        };
        open_delete();
        auto* dialog = ImGui::FindWindowByName("Delete Asset");
        ImGui::ActivateItemByID(dialog->GetID("Cancel"));
        frame();
        EXPECT_FALSE(project->take_delete_request());
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "a.png"));

        open_delete();
        dialog = ImGui::FindWindowByName("Delete Asset");
        ImGui::ActivateItemByID(dialog->GetID("Move to Trash"));
        frame();
        const auto request = project->take_delete_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->handle, handle);
        EXPECT_EQ(request->revision, database.get_revision(handle));
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "a.png"));
        project->complete_delete(*request,
            AssetSourceOperations::remove_asset(database, request->handle,
                [this](const std::filesystem::path& entry) { return move_to_fake_trash(entry); }));
        frame();
        EXPECT_FALSE(database.find(handle));
        EXPECT_TRUE(std::filesystem::exists(root / "fake-system-trash/a.png"));
        EXPECT_TRUE(std::filesystem::exists(root / "fake-system-trash/a.png.meta"));
        EXPECT_EQ(selection.get_selected_asset(), Comet::INVALID_ASSET_HANDLE);
        EXPECT_FALSE(ImGui::IsPopupOpen("Delete Asset", ImGuiPopupFlags_AnyPopupId));
    }

    TEST_F(ProjectPanelTest, MeshDragKeepsOriginalIdentityAcrossDocumentChanges) {
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            paths.assets() / "model.gltf");
        project->update_scan_report(database.scan());
        frame();
        const auto handle = database.find("model.gltf")->handle;
        const auto revision = database.get_revision(handle);
        const auto generation = history.generation();
        begin_drag(5);
        const auto* payload = ImGui::GetDragDropPayload();
        ASSERT_NE(payload, nullptr);
        ASSERT_TRUE(payload->IsDataType(AssetDragPayload::TYPE));
        ASSERT_EQ(payload->DataSize, sizeof(AssetDragPayload));
        auto source = *static_cast<const AssetDragPayload*>(payload->Data);
        EXPECT_EQ(source.handle, handle);
        EXPECT_EQ(source.revision, revision);
        EXPECT_EQ(source.generation, generation);
        EXPECT_EQ(source.type, Comet::AssetType::Mesh);
        history.bind_scene(&scene);
        ASSERT_NE(history.generation(), generation);
        frame();
        payload = ImGui::GetDragDropPayload();
        ASSERT_NE(payload, nullptr);
        source = *static_cast<const AssetDragPayload*>(payload->Data);
        EXPECT_EQ(source.generation, generation);
        drop(1);
        EXPECT_EQ(move_count, 1);
        EXPECT_EQ(database.find(handle)->path, "folder/model.gltf");
    }

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

    TEST_F(ProjectPanelTest, RenameOnlyQueuesUntilTheOwnerExecutesIt) {
        consume_requests = false;
        const auto source = database.find("a.png")->handle;
        open_rename(3);
        rename("deferred");
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "a.png"));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "deferred.png"));
        EXPECT_EQ(move_count, 0);
        const auto request = project->take_move_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->handle, source);
        EXPECT_EQ(request->destination, "deferred.png");
        EXPECT_FALSE(project->take_move_request());
        project->complete_move(
            *request, AssetSourceOperations::move(database, request->handle, request->destination));
        frame();
        frame();
        EXPECT_FALSE(ImGui::FindWindowByName("Rename Asset")->Active);
        EXPECT_EQ(database.find(source)->path, "deferred.png");
    }

    TEST_F(ProjectPanelTest, RenameInputDoesNotTruncateLongUtf8Names) {
        consume_requests = false;
        open_rename(3);
        const std::string name = "长名称" + std::string(1100, 'n');
        rename(name.c_str());
        const auto request = project->take_move_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->destination, std::filesystem::path(name + ".png"));
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "a.png"));
    }

    TEST_F(ProjectPanelTest, RefreshOnlyQueuesUntilTheOwnerExecutesIt) {
        consume_requests = false;
        std::ofstream(paths.assets() / "new.png") << "new";
        click({300, 350}, 1);
        auto& context = *ImGui::GetCurrentContext();
        ASSERT_EQ(context.OpenPopupStack.Size, 1);
        auto* popup = context.OpenPopupStack.back().Window;
        ASSERT_NE(popup, nullptr);
        ImGui::ActivateItemByID(popup->GetID("Refresh"));
        frame();
        EXPECT_FALSE(database.find("new.png"));
        EXPECT_EQ(refresh_count, 0);
        ASSERT_TRUE(project->take_refresh_request());
        EXPECT_FALSE(project->take_refresh_request());
        project->update_scan_report(database.scan());
        EXPECT_TRUE(database.find("new.png"));
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
        const auto selected = database.find("b.png")->handle;
        selection.select_asset(selected);
        begin_drag(3);
        drop(1);
        EXPECT_EQ(move_count, 1);
        EXPECT_EQ(database.find(source)->path, "folder/a.png");
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "folder/a.png.meta"));
        EXPECT_EQ(selection.get_selected_asset(), selected);
        begin_drag(2);
        drop(0);
        EXPECT_EQ(move_count, 2);
        EXPECT_EQ(database.find(source)->path, "a.png");
        EXPECT_TRUE(std::filesystem::exists(paths.assets() / "a.png.meta"));
        EXPECT_EQ(selection.get_selected_asset(), selected);
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
        project->update_scan_report(database.scan());
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
        ASSERT_TRUE(database.scan().succeeded());
        drop(1);
        EXPECT_EQ(move_count, 0);
        EXPECT_TRUE(database.find("a.png"));
        EXPECT_FALSE(database.find("folder/a.png"));
    }
}
#endif
