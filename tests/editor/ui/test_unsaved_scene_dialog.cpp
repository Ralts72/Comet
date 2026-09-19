#ifdef COMET_TEST_EDITOR_UI
#include "ui/dialogs.h"
#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>

namespace CometEditor::Tests {
    class UnsavedSceneDialogTest: public testing::TestWithParam<SceneDocument::Decision> {
    protected:
        Comet::Tests::ImGuiTestContext imgui;
        bool needs_confirmation = false;
        std::optional<SceneDocument::Decision> decision;

        void frame() {
            ImGui::NewFrame();
            if(const auto selected = draw_unsaved_scene_dialog(needs_confirmation)) {
                EXPECT_FALSE(decision);
                decision = selected;
                needs_confirmation = false;
            }
            ImGui::Render();
        }
    };

    TEST_P(UnsavedSceneDialogTest, OnlyReturnsTheClickedDecisionAndCloses) {
        frame();
        EXPECT_FALSE(decision);
        EXPECT_EQ(ImGui::FindWindowByName("Unsaved Scene"), nullptr);
        needs_confirmation = true;
        frame();
        frame();
        EXPECT_FALSE(decision);
        const auto* window = ImGui::FindWindowByName("Unsaved Scene");
        ASSERT_NE(window, nullptr);
        ASSERT_TRUE(window->Active);
        const auto& style = ImGui::GetStyle();
        float x = window->DC.CursorStartPos.x;
        const char* label = "Save";
        if(GetParam() != SceneDocument::Decision::Save) {
            x += ImGui::CalcTextSize("Save").x + 2 * style.FramePadding.x + style.ItemSpacing.x;
            label = "Discard";
        }
        if(GetParam() == SceneDocument::Decision::Cancel) {
            x += ImGui::CalcTextSize("Discard").x + 2 * style.FramePadding.x + style.ItemSpacing.x;
            label = "Cancel";
        }
        x += (ImGui::CalcTextSize(label).x + 2 * style.FramePadding.x) / 2;
        const float y = window->DC.CursorStartPos.y + ImGui::GetTextLineHeight()
                        + style.ItemSpacing.y + ImGui::GetFrameHeight() / 2;
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        ASSERT_TRUE(decision);
        EXPECT_EQ(*decision, GetParam());
        frame();
        EXPECT_FALSE(ImGui::FindWindowByName("Unsaved Scene")->Active);
    }

    INSTANTIATE_TEST_SUITE_P(SaveDiscardCancel, UnsavedSceneDialogTest,
        testing::Values(SceneDocument::Decision::Save, SceneDocument::Decision::Discard,
            SceneDocument::Decision::Cancel));
}
#endif
