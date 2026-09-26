#pragma once

#include <imgui.h>
#include "common/result.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace CometEditor {
    class EditorShortcuts {
    public:
        enum class Action {
            NewScene,
            OpenScene,
            SaveScene,
            Undo,
            Redo,
            CopyEntity,
            PasteEntity,
            DeleteSelection,
            FocusSelection
        };

        EditorShortcuts();
        [[nodiscard]] static Comet::Result<EditorShortcuts> load(const std::filesystem::path& path);
        [[nodiscard]] static Comet::Result<EditorShortcuts> parse(std::string_view yaml);

        // 调用者负责模式、焦点、文本输入及拖动等上下文限制。
        [[nodiscard]] bool pressed(Action action, ImGuiInputFlags flags) const;
        [[nodiscard]] std::string label(Action action, bool mac) const;

    private:
        struct Binding {
            ImGuiKeyChord chord;
            std::string key_name;
        };

        [[nodiscard]] static Comet::Result<Binding> parse_binding(std::string_view text);
        std::array<std::vector<Binding>, 9> m_bindings;
    };
}
