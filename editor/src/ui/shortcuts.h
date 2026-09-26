#pragma once

#include <imgui.h>
#include "common/result.h"

#include <array>
#include <cstddef>
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
        static constexpr std::size_t ACTION_COUNT =
            static_cast<std::size_t>(Action::FocusSelection) + 1;
        using BindingTexts = std::array<std::vector<std::string>, ACTION_COUNT>;

        EditorShortcuts();
        [[nodiscard]] static Comet::Result<EditorShortcuts> load(const std::filesystem::path& path);
        [[nodiscard]] static Comet::Result<EditorShortcuts> parse(std::string_view yaml);
        [[nodiscard]] static Comet::Result<EditorShortcuts> from_texts(BindingTexts texts);
        [[nodiscard]] Comet::Result<void> save_overrides(const std::filesystem::path& path) const;
        [[nodiscard]] BindingTexts binding_texts() const;
        [[nodiscard]] static std::string_view action_name(Action action);

        // 调用者负责模式、焦点、文本输入及拖动等上下文限制。
        [[nodiscard]] bool pressed(Action action, ImGuiInputFlags flags) const;
        [[nodiscard]] std::string label(Action action, bool mac) const;

    private:
        struct Binding {
            ImGuiKeyChord chord;
            std::string key_name;
        };

        [[nodiscard]] static Comet::Result<Binding> parse_binding(std::string_view text);
        [[nodiscard]] static Comet::Result<void> validate_conflicts(
            const EditorShortcuts& shortcuts);
        std::array<std::vector<Binding>, ACTION_COUNT> m_bindings;
    };
}
