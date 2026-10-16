#pragma once

#include "config/display_settings_preview.h"
#include "input/player_input_settings.h"
#include "ui/project_ui.h"

#include <memory>

namespace Comet {
    class Engine;

    // App／Play 的宿主设置装配：将独立设置存储接到 Engine 和项目 UI。
    class COMET_API PlayerSettingsOwner final {
    public:
        struct Startup {
            PlayerDisplaySettings display;
            PlayerQualitySettings quality;
            PlayerAudioSettings audio;
        };

        [[nodiscard]] static Result<Startup> load_startup(const Project& project,
            const std::optional<std::filesystem::path>& directory = std::nullopt);
        explicit PlayerSettingsOwner(
            const Project& project, std::optional<std::filesystem::path> directory = std::nullopt);
        PlayerSettingsOwner(const Project& project, Startup startup);
        PlayerSettingsOwner(const PlayerSettingsOwner&) = delete;
        PlayerSettingsOwner& operator=(const PlayerSettingsOwner&) = delete;

        // 先绑定宿主，再应用设置；Engine 的寿命须覆盖本对象及其 UI 服务。
        void bind_app(Engine& engine);
        void bind_play(Engine& engine, std::function<void(Math::Vec2u)> set_resolution,
            std::function<bool()> play_active);
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
        [[nodiscard]] Result<void> apply_display_target(const DisplaySettings& settings);
        [[nodiscard]] Engine& engine() const;
        [[nodiscard]] bool is_play() const { return bool(m_set_play_resolution); }

        const Project& m_project;
        std::optional<std::filesystem::path> m_directory;
        Engine* m_engine = nullptr;
        std::function<void(Math::Vec2u)> m_set_play_resolution;
        std::function<bool()> m_play_active;
        std::optional<PlayerInputSettings> m_input;
        std::optional<PlayerDisplaySettings> m_display;
        std::unique_ptr<DisplaySettingsPreview> m_display_preview;
        std::optional<PlayerQualitySettings> m_quality;
        std::optional<PlayerAudioSettings> m_audio;
    };
}
