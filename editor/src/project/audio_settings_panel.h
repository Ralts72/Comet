#pragma once

#include "audio/audio_settings.h"

#include <optional>
#include <string>

namespace CometEditor {
    class AudioSettingsPanel final {
    public:
        void request(Comet::AudioSettings current);
        void close();
        void render();
        [[nodiscard]] std::optional<Comet::AudioSettings> take_request();
        void complete(const Comet::Result<void>& result);

    private:
        Comet::AudioSettings m_draft;
        std::optional<Comet::AudioSettings> m_request;
        std::string m_error;
        bool m_open = false;
    };
}
