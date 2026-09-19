#ifdef COMET_TEST_EDITOR_UI

#include "ui/console.h"

#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui.h>

#include <atomic>
#include <thread>

namespace CometEditor::Tests {
    class ConsolePanelTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui;

        void render_frame() {
            ImGui::NewFrame();
            ImGui::SetNextWindowSize(ImVec2(700, 500));
            panel.render();
            ImGui::Render();
        }

        ConsolePanel panel;
    };

    TEST_F(ConsolePanelTest, RendersAfterAppendClearAndVisibilityChanges) {
        panel.add_log(Comet::LogLevel::Info, "first");
        render_frame();
        render_frame();
        EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
        panel.clear_logs();
        render_frame();
        panel.set_visible(false);
        panel.add_log(Comet::LogLevel::Error, "added while hidden");
        render_frame();
        EXPECT_EQ(ImGui::GetDrawData()->TotalVtxCount, 0);
        panel.set_visible(true);
        render_frame();
        EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
    }

    TEST_F(ConsolePanelTest, AcceptsWorkerLogsWhileOwnerRendersAndClears) {
        std::atomic_bool start = false;
        std::thread producer([&] {
            start.wait(false);
            for(int i = 0; i < 12000; ++i) {
                panel.add_log(Comet::LogLevel::Info, "worker log");
            }
        });
        start.store(true);
        start.notify_one();
        for(int i = 0; i < 30; ++i) {
            panel.clear_logs();
            render_frame();
        }
        producer.join();
        render_frame();
        EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
    }
}

#endif
