#ifdef COMET_TEST_EDITOR_UI
#include "project/input_settings_panel.h"
#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace CometEditor::Tests {
    class ProjectInputUiTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui{{1200, 800}};
        InputSettingsPanel panel;
        Comet::InputActions original;
        bool shortcut_triggered = false;

        void SetUp() override {
            auto actions = Comet::InputActions::create(
                {{"jump", Comet::InputActions::Type::Button, {{Comet::Input::Key::Space}}},
                    {"interact", Comet::InputActions::Type::Button, {{Comet::Input::Key::S}}}});
            ASSERT_TRUE(actions);
            original = std::move(actions).value();
            panel.request(original);
            frame();
            frame();
        }

        void frame() {
            ImGui::NewFrame();
            shortcut_triggered = ImGui::Shortcut(ImGuiKey_S, ImGuiInputFlags_RouteGlobal);
            ImGui::SetNextWindowPos({10, 20});
            panel.render();
            shortcut_triggered |= ImGui::Shortcut(ImGuiKey_S, ImGuiInputFlags_RouteGlobal);
            ImGui::Render();
        }

        ImGuiWindow* window() { return ImGui::FindWindowByName("Project Settings - Input"); }

        ImGuiWindow* details() {
            for(auto* child : ImGui::GetCurrentContext()->Windows)
                if(child->ParentWindow == window()
                    && child->ChildId == window()->GetID("ActionDetails"))
                    return child;
            return nullptr;
        }

        ImGuiID binding_id(const char* label) {
            const int first = 0;
            auto id = ImHashData(&first, sizeof(first), details()->ID);
            id = ImHashStr("Bindings", 0, id);
            id = ImHashData(&first, sizeof(first), id);
            return ImHashStr(label, 0, id);
        }

        void press(const ImGuiKey key) {
            ImGui::GetIO().AddKeyEvent(key, true);
            frame();
            ImGui::GetIO().AddKeyEvent(key, false);
        }

        void button(const char* label) {
            ImGui::ActivateItemByID(window()->GetID(label));
            frame();
        }

        void record() {
            ASSERT_NE(details(), nullptr);
            ImGui::FocusWindow(details());
            ImGui::ActivateItemByID(binding_id("Record Key"));
            frame();
            ASSERT_EQ(ImGui::GetActiveID(), window()->GetID("KeyCapture"));
        }

        void edit_control(const char* text) {
            ImGui::FocusWindow(details());
            const auto id = binding_id("##Control");
            ImGui::ActivateItemByID(id);
            frame();
            ASSERT_EQ(ImGui::GetActiveID(), id);
            auto& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            press(ImGuiKey_A);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            io.AddInputCharactersUTF8(text);
            frame();
            press(ImGuiKey_Enter);
            frame();
        }
    };

    TEST_F(ProjectInputUiTest, RecordingConsumesShortcutsAndAllowsSharedBindings) {
        record();
        press(ImGuiKey_S);
        EXPECT_FALSE(shortcut_triggered);
        frame();
        press(ImGuiKey_S);
        EXPECT_TRUE(shortcut_triggered);
        frame();
        button("Save");
        auto saved = panel.take_request();
        ASSERT_TRUE(saved);
        EXPECT_EQ(std::get<Comet::Input::Key>(saved->actions()[0].bindings[0].control),
            Comet::Input::Key::S);
        EXPECT_EQ(saved->actions()[0].bindings[0], saved->actions()[1].bindings[0]);
        EXPECT_FALSE(panel.take_request());

        record();
        press(ImGuiKey_Escape);
        frame();
        button("Save");
        auto cancelled = panel.take_request();
        ASSERT_TRUE(cancelled);
        EXPECT_EQ(*cancelled, *saved);
    }

    TEST_F(ProjectInputUiTest, LosingFocusOrClosingCancelsRecording) {
        record();
        ImGui::FocusWindow(nullptr);
        frame();
        press(ImGuiKey_K);
        frame();
        button("Save");
        auto saved = panel.take_request();
        ASSERT_TRUE(saved);
        EXPECT_EQ(*saved, original);

        record();
        button("Close");
        EXPECT_FALSE(panel.is_open());
        EXPECT_NE(ImGui::GetActiveID(), window()->GetID("KeyCapture"));
    }

    TEST_F(ProjectInputUiTest, InvalidDraftAndFailedSaveRemainEditableWhileCloseDiscardsDraft) {
        edit_control("unknown_control_longer_than_the_initial_string_capacity");
        button("Save");
        EXPECT_FALSE(panel.take_request());
        EXPECT_TRUE(panel.is_open());

        edit_control("RightControl");
        button("Save");
        auto saved = panel.take_request();
        ASSERT_TRUE(saved);
        EXPECT_EQ(std::get<Comet::Input::Key>(saved->actions()[0].bindings[0].control),
            Comet::Input::Key::RightControl);
        panel.complete(Comet::Result<void>::failure("Project file changed since it was loaded"));
        frame();
        EXPECT_TRUE(panel.is_open());
        button("Save");
        auto retry = panel.take_request();
        ASSERT_TRUE(retry);
        EXPECT_EQ(*retry, *saved);

        button("Close");
        EXPECT_FALSE(panel.is_open());
        EXPECT_FALSE(panel.take_request());
        panel.request(original);
        frame();
        button("Save");
        auto reopened = panel.take_request();
        ASSERT_TRUE(reopened);
        EXPECT_EQ(*reopened, original);
    }

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
