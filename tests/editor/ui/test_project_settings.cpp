#ifdef COMET_TEST_EDITOR_UI
#include "project/input_settings_panel.h"
#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace CometEditor::Tests {
    TEST(ProjectSettingsUiTest, InputSettingsOpensAsNonModalPanel) {
        Comet::Tests::ImGuiTestContext imgui;
        InputSettingsPanel panel;
        EXPECT_FALSE(panel.is_open());

        panel.request(Comet::InputActions{});
        ImGui::NewFrame();
        panel.render();
        ImGui::Render();

        EXPECT_TRUE(panel.is_open());
        const auto* window = ImGui::FindWindowByName("Project Settings - Input");
        ASSERT_NE(window, nullptr);
        EXPECT_FALSE(window->Flags & ImGuiWindowFlags_Popup);
        EXPECT_FALSE(window->Flags & ImGuiWindowFlags_Modal);
        EXPECT_FALSE(panel.take_request());
    }
}
#endif
