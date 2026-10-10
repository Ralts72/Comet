#pragma once

#include "render/quality_settings.h"

#include <optional>
#include <string>

namespace CometEditor {
    class QualitySettingsPanel final {
    public:
        void request(Comet::QualitySettings current);
        void close();
        void render();
        [[nodiscard]] std::optional<Comet::QualitySettings> take_request();
        void complete(const Comet::Result<void>& result);

    private:
        Comet::QualitySettings m_draft;
        std::optional<Comet::QualitySettings> m_request;
        std::string m_error;
        bool m_open = false;
    };
}
