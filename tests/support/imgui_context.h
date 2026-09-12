#pragma once

#include <imgui.h>

namespace Comet::Tests {
    class ImGuiTestContext {
    public:
        explicit ImGuiTestContext(ImVec2 display_size = {800, 600}) {
            m_context = ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = display_size;
            io.DeltaTime = 1.0f / 60;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
        ~ImGuiTestContext() { ImGui::DestroyContext(m_context); }
        ImGuiTestContext(const ImGuiTestContext&) = delete;
        ImGuiTestContext& operator=(const ImGuiTestContext&) = delete;

    private:
        ImGuiContext* m_context;
    };
}
