#ifdef COMET_TEST_EDITOR_UI
#include "ui/editor_panel.h"
#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>

namespace Comet::Tests {
    TEST(EditorTextTest, ChineseCaptionsReuseSavedWindowAndHeaderState) {
        ImGuiTestContext imgui;
        class Panel final: public CometEditor::EditorPanel {
        public:
            Panel() : EditorPanel("属性###Inspector") {}
            void render() override {
                ImGui::Begin(window_label().c_str());
                expanded = ImGui::CollapsingHeader("相机###Camera");
                ImGui::End();
            }
            bool expanded = false;
        } panel;

        ImGui::NewFrame();
        ImGui::Begin("Inspector");
        ImGui::CollapsingHeader("Camera");
        ImGui::End();
        ImGui::Render();
        auto* original = ImGui::FindWindowByName("Inspector");
        ASSERT_NE(original, nullptr);
        original->StateStorage.SetInt(original->GetID("Camera"), 1);

        ImGui::NewFrame();
        panel.render();
        ImGui::Render();
        EXPECT_EQ(ImGui::FindWindowByName("Inspector"), original);
        EXPECT_EQ(ImHashStr(panel.window_label().c_str()), ImHashStr("Inspector"));
        EXPECT_TRUE(panel.expanded);
    }
}
#endif
