#pragma once

#include "config/display_settings.h"

#include <optional>
#include <string>

namespace CometEditor {
    class DisplaySettingsPanel final {
    public:
        void request(Comet::DisplaySettings current);
        void close();
        void render();
        [[nodiscard]] std::optional<Comet::DisplaySettings> take_request();
        void complete(const Comet::Result<void>& result);

    private:
        Comet::DisplaySettings m_draft;
        std::optional<Comet::DisplaySettings> m_request;
        std::string m_error;
        bool m_open = false;
    };
}
