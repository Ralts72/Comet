#pragma once

#include <imgui.h>
#include <string>

namespace CometUi {
    bool input_text(const char* label, std::string& value,
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);
}
