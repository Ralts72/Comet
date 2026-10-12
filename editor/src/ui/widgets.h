#pragma once

#include <imgui.h>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>

namespace CometEditor::Ui {
    template<typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] std::optional<Payload> read_drag_payload(const ImGuiPayload* payload) {
        if(!payload || !payload->IsDataType(Payload::TYPE) || !payload->Data
            || payload->DataSize != sizeof(Payload))
            return std::nullopt;
        Payload result;
        std::memcpy(&result, payload->Data, sizeof(result));
        return result;
    }

    bool input_text(const char* label, std::string& value,
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);
}
