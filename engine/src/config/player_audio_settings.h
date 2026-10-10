#pragma once

#include "audio/audio_settings.h"
#include "common/uuid.h"

#include <filesystem>
#include <functional>

namespace Comet {
    class COMET_API PlayerAudioSettings final {
    public:
        [[nodiscard]] static Result<PlayerAudioSettings> load(
            Uuid project_id, AudioSettings defaults);
        [[nodiscard]] static Result<PlayerAudioSettings> load(
            Uuid project_id, AudioSettings defaults, const std::filesystem::path& path);
        [[nodiscard]] const AudioSettings& settings() const { return m_settings; }
        [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
        [[nodiscard]] Result<void> save(AudioSettings settings);
        [[nodiscard]] Result<void> save_and_apply(
            AudioSettings settings, const std::function<Result<void>(const AudioSettings&)>& apply);

    private:
        Uuid m_project_id;
        AudioSettings m_settings;
        std::filesystem::path m_path;
    };
}
