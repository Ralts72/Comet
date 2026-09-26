#ifdef COMET_TEST_EDITOR_UI
#include "ui/path_dialog.h"
#include "project/project_name_dialog.h"
#include "scene/scene_document.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "core/project.h"
#include "support/imgui_context.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>

namespace CometEditor::Tests {
    class PathDialogTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui;
        Comet::Tests::TemporaryDirectory directory;
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        Comet::SceneSerializer serializer{components};
        std::unique_ptr<Comet::Scene> active = std::make_unique<Comet::Scene>();
        int installations = 0;
        CommandHistory history;
        SceneDocument document{serializer, Comet::ProjectPaths(directory.path()), history,
            [this] { return active.get(); },
            [this](std::unique_ptr<Comet::Scene> scene) {
                ++installations;
                active.swap(scene);
                return Comet::Result<void, Comet::Error>::success();
            }};
        PathDialog dialog;
        const std::filesystem::path path = directory.path() / "assets/untitled.scene";

        void frame() {
            ImGui::NewFrame();
            dialog.render();
            ImGui::Render();
        }

        void show(PathDialog::Action action) {
            dialog.request(action, path, path.parent_path());
            frame();
            frame();
        }

        void click(const char* title, bool cancel = false) {
            const auto* window = ImGui::FindWindowByName(title);
            ASSERT_NE(window, nullptr);
            ASSERT_TRUE(window->Active);
            const auto& style = ImGui::GetStyle();
            const float height = ImGui::GetFontSize() + style.FramePadding.y * 2;
            const ImVec2 point(
                window->DC.CursorStartPos.x + 50 + (cancel ? 100 + style.ItemSpacing.x : 0),
                window->DC.CursorStartPos.y + height + style.ItemSpacing.y + height / 2);
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        }
    };

    TEST_F(PathDialogTest, SaveQueuesOnceWithoutWritingDuringRendering) {
        show(PathDialog::Action::SaveScene);
        click("Save Scene");
        EXPECT_FALSE(std::filesystem::exists(path));
        const auto request = dialog.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->action, PathDialog::Action::SaveScene);
        EXPECT_EQ(request->path, path.string());
        EXPECT_FALSE(dialog.take_request());
        frame();
        EXPECT_FALSE(dialog.take_request());

        const auto saved = document.save(request->path);
        ASSERT_TRUE(saved);
        dialog.complete(saved);
        frame();
        frame();
        EXPECT_FALSE(ImGui::FindWindowByName("Save Scene")->Active);
        EXPECT_TRUE(std::filesystem::exists(path));
    }

    TEST_F(PathDialogTest, FailedOpenStaysOpenAndRetryInstallsOnlyWhenExecuted) {
        show(PathDialog::Action::OpenScene);
        click("Open Scene");
        const auto request = dialog.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->action, PathDialog::Action::OpenScene);
        EXPECT_EQ(installations, 0);
        const auto* original = active.get();
        const auto failed = document.open(request->path);
        ASSERT_FALSE(failed);
        dialog.complete(failed);
        frame();
        frame();
        EXPECT_TRUE(ImGui::FindWindowByName("Open Scene")->Active);
        EXPECT_FALSE(dialog.take_request());
        EXPECT_EQ(active.get(), original);

        ASSERT_TRUE(document.save(path.string()));
        click("Open Scene");
        const auto retry = dialog.take_request();
        ASSERT_TRUE(retry);
        EXPECT_EQ(retry->path, request->path);
        EXPECT_EQ(installations, 0);
        const auto opened = document.open(retry->path);
        ASSERT_TRUE(opened);
        EXPECT_EQ(installations, 1);
        dialog.complete(opened);
        frame();
        frame();
        EXPECT_FALSE(ImGui::FindWindowByName("Open Scene")->Active);
    }

    TEST_F(PathDialogTest, CancelClosesWithoutRequestOrFileOperation) {
        show(PathDialog::Action::SaveScene);
        click("Save Scene", true);
        frame();
        EXPECT_FALSE(dialog.take_request());
        EXPECT_FALSE(ImGui::FindWindowByName("Save Scene")->Active);
        EXPECT_FALSE(std::filesystem::exists(path));
        EXPECT_EQ(installations, 0);
    }

    TEST_F(PathDialogTest, ProjectOpenReportsFailureWithoutClosingTheDialog) {
        dialog.request(PathDialog::Action::OpenProject, directory.path(), directory.path());
        frame();
        frame();
        click("Open Project");
        const auto request = dialog.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->action, PathDialog::Action::OpenProject);
        EXPECT_EQ(request->path, directory.path().string());
        auto candidate = Comet::Project::load(request->path);
        ASSERT_FALSE(candidate);
        dialog.complete(Comet::Result<void, Comet::Error>::failure({candidate.error()}));
        frame();
        frame();
        EXPECT_TRUE(ImGui::FindWindowByName("Open Project")->Active);
        EXPECT_EQ(installations, 0);
    }

    TEST(ProjectNameDialogTest, FailureKeepsDialogOpenUntilSaveSucceeds) {
        Comet::Tests::ImGuiTestContext imgui;
        ProjectNameDialog dialog;
        const auto frame = [&] {
            ImGui::NewFrame();
            dialog.render();
            ImGui::Render();
        };
        dialog.request("Old Name");
        frame();
        frame();
        auto* popup = ImGui::FindWindowByName("Rename Project");
        ASSERT_NE(popup, nullptr);
        ASSERT_TRUE(popup->Active);

        ImGui::ActivateItemByID(popup->GetID("Rename"));
        frame();
        EXPECT_EQ(dialog.take_request(), "Old Name");
        dialog.complete(Comet::Result<void>::failure("Invalid name"));
        frame();
        EXPECT_TRUE(popup->Active);

        ImGui::ActivateItemByID(popup->GetID("Rename"));
        frame();
        EXPECT_EQ(dialog.take_request(), "Old Name");
        dialog.complete(Comet::Result<void>::success());
        frame();
        frame();
        EXPECT_FALSE(popup->Active);
    }
}
#endif
