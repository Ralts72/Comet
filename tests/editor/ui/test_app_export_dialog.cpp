#ifdef COMET_TEST_EDITOR_UI
#include "project/app_export_dialog.h"
#include "support/imgui_context.h"
#include <gtest/gtest.h>
#include <imgui_internal.h>

namespace CometEditor::Tests {
    class AppExportDialogTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui;
        AppExportDialog dialog;
        bool modified = false;
        bool edit = true;
        void frame() {
            ImGui::NewFrame();
            dialog.render(modified, edit);
            ImGui::Render();
        }
        void SetUp() override {
            dialog.open("/projects/中文 game", "/build/Release", "scenes/start.scene",
                {"scenes/second.scene", "scenes/start.scene"});
            frame();
            frame();
        }
        void activate(const char* item) {
            auto* window = ImGui::FindWindowByName("Export App");
            ASSERT_NE(window, nullptr);
            ImGui::ActivateItemByID(window->GetID(item));
            frame();
        }
    };

    TEST_F(AppExportDialogTest, UnsavedAndPlayBlockExportAndFailureCanBeRetried) {
        modified = true;
        activate("Export");
        EXPECT_FALSE(dialog.take_request());
        modified = false;
        edit = false;
        activate("Export");
        EXPECT_FALSE(dialog.take_request());
        edit = true;
        activate("Export");
        auto request = dialog.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->project, "/projects/中文 game");
        EXPECT_EQ(request->destination, "/projects/中文 game.app");
        EXPECT_EQ(request->build, "/build/Release");
        EXPECT_TRUE(request->scenes.empty());
        activate("Export");
        EXPECT_FALSE(dialog.take_request());
        dialog.complete(Comet::Result<void>::failure("Destination exists"));
        activate("Export");
        EXPECT_TRUE(dialog.take_request());
        dialog.complete(Comet::Result<void>::success());
        activate("Close");
        frame();
        EXPECT_FALSE(ImGui::FindWindowByName("Export App")->Active);
    }

    TEST_F(AppExportDialogTest, SelectedAdditionalSceneIsPassedWithoutDuplicatingStartup) {
        auto* window = ImGui::FindWindowByName("Export App");
        ASSERT_NE(window, nullptr);
        const auto child_id = window->GetID("Scenes");
        ImGuiWindow* scenes = nullptr;
        for(auto* child : window->DC.ChildWindows)
            if(child->ChildId == child_id)
                scenes = child;
        ASSERT_NE(scenes, nullptr);
        ImGui::ActivateItemByID(scenes->GetID("scenes/second.scene"));
        frame();
        activate("Export");
        const auto request = dialog.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(request->scenes, (std::vector<std::filesystem::path>{"scenes/second.scene"}));
    }
}
#endif
