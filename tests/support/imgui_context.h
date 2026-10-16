#pragma once

#include <imgui_internal.h>

#include <functional>
#include <optional>

namespace Comet::Tests {
    inline void click_imgui(ImVec2 point, const std::function<void()>& frame,
        ImGuiMouseButton button = ImGuiMouseButton_Left,
        const std::function<void(bool)>& on_button = {}) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        frame();
        for(const bool pressed : {true, false}) {
            io.AddMouseButtonEvent(button, pressed);
            if(on_button)
                on_button(pressed);
            frame();
        }
    }

    inline std::optional<ImVec2> find_imgui_item(
        ImGuiID id, const ImRect& area, const std::function<void()>& frame, float step = 3) {
        for(float y = area.Min.y; y < area.Max.y; y += step)
            for(float x = area.Min.x; x < area.Max.x; x += step) {
                ImGui::GetIO().AddMousePosEvent(x, y);
                frame();
                if(ImGui::GetCurrentContext()->HoveredId == id)
                    return ImVec2(x, y);
            }
        return std::nullopt;
    }

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
