#include "ui/widgets.h"

namespace CometEditor::Ui {
    namespace {
        int resize_text(ImGuiInputTextCallbackData* data) {
            auto& text = *static_cast<std::string*>(data->UserData);
            text.resize(static_cast<std::size_t>(data->BufTextLen));
            data->Buf = text.data();
            return 0;
        }
    }

    bool input_text(const char* label, std::string& value, const ImGuiInputTextFlags flags) {
        return ImGui::InputText(label, value.data(), value.capacity() + 1,
            flags | ImGuiInputTextFlags_CallbackResize, resize_text, &value);
    }
}
