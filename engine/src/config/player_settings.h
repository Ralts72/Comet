#pragma once

#include "audio/audio_settings.h"
#include "common/uuid.h"
#include "config/display_settings.h"
#include "render/quality_settings.h"

#include <filesystem>
#include <functional>

namespace Comet {
    template<typename T> class COMET_API PlayerSettings final {
    public:
        [[nodiscard]] static Result<PlayerSettings> load(Uuid project_id, T defaults);
        [[nodiscard]] static Result<PlayerSettings> load(
            Uuid project_id, T defaults, const std::filesystem::path& path);
        [[nodiscard]] const T& settings() const { return m_settings; }
        [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
        [[nodiscard]] Result<void> save(T settings);
        [[nodiscard]] Result<void> save_and_apply(
            T settings, const std::function<Result<void>(const T&)>& apply);

    private:
        Uuid m_project_id;
        T m_settings;
        std::filesystem::path m_path;
    };

    extern template class PlayerSettings<AudioSettings>;
    extern template class PlayerSettings<DisplaySettings>;
    extern template class PlayerSettings<QualitySettings>;

    using PlayerAudioSettings = PlayerSettings<AudioSettings>;
    using PlayerDisplaySettings = PlayerSettings<DisplaySettings>;
    using PlayerQualitySettings = PlayerSettings<QualitySettings>;
}
