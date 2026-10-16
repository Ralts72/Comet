#include "config/player_settings_owner.h"

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
        bind({.host = Host::App,
            .input_active = [&engine] { return engine.get_scene_runtime().is_active(); },
            .configure_input =
                [&engine](
                    InputActions actions) { return engine.set_input_actions(std::move(actions)); },
            .rebind_input =
                [&engine](InputActions actions) {
                    return engine.rebind_input_actions(std::move(actions));
                },
            .current_display =
                [&engine](DisplaySettings settings) {
                    const auto& window = engine.get_window();
                    const auto size = window.get_restore_size();
                    settings.width = static_cast<int>(size.x);
                    settings.height = static_cast<int>(size.y);
                    settings.mode = window.get_mode();
                    return settings;
                },
            .apply_display =
                [&engine](const DisplaySettings& settings) {
                    if(auto window = engine.get_window().set_display_settings(
                           settings.mode, {static_cast<uint32_t>(settings.width),
                                              static_cast<uint32_t>(settings.height)});
                        !window)
                        return window;
                    auto& renderer = engine.get_renderer();
                    renderer.set_vsync_enabled(settings.vsync);
                    if(auto output = renderer.request_output_settings(settings.output); !output)
                        return Result<void>::failure(output.error().message);
                    return ui_result(engine.set_frame_rate_limit(settings.frame_rate_limit));
                },
            .output_pending = [&engine] { return engine.get_renderer().output_pending(); },
            .apply_quality =
                [&engine](const QualitySettings& settings) {
                    const auto applied = engine.get_renderer().request_quality_settings(settings);
                    if(!applied)
                        return Result<void, Error>::failure(applied.error().as_error());
                    return Result<void, Error>::success();
                },
            .apply_audio =
                [&engine](const AudioSettings& settings) {
                    return engine.apply_audio_settings(settings);
                },
            .active_audio = [&engine] { return engine.get_audio_settings(); }});
    }

    void PlayerSettingsOwner::bind_play(Engine& engine,
        std::function<void(Math::Vec2u)> set_resolution, std::function<bool()> play_active) {
        bind_app(engine);
        m_targets.host = Host::Play;
        m_targets.input_active = [&engine, play_active = std::move(play_active)] {
            return play_active && play_active() && engine.get_scene_runtime().is_active();
        };
        m_targets.current_display = {};
        m_targets.apply_display = [set_resolution = std::move(set_resolution)](
                                      const DisplaySettings& settings) {
            if(!set_resolution)
                return Result<void>::failure("Play resolution service is unavailable");
            set_resolution(
                {static_cast<uint32_t>(settings.width), static_cast<uint32_t>(settings.height)});
            return Result<void>::success();
        };
        m_targets.output_pending = {};
    }

    void PlayerSettingsOwner::bind(Targets targets) {
        m_targets = std::move(targets);
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
            .active_audio =
                [this] {
                    return m_targets.active_audio ? m_targets.active_audio() : AudioSettings{};
                },
            .apply_audio = [this](AudioSettings settings) { return apply_audio(settings); }};
        if(m_targets.host == Host::App) {
            services.display_confirmation = [this] {
                return m_display_preview ? m_display_preview->remaining() : std::optional<float>{};
            };
            services.confirm_display = [this] {
                if(m_targets.output_pending && m_targets.output_pending())
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
        if(!m_display_preview)
            m_display_preview = std::make_unique<DisplaySettingsPreview>(*m_display);
        return Result<void>::success();
    }

    DisplaySettings PlayerSettingsOwner::current_display() const {
        auto settings = m_display_preview->settings();
        return m_targets.host == Host::App && m_targets.current_display
                   ? m_targets.current_display(settings)
                   : settings;
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
        if(!m_input || !m_targets.input_active || !m_targets.input_active())
            return Result<void>::failure(
                m_targets.host == Host::App
                    ? "Player input settings require an active game"
                    : "Player input settings require an active Play session");
        return m_input->save_and_apply(
            m_project.input_actions(), std::move(overrides), m_targets.rebind_input);
    }

    Result<void, Error> PlayerSettingsOwner::configure_input() {
        if(!m_targets.configure_input)
            return Result<void, Error>::failure({"Player input service is unavailable"});
        auto overrides = load_input();
        if(!overrides) {
            LOG_WARN(
                "Player input settings unavailable; using project defaults: {}", overrides.error());
            return m_targets.configure_input(m_project.input_actions());
        }
        auto resolved = overrides.value().resolve(m_project.input_actions());
        if(!resolved)
            return Result<void, Error>::failure({resolved.error()});
        for(const auto& issue : resolved.value().issues)
            LOG_WARN("Player input override ignored for action '{}', binding '{}': {}",
                issue.action.to_string(), issue.binding.to_string(), issue.message);
        return m_targets.configure_input(std::move(resolved).value().actions);
    }

    Result<void, Error> PlayerSettingsOwner::activate_quality() {
        const auto loaded = load_quality();
        if(!loaded)
            return Result<void, Error>::failure({loaded.error()});
        if(!m_targets.apply_quality)
            return Result<void, Error>::failure({"Quality apply service is unavailable"});
        return m_targets.apply_quality(loaded.value());
    }

    Result<void, Error> PlayerSettingsOwner::activate_audio() {
        const auto loaded = load_audio();
        if(!loaded)
            return Result<void, Error>::failure({loaded.error()});
        if(!m_targets.apply_audio)
            return Result<void, Error>::failure({"Audio apply service is unavailable"});
        return m_targets.apply_audio(loaded.value());
    }

    Result<void> PlayerSettingsOwner::apply_display(DisplaySettings settings) {
        if(auto loaded = ensure_display(); !loaded)
            return loaded;
        if(m_targets.host == Host::App)
            return m_display_preview->apply(settings, current_display(), m_targets.apply_display);
        const auto& current = m_display->settings();
        if(settings.mode != current.mode || settings.vsync != current.vsync
            || settings.output != current.output
            || settings.frame_rate_limit != current.frame_rate_limit)
            return Result<void>::failure(
                "Window mode, VSync, HDR and frame rate limit apply only in the standalone App");
        return m_display->save_and_apply(settings, m_targets.apply_display);
    }

    Result<void> PlayerSettingsOwner::apply_quality(QualitySettings settings) {
        if(auto loaded = load_quality(); !loaded)
            return Result<void>::failure(loaded.error());
        if(!m_targets.apply_quality)
            return Result<void>::failure("Quality apply service is unavailable");
        return m_quality->save_and_apply(settings, [this](const QualitySettings& candidate) {
            return ui_result(m_targets.apply_quality(candidate));
        });
    }

    Result<void> PlayerSettingsOwner::apply_audio(AudioSettings settings) {
        if(auto loaded = load_audio(); !loaded)
            return Result<void>::failure(loaded.error());
        if(!m_targets.apply_audio)
            return Result<void>::failure("Audio apply service is unavailable");
        return m_audio->save_and_apply(settings, [this](const AudioSettings& candidate) {
            return ui_result(m_targets.apply_audio(candidate));
        });
    }

    Result<void> PlayerSettingsOwner::expire_display() {
        return m_display_preview ? m_display_preview->expire(m_targets.apply_display)
                                 : Result<void>::success();
    }

    Result<void> PlayerSettingsOwner::revert_display() {
        return m_display_preview ? m_display_preview->revert(m_targets.apply_display)
                                 : Result<void>::success();
    }

    Result<void> PlayerSettingsOwner::save_window_state() {
        if(m_targets.host != Host::App || !m_display_preview || m_display_preview->is_pending())
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
