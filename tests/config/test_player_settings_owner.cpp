#include "config/player_settings_owner.h"
#include "common/file_io.h"
#include "core/window.h"
#include "scene/scene.h"
#include "support/engine_fixture.h"
#include "support/temporary_directory.h"

namespace Comet::Tests {
    class PlayerSettingsOwnerTest: public EngineTest {
    protected:
        TemporaryDirectory directory;
        std::optional<Project> project;
        std::unique_ptr<PlayerSettingsOwner> owner;

        std::filesystem::path settings_directory() const { return directory.path() / "player"; }
        std::filesystem::path path(std::string_view name) const {
            return settings_directory() / (std::string(name) + ".json");
        }
        void SetUp() override {
            EngineTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            std::filesystem::create_directory(directory.path() / "assets");
            ASSERT_TRUE(write_text_file_atomic(directory.path() / "project.json",
                R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"main.scene"})"));
            auto loaded = Project::load(directory.path());
            ASSERT_TRUE(loaded) << loaded.error();
            project = std::move(loaded).value();
            owner = std::make_unique<PlayerSettingsOwner>(*project, settings_directory());
            owner->bind_app(*engine);
        }
    };

    TEST_F(PlayerSettingsOwnerTest, AppSavesWindowResizesWithoutOpeningSettings) {
        auto startup = PlayerSettingsOwner::load_startup(*project, settings_directory());
        ASSERT_TRUE(startup);
        owner = std::make_unique<PlayerSettingsOwner>(*project, std::move(startup).value());
        owner->bind_app(*engine);
        ASSERT_TRUE(engine->get_window().set_display_settings(WindowMode::Windowed, {320, 240}));
        ASSERT_TRUE(owner->save_window_state());
        const auto saved = PlayerDisplaySettings::load(project->id(), {}, path("display"));
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().settings().width, 320);
        EXPECT_EQ(saved.value().settings().height, 240);
    }

    TEST_F(PlayerSettingsOwnerTest, AppPreviewUsesActualWindowAndWaitsForOutputBeforeConfirming) {
        auto services = owner->ui_services();
        auto current = services.load_display();
        ASSERT_TRUE(current);
        const auto size = engine->get_window().get_restore_size();
        EXPECT_EQ(current.value().width, size.x);
        EXPECT_EQ(current.value().height, size.y);
        auto candidate = current.value();
        candidate.width += 80;
        candidate.height += 60;
        candidate.output.hdr_white_level = 1.25f;
        candidate.frame_rate_limit = 120;
        ASSERT_TRUE(services.apply_display(candidate));
        EXPECT_EQ(engine->frame_rate_limit(), 120);
        EXPECT_TRUE(services.display_confirmation().has_value());
        EXPECT_TRUE(engine->get_renderer().output_pending());
        EXPECT_FALSE(services.confirm_display());
        ASSERT_TRUE(owner->save_window_state());
        EXPECT_FALSE(std::filesystem::exists(path("display")));
        const auto prepared = render_frontend->prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error().message;
        ASSERT_TRUE(render_frontend->render({}));
        ASSERT_FALSE(engine->get_renderer().output_pending());
        ASSERT_TRUE(services.confirm_display());
        EXPECT_EQ(owner->load_display().value(), candidate);
        ASSERT_TRUE(services.apply_display(current.value()));
        ASSERT_TRUE(services.revert_display());
        EXPECT_EQ(owner->load_display().value(), candidate);
        const auto saved = PlayerDisplaySettings::load(project->id(), {}, path("display"));
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().settings(), candidate);
        ASSERT_TRUE(engine->get_window().set_display_settings(WindowMode::Windowed, {320, 240}));
        ASSERT_TRUE(owner->save_window_state());
        const auto resized = PlayerDisplaySettings::load(project->id(), {}, path("display"));
        ASSERT_TRUE(resized);
        EXPECT_EQ(resized.value().settings().width, 320);
        EXPECT_EQ(resized.value().settings().height, 240);
    }

    TEST_F(PlayerSettingsOwnerTest, PlayOnlyChangesOffscreenResolutionAndRequiresAnActiveSession) {
        Math::Vec2u resolution{};
        bool playing = false;
        owner->bind_play(
            *engine, [&](Math::Vec2u size) { resolution = size; }, [&] { return playing; });
        const auto window_size = engine->get_window().get_restore_size();
        const auto frame_limit = engine->frame_rate_limit();
        auto services = owner->ui_services();
        EXPECT_FALSE(services.confirm_display);
        EXPECT_FALSE(services.display_confirmation);
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
        EXPECT_EQ(resolution, Math::Vec2u{});
        EXPECT_FALSE(std::filesystem::exists(path("display")));
        auto candidate = project->display_settings();
        candidate.width = 960;
        candidate.height = 540;
        ASSERT_TRUE(services.apply_display(candidate));
        EXPECT_EQ(resolution, Math::Vec2u(960, 540));
        EXPECT_EQ(engine->get_window().get_restore_size(), window_size);
        EXPECT_EQ(engine->frame_rate_limit(), frame_limit);
        EXPECT_EQ(services.load_display().value(), candidate);

        const auto action_id = Uuid::generate(), binding_id = Uuid::generate();
        InputActions::Binding binding{Input::Key::Space};
        binding.id = binding_id;
        InputActions::Action action{"jump", InputActions::Type::Button, {binding}};
        action.id = action_id;
        auto defaults = InputActions::create({action});
        ASSERT_TRUE(defaults);
        ASSERT_TRUE(project->save_input_actions(std::move(defaults).value()));
        ASSERT_TRUE(write_text_file_atomic(path("input"), "{broken"));
        ASSERT_TRUE(owner->configure_input());
        EXPECT_EQ(read_text_file(path("input")).value(), "{broken");
        std::filesystem::remove(path("input"));
        owner->reset_input();
        ASSERT_TRUE(owner->configure_input());
        const auto overrides = InputOverrides::create({{action_id, InputActions::Type::Button,
            false, {{.id = binding_id, .control = Input::Key::J}}}});
        ASSERT_TRUE(overrides);
        playing = true;
        EXPECT_FALSE(services.apply_input(overrides.value()));
        engine->set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine->start_scene_runtime());
        playing = false;
        EXPECT_FALSE(services.apply_input(overrides.value()));
        EXPECT_FALSE(std::filesystem::exists(path("input")));
        playing = true;
        ASSERT_TRUE(services.apply_input(overrides.value()));
        const auto saved = PlayerInputSettings::load(project->id(), path("input"));
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().overrides(), overrides.value());
    }

    TEST_F(PlayerSettingsOwnerTest, SavedChoicesSurviveEngineApplicationFailure) {
        auto services = owner->ui_services();
        const AudioSettings audio{0.5f, 0.4f, 0.3f};
        ASSERT_TRUE(services.apply_audio(audio));
        EXPECT_EQ(services.active_audio(), audio);
        std::filesystem::create_directory(path("quality"));
        auto quality = engine->get_renderer().get_quality_settings();
        quality.render_scale = 0.75f;
        EXPECT_FALSE(services.apply_quality(quality));
        EXPECT_FALSE(engine->get_renderer().quality_pending());
        std::filesystem::remove(path("quality"));
        engine->prepare_shutdown();
        const auto failed = services.apply_quality(quality);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().find("saved but not applied"), std::string::npos);
        auto startup = PlayerSettingsOwner::load_startup(*project, settings_directory());
        ASSERT_TRUE(startup);
        EXPECT_EQ(startup.value().quality.settings(), quality);
        EXPECT_EQ(startup.value().audio.settings(), audio);
        PlayerSettingsOwner restored(*project, std::move(startup).value());
        restored.bind_app(*engine);
        const auto activated = restored.activate_quality();
        ASSERT_FALSE(activated);
        EXPECT_NE(activated.error().message.find("shutting down"), std::string::npos);
    }

    TEST_F(PlayerSettingsOwnerTest, CacheInvalidationReloadsCurrentProjectDefaults) {
        owner->bind_play(*engine, [](Math::Vec2u) {}, [] { return false; });
        ASSERT_TRUE(owner->load_display());
        ASSERT_TRUE(owner->load_quality());
        ASSERT_TRUE(owner->load_audio());
        const DisplaySettings display{800, 600};
        const QualitySettings quality{1, 2, 0.75f};
        const AudioSettings audio{0.5f, 0.4f, 0.3f};
        ASSERT_TRUE(project->save_display_settings(display));
        ASSERT_TRUE(project->save_quality_settings(quality));
        ASSERT_TRUE(project->save_audio_settings(audio));
        owner->reset_display();
        owner->reset_quality();
        owner->reset_audio();
        const auto services = owner->ui_services();
        EXPECT_EQ(services.load_display().value(), display);
        EXPECT_EQ(services.load_quality().value(), quality);
        EXPECT_EQ(services.load_audio().value(), audio);
        EXPECT_EQ(services.display_defaults, display);
        EXPECT_EQ(services.quality_defaults, quality);
        EXPECT_EQ(services.audio_defaults, audio);
    }
}
