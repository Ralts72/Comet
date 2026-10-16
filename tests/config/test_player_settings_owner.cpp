#include "config/player_settings_owner.h"
#include "common/file_io.h"
#include "input/input_state.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class PlayerSettingsOwnerTest: public testing::Test {
    protected:
        TemporaryDirectory directory;
        std::optional<Project> project;
        std::unique_ptr<PlayerSettingsOwner> owner;
        DisplaySettings display;
        QualitySettings quality;
        AudioSettings audio;
        InputActions actions;
        bool active = false;
        bool output_pending = false;
        bool fail_apply = false;
        unsigned display_applications = 0;
        unsigned quality_applications = 0;
        unsigned audio_applications = 0;
        unsigned input_applications = 0;
        const Uuid action_id = Uuid::generate();
        const Uuid binding_id = Uuid::generate();

        std::filesystem::path settings_directory() const { return directory.path() / "player"; }
        std::filesystem::path path(std::string_view name) const {
            return settings_directory() / (std::string(name) + ".json");
        }
        void SetUp() override {
            std::filesystem::create_directory(directory.path() / "assets");
            ASSERT_TRUE(write_text_file_atomic(directory.path() / "project.json",
                R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"main.scene"})"));
            auto loaded = Project::load(directory.path());
            ASSERT_TRUE(loaded) << loaded.error();
            project = std::move(loaded).value();
            InputActions::Binding binding{Input::Key::Space};
            binding.id = binding_id;
            InputActions::Action action{"jump", InputActions::Type::Button, {binding}};
            action.id = action_id;
            auto defaults = InputActions::create({action});
            ASSERT_TRUE(defaults) << defaults.error();
            ASSERT_TRUE(project->save_input_actions(std::move(defaults).value()));
            owner = std::make_unique<PlayerSettingsOwner>(*project, settings_directory());
            bind(PlayerSettingsOwner::Host::Play);
        }
        void bind(PlayerSettingsOwner::Host host) {
            owner->bind({.host = host,
                .input_active = [this] { return active; },
                .configure_input =
                    [this](InputActions candidate) {
                        actions = std::move(candidate);
                        ++input_applications;
                        return Result<void, Error>::success();
                    },
                .rebind_input =
                    [this](InputActions candidate) {
                        ++input_applications;
                        if(fail_apply)
                            return Result<void, Error>::failure({"Runtime rejected input"});
                        actions = std::move(candidate);
                        return Result<void, Error>::success();
                    },
                .current_display =
                    [this, host](DisplaySettings candidate) {
                        if(host == PlayerSettingsOwner::Host::App) {
                            candidate.width = display.width;
                            candidate.height = display.height;
                            candidate.mode = display.mode;
                        }
                        return candidate;
                    },
                .apply_display =
                    [this](const DisplaySettings& candidate) {
                        display = candidate;
                        ++display_applications;
                        return Result<void>::success();
                    },
                .output_pending = [this] { return output_pending; },
                .apply_quality =
                    [this](const QualitySettings& candidate) {
                        ++quality_applications;
                        if(fail_apply)
                            return Result<void, Error>::failure({"Renderer unavailable",
                                std::make_error_code(std::errc::io_error)});
                        quality = candidate;
                        return Result<void, Error>::success();
                    },
                .apply_audio =
                    [this](const AudioSettings& candidate) {
                        ++audio_applications;
                        if(fail_apply)
                            return Result<void, Error>::failure({"Audio unavailable"});
                        audio = candidate;
                        return Result<void, Error>::success();
                    },
                .active_audio = [this] { return audio; }});
        }
        InputOverrides remapping() const {
            return InputOverrides::create({{action_id, InputActions::Type::Button, false,
                                              {{.id = binding_id, .control = Input::Key::J}}}})
                .value();
        }
    };

    TEST_F(PlayerSettingsOwnerTest, AppReadsTheActualWindowAndConfirmsOnlyAfterOutputSettles) {
        bind(PlayerSettingsOwner::Host::App);
        display.width = 1377;
        display.height = 811;
        auto services = owner->ui_services();
        ASSERT_TRUE(services.load_display());
        EXPECT_EQ(services.load_display().value(), display);
        const DisplaySettings candidate{
            1920, 1080, WindowMode::Fullscreen, false, {OutputMode::Hdr, 8, 1.25f}, 120};
        ASSERT_TRUE(services.apply_display(candidate));
        EXPECT_EQ(display, candidate);
        EXPECT_TRUE(services.display_confirmation().has_value());
        EXPECT_FALSE(std::filesystem::exists(path("display")));
        ASSERT_TRUE(owner->save_window_state());
        EXPECT_FALSE(std::filesystem::exists(path("display")));
        output_pending = true;
        EXPECT_FALSE(services.confirm_display());
        EXPECT_TRUE(services.display_confirmation().has_value());
        output_pending = false;
        ASSERT_TRUE(services.confirm_display());
        EXPECT_FALSE(services.display_confirmation().has_value());
        EXPECT_EQ(display_applications, 1u);
        auto saved = PlayerDisplaySettings::load(project->id(), {}, path("display"));
        ASSERT_TRUE(saved) << saved.error();
        EXPECT_EQ(saved.value().settings(), candidate);
    }

    TEST_F(PlayerSettingsOwnerTest, AppRevertsToActualWindowAndSavesResizesOutsideTrials) {
        bind(PlayerSettingsOwner::Host::App);
        const DisplaySettings actual{1377, 811, WindowMode::Windowed, false};
        display = actual;
        auto services = owner->ui_services();
        ASSERT_TRUE(services.load_display());
        auto candidate = actual;
        candidate.mode = WindowMode::Borderless;
        ASSERT_TRUE(services.apply_display(candidate));
        ASSERT_TRUE(owner->revert_display());
        EXPECT_EQ(display, actual);
        EXPECT_FALSE(std::filesystem::exists(path("display")));
        ASSERT_TRUE(owner->save_window_state());
        auto saved = PlayerDisplaySettings::load(project->id(), {}, path("display"));
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().settings(), actual);
    }

    TEST_F(PlayerSettingsOwnerTest, PlayOnlyPersistsAndAppliesOffscreenResolution) {
        auto services = owner->ui_services();
        ASSERT_TRUE(services.load_display());
        EXPECT_FALSE(services.display_confirmation);
        EXPECT_FALSE(services.confirm_display);
        EXPECT_FALSE(services.revert_display);
        for(unsigned field = 0; field < 4; ++field) {
            auto rejected = project->display_settings();
            if(field == 0)
                rejected.mode = WindowMode::Fullscreen;
            else if(field == 1)
                rejected.vsync = !rejected.vsync;
            else if(field == 2)
                rejected.output.mode = OutputMode::Hdr;
            else
                rejected.frame_rate_limit = 30;
            EXPECT_FALSE(services.apply_display(rejected));
        }
        EXPECT_EQ(display_applications, 0u);
        EXPECT_FALSE(std::filesystem::exists(path("display")));
        auto candidate = project->display_settings();
        candidate.width = 960;
        candidate.height = 540;
        ASSERT_TRUE(services.apply_display(candidate));
        EXPECT_EQ(display_applications, 1u);
        EXPECT_EQ(display, candidate);
        ASSERT_TRUE(owner->save_window_state());
        auto saved = PlayerDisplaySettings::load(project->id(), {}, path("display"));
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().settings(), candidate);
    }

    TEST_F(
        PlayerSettingsOwnerTest, InputRequiresActivePlayAndKeepsSparseOverridesAfterApplyFailure) {
        auto services = owner->ui_services();
        ASSERT_TRUE(services.load_input());
        const auto candidate = remapping();
        EXPECT_FALSE(services.apply_input(candidate));
        EXPECT_EQ(input_applications, 0u);
        EXPECT_FALSE(std::filesystem::exists(path("input")));
        active = true;
        fail_apply = true;
        const auto failed = services.apply_input(candidate);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().find("saved but not applied"), std::string::npos);
        const auto saved = PlayerInputSettings::load(project->id(), path("input"));
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().overrides(), candidate);
        EXPECT_FALSE(saved.value().overrides().actions()[0].bindings[0].scale.has_value());
        fail_apply = false;
        ASSERT_TRUE(services.apply_input(candidate));
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::J, true);
        InputState state;
        actions.evaluate(input.publish_frame(), state);
        ASSERT_NE(state.action("jump"), nullptr);
        EXPECT_TRUE(state.action("jump")->pressed);
        active = false;
        EXPECT_FALSE(services.apply_input(InputOverrides{}));
        EXPECT_EQ(input_applications, 2u);
    }

    TEST_F(PlayerSettingsOwnerTest,
        InputReloadUsesDefaultsAfterMalformedSettingsWithoutRewritingThem) {
        ASSERT_TRUE(write_text_file_atomic(path("input"), "{broken"));
        ASSERT_TRUE(owner->configure_input());
        EXPECT_EQ(actions, project->input_actions());
        EXPECT_EQ(read_text_file(path("input")).value(), "{broken");
        std::filesystem::remove(path("input"));
        auto stored = PlayerInputSettings::load(project->id(), path("input"));
        ASSERT_TRUE(stored);
        ASSERT_TRUE(stored.value().save(remapping()));
        owner->reset_input();
        ASSERT_TRUE(owner->configure_input());
        EXPECT_EQ(owner->load_input().value(), remapping());
        EXPECT_NE(actions, project->input_actions());
    }

    TEST_F(
        PlayerSettingsOwnerTest, QualityAndAudioKeepSavedChoicesWhenApplicationFailsAndCanRetry) {
        auto services = owner->ui_services();
        const QualitySettings quality_choice{1, 2, 0.75f};
        const AudioSettings audio_choice{0.5f, 0.4f, 0.3f};
        fail_apply = true;
        const auto quality_failed = services.apply_quality(quality_choice);
        const auto audio_failed = services.apply_audio(audio_choice);
        ASSERT_FALSE(quality_failed);
        ASSERT_FALSE(audio_failed);
        EXPECT_NE(quality_failed.error().find("saved but not applied"), std::string::npos);
        EXPECT_NE(audio_failed.error().find("saved but not applied"), std::string::npos);
        EXPECT_EQ(services.load_quality().value(), quality_choice);
        EXPECT_EQ(services.load_audio().value(), audio_choice);
        EXPECT_EQ(services.active_audio(), AudioSettings{});
        auto saved_quality = PlayerQualitySettings::load(project->id(), {}, path("quality"));
        auto saved_audio = PlayerAudioSettings::load(project->id(), {}, path("audio"));
        ASSERT_TRUE(saved_quality);
        ASSERT_TRUE(saved_audio);
        EXPECT_EQ(saved_quality.value().settings(), quality_choice);
        EXPECT_EQ(saved_audio.value().settings(), audio_choice);
        fail_apply = false;
        ASSERT_TRUE(services.apply_quality(quality_choice));
        ASSERT_TRUE(services.apply_audio(audio_choice));
        EXPECT_EQ(quality, quality_choice);
        EXPECT_EQ(services.active_audio(), audio_choice);
        EXPECT_FALSE(services.apply_quality({3, 2, 0.75f}));
        EXPECT_FALSE(services.apply_audio({2, 1, 1}));
        EXPECT_EQ(quality_applications, 2u);
        EXPECT_EQ(audio_applications, 2u);
    }

    TEST_F(
        PlayerSettingsOwnerTest, PersistenceFailurePreventsRuntimeChangesAndRetainsCachedChoices) {
        auto services = owner->ui_services();
        ASSERT_TRUE(services.load_quality());
        ASSERT_TRUE(services.load_audio());
        std::filesystem::create_directories(path("quality"));
        std::filesystem::create_directory(path("audio"));
        EXPECT_FALSE(services.apply_quality({1, 2, 0.75f}));
        EXPECT_FALSE(services.apply_audio({0.5f, 0.4f, 0.3f}));
        EXPECT_EQ(quality_applications, 0u);
        EXPECT_EQ(audio_applications, 0u);
        EXPECT_EQ(services.load_quality().value(), project->quality_settings());
        EXPECT_EQ(services.load_audio().value(), project->audio_settings());
    }

    TEST_F(PlayerSettingsOwnerTest,
        StartupLoadsPersistedChoicesAndActivationPreservesRuntimeErrorCode) {
        auto services = owner->ui_services();
        const QualitySettings quality_choice{1, 2, 0.75f};
        const AudioSettings audio_choice{0.5f, 0.4f, 0.3f};
        ASSERT_TRUE(services.apply_quality(quality_choice));
        ASSERT_TRUE(services.apply_audio(audio_choice));
        auto startup = PlayerSettingsOwner::load_startup(*project, settings_directory());
        ASSERT_TRUE(startup) << startup.error();
        EXPECT_EQ(startup.value().display.settings(), project->display_settings());
        EXPECT_EQ(startup.value().quality.settings(), quality_choice);
        EXPECT_EQ(startup.value().audio.settings(), audio_choice);
        owner = std::make_unique<PlayerSettingsOwner>(*project, std::move(startup).value());
        bind(PlayerSettingsOwner::Host::App);
        fail_apply = true;
        const auto failed = owner->activate_quality();
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().code, std::make_error_code(std::errc::io_error));
        ASSERT_TRUE(owner->load_input());
        EXPECT_FALSE(std::filesystem::exists(path("input")));
    }

    TEST_F(PlayerSettingsOwnerTest, CacheInvalidationReloadsCurrentProjectDefaults) {
        ASSERT_TRUE(owner->load_display());
        ASSERT_TRUE(owner->load_quality());
        ASSERT_TRUE(owner->load_audio());
        const DisplaySettings display_defaults{800, 600};
        const QualitySettings quality_defaults{1, 2, 0.75f};
        const AudioSettings audio_defaults{0.5f, 0.4f, 0.3f};
        ASSERT_TRUE(project->save_display_settings(display_defaults));
        ASSERT_TRUE(project->save_quality_settings(quality_defaults));
        ASSERT_TRUE(project->save_audio_settings(audio_defaults));
        owner->reset_display();
        owner->reset_quality();
        owner->reset_audio();
        auto services = owner->ui_services();
        EXPECT_EQ(services.load_display().value(), display_defaults);
        EXPECT_EQ(services.load_quality().value(), quality_defaults);
        EXPECT_EQ(services.load_audio().value(), audio_defaults);
        EXPECT_EQ(services.display_defaults, display_defaults);
        EXPECT_EQ(services.quality_defaults, quality_defaults);
        EXPECT_EQ(services.audio_defaults, audio_defaults);
    }
}
