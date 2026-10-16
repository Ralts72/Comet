#include "runtime/player_settings_owner.h"

#include "core/engine.h"
#include "core/window.h"
#include "diagnostics/logger.h"
#include "render/renderer.h"

#include <utility>

namespace Comet {
    namespace {
        template<typename T>
        Result<PlayerSettings<T>> load_settings(const Project& project, T defaults,
            const std::optional<std::filesystem::path>& directory, std::string_view name) {
            if(directory)
                return PlayerSettings<T>::load(
                    project.id(), defaults, *directory / (std::string(name) + ".json"));
            return PlayerSettings<T>::load(project.id(), defaults);
        }

        Result<void> ui_result(const Result<void, Error>& result) {
            if(!result)
                return Result<void>::failure(result.error().message);
            return Result<void>::success();
        }
    }

    Result<PlayerSettingsOwner::Startup> PlayerSettingsOwner::load_startup(
        const Project& project, const std::optional<std::filesystem::path>& directory) {
        auto display = load_settings(project, project.display_settings(), directory, "display");
        if(!display)
            return Result<Startup>::failure(display.error());
        auto quality = load_settings(project, project.quality_settings(), directory, "quality");
        if(!quality)
            return Result<Startup>::failure(quality.error());
        auto audio = load_settings(project, project.audio_settings(), directory, "audio");
        if(!audio)
            return Result<Startup>::failure(audio.error());
        return Result<Startup>::success(
            {std::move(display).value(), std::move(quality).value(), std::move(audio).value()});
    }

    PlayerSettingsOwner::PlayerSettingsOwner(
        const Project& project, std::optional<std::filesystem::path> directory)
        : m_project(project), m_directory(std::move(directory)) {}

    PlayerSettingsOwner::PlayerSettingsOwner(const Project& project, Startup startup)
        : m_project(project), m_directory(startup.display.path().parent_path()),
          m_display(std::move(startup.display)),
          m_display_preview(std::make_unique<DisplaySettingsPreview>(*m_display)),
          m_quality(std::move(startup.quality)), m_audio(std::move(startup.audio)) {}

    void PlayerSettingsOwner::bind_app(Engine& engine) {
        m_engine = &engine;
        m_set_play_resolution = {};
        m_play_active = {};
    }

    void PlayerSettingsOwner::bind_play(Engine& engine,
        std::function<void(Math::Vec2u)> set_resolution, std::function<bool()> play_active) {
        if(!set_resolution || !play_active)
            LOG_FATAL("Play settings require resolution and session callbacks");
        bind_app(engine);
        m_set_play_resolution = std::move(set_resolution);
        m_play_active = std::move(play_active);
    }

    Engine& PlayerSettingsOwner::engine() const {
        if(!m_engine)
            LOG_FATAL("Player settings require a bound host");
        return *m_engine;
    }

    Result<void> PlayerSettingsOwner::apply_display_target(const DisplaySettings& settings) {
        const Math::Vec2u size{
            static_cast<uint32_t>(settings.width), static_cast<uint32_t>(settings.height)};
        if(is_play()) {
            m_set_play_resolution(size);
            return Result<void>::success();
        }
        auto& host = engine();
        if(auto window = host.get_window().set_display_settings(settings.mode, size); !window)
            return window;
        auto& renderer = host.get_renderer();
        renderer.set_vsync_enabled(settings.vsync);
        if(auto output = renderer.request_output_settings(settings.output); !output)
            return Result<void>::failure(output.error().message);
        return ui_result(host.set_frame_rate_limit(settings.frame_rate_limit));
    }

    Ui::ProjectUi::Services PlayerSettingsOwner::ui_services() {
        Ui::ProjectUi::Services services{.input_actions = m_project.input_actions(),
            .load_input = [this] { return load_input(); },
            .apply_input =
                [this](InputOverrides overrides) { return apply_input(std::move(overrides)); },
            .display_defaults = m_project.display_settings(),
            .load_display = [this] { return load_display(); },
            .apply_display = [this](DisplaySettings settings) { return apply_display(settings); },
            .quality_defaults = m_project.quality_settings(),
            .load_quality = [this] { return load_quality(); },
            .apply_quality = [this](QualitySettings settings) { return apply_quality(settings); },
            .audio_defaults = m_project.audio_settings(),
            .load_audio = [this] { return load_audio(); },
            .active_audio = [this] { return engine().get_audio_settings(); },
            .apply_audio = [this](AudioSettings settings) { return apply_audio(settings); }};
        if(!is_play()) {
            services.display_confirmation = [this] {
                return m_display_preview ? m_display_preview->remaining() : std::optional<float>{};
            };
            services.confirm_display = [this] {
                if(engine().get_renderer().output_pending())
                    return Result<void>::failure("Display output is still being applied");
                if(!m_display_preview)
                    return Result<void>::failure("No display preview is pending");
                return m_display_preview->confirm();
            };
            services.revert_display = [this] { return revert_display(); };
        }
        return services;
    }

    Result<InputOverrides> PlayerSettingsOwner::load_input() {
        if(!m_input) {
            auto loaded =
                m_directory ? PlayerInputSettings::load(m_project.id(), *m_directory / "input.json")
                            : PlayerInputSettings::load(m_project.id());
            if(!loaded)
                return Result<InputOverrides>::failure(loaded.error());
            m_input = std::move(loaded).value();
        }
        return Result<InputOverrides>::success(m_input->overrides());
    }

    Result<void> PlayerSettingsOwner::ensure_display() {
        if(!m_display) {
            auto loaded =
                load_settings(m_project, m_project.display_settings(), m_directory, "display");
            if(!loaded)
                return Result<void>::failure(loaded.error());
            m_display = std::move(loaded).value();
        }
        if(!is_play() && !m_display_preview)
            m_display_preview = std::make_unique<DisplaySettingsPreview>(*m_display);
        return Result<void>::success();
    }

    DisplaySettings PlayerSettingsOwner::current_display() const {
        if(is_play())
            return m_display->settings();
        auto settings = m_display_preview->settings();
        const auto& window = engine().get_window();
        const auto size = window.get_restore_size();
        settings.width = static_cast<int>(size.x);
        settings.height = static_cast<int>(size.y);
        settings.mode = window.get_mode();
        return settings;
    }

    Result<DisplaySettings> PlayerSettingsOwner::load_display() {
        if(auto loaded = ensure_display(); !loaded)
            return Result<DisplaySettings>::failure(loaded.error());
        return Result<DisplaySettings>::success(current_display());
    }

    Result<QualitySettings> PlayerSettingsOwner::load_quality() {
        if(!m_quality) {
            auto loaded =
                load_settings(m_project, m_project.quality_settings(), m_directory, "quality");
            if(!loaded)
                return Result<QualitySettings>::failure(loaded.error());
            m_quality = std::move(loaded).value();
        }
        return Result<QualitySettings>::success(m_quality->settings());
    }

    Result<AudioSettings> PlayerSettingsOwner::load_audio() {
        if(!m_audio) {
            auto loaded =
                load_settings(m_project, m_project.audio_settings(), m_directory, "audio");
            if(!loaded)
                return Result<AudioSettings>::failure(loaded.error());
            m_audio = std::move(loaded).value();
        }
        return Result<AudioSettings>::success(m_audio->settings());
    }

    Result<void> PlayerSettingsOwner::apply_input(InputOverrides overrides) {
        if(!m_input || !engine().get_scene_runtime().is_active() || (is_play() && !m_play_active()))
            return Result<void>::failure("Player input settings require an active game session");
        return m_input->save_and_apply(
            m_project.input_actions(), std::move(overrides), [this](InputActions actions) {
                return engine().rebind_input_actions(std::move(actions));
            });
    }

    Result<void, Error> PlayerSettingsOwner::configure_input() {
        auto overrides = load_input();
        if(!overrides) {
            LOG_WARN(
                "Player input settings unavailable; using project defaults: {}", overrides.error());
            return engine().set_input_actions(m_project.input_actions());
        }
        auto resolved = overrides.value().resolve(m_project.input_actions());
        if(!resolved)
            return Result<void, Error>::failure({resolved.error()});
        for(const auto& issue : resolved.value().issues)
            LOG_WARN("Player input override ignored for action '{}', binding '{}': {}",
                issue.action.to_string(), issue.binding.to_string(), issue.message);
        return engine().set_input_actions(std::move(resolved).value().actions);
    }

    Result<void, Error> PlayerSettingsOwner::activate_quality() {
        const auto loaded = load_quality();
        if(!loaded)
            return Result<void, Error>::failure({loaded.error()});
        const auto applied = engine().get_renderer().request_quality_settings(loaded.value());
        if(!applied)
            return Result<void, Error>::failure(applied.error().as_error());
        return Result<void, Error>::success();
    }

    Result<void, Error> PlayerSettingsOwner::activate_audio() {
        const auto loaded = load_audio();
        if(!loaded)
            return Result<void, Error>::failure({loaded.error()});
        return engine().apply_audio_settings(loaded.value());
    }

    Result<void> PlayerSettingsOwner::apply_display(DisplaySettings settings) {
        if(auto loaded = ensure_display(); !loaded)
            return loaded;
        const auto apply = [this](const DisplaySettings& candidate) {
            return apply_display_target(candidate);
        };
        if(!is_play())
            return m_display_preview->apply(settings, current_display(), apply);
        const auto& current = m_display->settings();
        if(settings.mode != current.mode || settings.vsync != current.vsync
            || settings.output != current.output
            || settings.frame_rate_limit != current.frame_rate_limit)
            return Result<void>::failure(
                "Window mode, VSync, HDR and frame rate limit apply only in the standalone App");
        return m_display->save_and_apply(settings, apply);
    }

    Result<void> PlayerSettingsOwner::apply_quality(QualitySettings settings) {
        if(auto loaded = load_quality(); !loaded)
            return Result<void>::failure(loaded.error());
        return m_quality->save_and_apply(settings, [this](const QualitySettings& candidate) {
            const auto applied = engine().get_renderer().request_quality_settings(candidate);
            if(!applied)
                return Result<void>::failure(applied.error().message);
            return Result<void>::success();
        });
    }

    Result<void> PlayerSettingsOwner::apply_audio(AudioSettings settings) {
        if(auto loaded = load_audio(); !loaded)
            return Result<void>::failure(loaded.error());
        return m_audio->save_and_apply(settings, [this](const AudioSettings& candidate) {
            return ui_result(engine().apply_audio_settings(candidate));
        });
    }

    Result<void> PlayerSettingsOwner::expire_display() {
        if(!m_display_preview)
            return Result<void>::success();
        return m_display_preview->expire(
            [this](const DisplaySettings& settings) { return apply_display_target(settings); });
    }

    Result<void> PlayerSettingsOwner::revert_display() {
        if(!m_display_preview)
            return Result<void>::success();
        return m_display_preview->revert(
            [this](const DisplaySettings& settings) { return apply_display_target(settings); });
    }

    Result<void> PlayerSettingsOwner::save_window_state() {
        if(is_play() || !m_display_preview || m_display_preview->is_pending())
            return Result<void>::success();
        const auto settings = current_display();
        return settings != m_display->settings() ? m_display->save(settings)
                                                 : Result<void>::success();
    }

    void PlayerSettingsOwner::reset_display() {
        m_display_preview.reset();
        m_display.reset();
    }
}
