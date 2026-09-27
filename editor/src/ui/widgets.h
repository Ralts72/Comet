#pragma once

#include <imgui.h>
#include <string>

namespace CometEditor::Ui {
    bool input_text(const char* label, std::string& value,
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);
}
