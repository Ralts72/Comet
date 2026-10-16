#pragma once

#include "config/display_settings_preview.h"
#include "input/player_input_settings.h"
#include "ui/project_ui.h"

#include <memory>

namespace Comet {
    class Engine;

    // App 与 Editor Play 共用玩家设置；宿主只指定实际运行目标。
    class COMET_API PlayerSettingsOwner final {
    public:
        struct Startup {
            PlayerDisplaySettings display;
            PlayerQualitySettings quality;
            PlayerAudioSettings audio;
        };
        enum class Host { App, Play };
        struct Targets {
            Host host = Host::Play;
            std::function<bool()> input_active;
            std::function<Result<void, Error>(InputActions)> configure_input;
            std::function<Result<void, Error>(InputActions)> rebind_input;
            std::function<DisplaySettings(DisplaySettings)> current_display;
            std::function<Result<void>(const DisplaySettings&)> apply_display;
            std::function<bool()> output_pending;
            std::function<Result<void, Error>(const QualitySettings&)> apply_quality;
            std::function<Result<void, Error>(const AudioSettings&)> apply_audio;
            std::function<AudioSettings()> active_audio;
        };

        [[nodiscard]] static Result<Startup> load_startup(const Project& project,
            const std::optional<std::filesystem::path>& directory = std::nullopt);
        explicit PlayerSettingsOwner(
            const Project& project, std::optional<std::filesystem::path> directory = std::nullopt);
        PlayerSettingsOwner(const Project& project, Startup startup);
        PlayerSettingsOwner(const PlayerSettingsOwner&) = delete;
        PlayerSettingsOwner& operator=(const PlayerSettingsOwner&) = delete;

        void bind_app(Engine& engine);
        void bind_play(Engine& engine, std::function<void(Math::Vec2u)> set_resolution,
            std::function<bool()> play_active);
        void bind(Targets targets);
        [[nodiscard]] Ui::ProjectUi::Services ui_services();

        [[nodiscard]] Result<InputOverrides> load_input();
        [[nodiscard]] Result<DisplaySettings> load_display();
        [[nodiscard]] Result<QualitySettings> load_quality();
        [[nodiscard]] Result<AudioSettings> load_audio();
        [[nodiscard]] Result<void> apply_input(InputOverrides overrides);
        [[nodiscard]] Result<void, Error> configure_input();
        [[nodiscard]] Result<void, Error> activate_quality();
        [[nodiscard]] Result<void, Error> activate_audio();
        [[nodiscard]] Result<void> expire_display();
        [[nodiscard]] Result<void> revert_display();
        [[nodiscard]] Result<void> save_window_state();

        void reset_input() { m_input.reset(); }
        void reset_display();
        void reset_quality() { m_quality.reset(); }
        void reset_audio() { m_audio.reset(); }

    private:
        [[nodiscard]] Result<void> apply_display(DisplaySettings settings);
        [[nodiscard]] Result<void> apply_quality(QualitySettings settings);
        [[nodiscard]] Result<void> apply_audio(AudioSettings settings);
        [[nodiscard]] Result<void> ensure_display();
        [[nodiscard]] DisplaySettings current_display() const;

        const Project& m_project;
        std::optional<std::filesystem::path> m_directory;
        Targets m_targets;
        std::optional<PlayerInputSettings> m_input;
        std::optional<PlayerDisplaySettings> m_display;
        std::unique_ptr<DisplaySettingsPreview> m_display_preview;
        std::optional<PlayerQualitySettings> m_quality;
        std::optional<PlayerAudioSettings> m_audio;
    };
}
