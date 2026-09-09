#pragma once

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace CometEditor {
    class EditorShortcuts {
    public:
        enum class Action { NewScene, OpenScene, SaveScene, Undo, Redo, FocusSelection };

        EditorShortcuts();
        [[nodiscard]] static EditorShortcuts load(const std::filesystem::path& path);
        [[nodiscard]] static EditorShortcuts parse(std::string_view yaml);

        // 调用者负责模式、焦点、文本输入及拖动等上下文限制。
        [[nodiscard]] bool pressed(Action action, ImGuiInputFlags flags) const;
        [[nodiscard]] std::string label(Action action, bool mac) const;

    private:
        struct Binding {
            ImGuiKeyChord chord;
            std::string key_name;
        };

        [[nodiscard]] static Binding parse_binding(std::string_view text);
        std::array<std::vector<Binding>, 6> m_bindings;
    };
}
