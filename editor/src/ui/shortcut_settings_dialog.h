#pragma once

#include "ui/shortcuts.h"

#include <array>
#include <optional>
#include <string>

namespace CometEditor {
    class ShortcutSettingsDialog {
    public:
        void request(const EditorShortcuts& current);
        void render();
        [[nodiscard]] std::optional<EditorShortcuts> take_request();
        void complete(const Comet::Result<void>& result);

    private:
        void load_draft(const EditorShortcuts& shortcuts);
        [[nodiscard]] Comet::Result<EditorShortcuts> build_shortcuts() const;

        std::array<std::string, EditorShortcuts::ACTION_COUNT> m_draft;
        std::optional<EditorShortcuts> m_request;
        std::string m_error;
        bool m_active = false;
        bool m_open_requested = false;
        bool m_close_requested = false;
    };
}
