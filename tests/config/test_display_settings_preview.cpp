#include "config/display_settings_preview.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <memory>

namespace Comet::Tests {
    class DisplaySettingsPreviewTest: public testing::Test {
    protected:
        using Clock = DisplaySettingsPreview::Clock;
        TemporaryDirectory directory;
        Uuid project_id = Uuid::generate();
        DisplaySettings defaults{1280, 720, WindowMode::Windowed, true};
        DisplaySettings active = defaults;
        std::optional<PlayerDisplaySettings> player;
        std::unique_ptr<DisplaySettingsPreview> preview;
        Clock::time_point now{};
        unsigned applications = 0;
        std::filesystem::path path() const { return directory.path() / "display.json"; }

        void SetUp() override {
            auto loaded = PlayerDisplaySettings::load(project_id, defaults, path());
            ASSERT_TRUE(loaded) << loaded.error();
            player.emplace(std::move(loaded).value());
            preview = std::make_unique<DisplaySettingsPreview>(*player);
        }
        Result<void> apply(const DisplaySettings& settings) {
            active = settings;
            ++applications;
            return Result<void>::success();
        }
        DisplaySettingsPreview::Apply application() {
            return [this](const auto& settings) { return apply(settings); };
        }
    };

    TEST_F(DisplaySettingsPreviewTest, SavesOnlyAfterConfirmationWithoutApplyingAgain) {
        ASSERT_TRUE(player->save(defaults));
        const DisplaySettings candidate{
            1920, 1080, WindowMode::Borderless, false, {OutputMode::Hdr, 8, 1.25f}};
        ASSERT_TRUE(preview->apply(candidate, active, application(), now));
        EXPECT_TRUE(preview->is_pending());
        EXPECT_EQ(preview->remaining(now), 15);
        EXPECT_EQ(preview->settings(), candidate);
        EXPECT_EQ(active, candidate);
        EXPECT_EQ(player->settings(), defaults);
        auto saved = PlayerDisplaySettings::load(project_id, defaults, path());
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().settings(), defaults);

        ASSERT_TRUE(preview->confirm(now + std::chrono::seconds(14)));
        EXPECT_FALSE(preview->remaining(now));
        EXPECT_EQ(applications, 1u);
        saved = PlayerDisplaySettings::load(project_id, defaults, path());
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().settings(), candidate);
        ASSERT_TRUE(preview->expire(application(), now + std::chrono::seconds(30)));
        EXPECT_EQ(active, candidate);
        EXPECT_EQ(applications, 1u);
    }

    TEST_F(DisplaySettingsPreviewTest, ExpiryAndManualRevertRestoreActualStateWithoutSaving) {
        const DisplaySettings previous{
            1377, 811, WindowMode::Windowed, true, {OutputMode::Auto, 6, 0.75f}, 30};
        active = previous;
        const DisplaySettings candidate{
            1920, 1080, WindowMode::Fullscreen, false, {OutputMode::Hdr, 12, 1.5f}, 120};
        ASSERT_TRUE(preview->apply(candidate, active, application(), now));
        ASSERT_TRUE(preview->expire(application(), now + std::chrono::seconds(14)));
        EXPECT_EQ(active, candidate);
        EXPECT_FALSE(preview->confirm(now + std::chrono::seconds(15)));
        EXPECT_EQ(preview->remaining(now + std::chrono::seconds(15)), 0);
        ASSERT_TRUE(preview->expire(application(), now + std::chrono::seconds(15)));
        EXPECT_EQ(active, previous);
        EXPECT_FALSE(preview->is_pending());
        EXPECT_FALSE(std::filesystem::exists(path()));
        ASSERT_TRUE(preview->apply(candidate, active, application(), now));
        ASSERT_TRUE(preview->revert(application()));
        EXPECT_EQ(active, previous);
        EXPECT_EQ(player->settings(), defaults);
        EXPECT_FALSE(std::filesystem::exists(path()));
    }

    TEST_F(DisplaySettingsPreviewTest, FailedSaveOrRevertKeepsTheOriginalDeadlineAndCanRetry) {
        const DisplaySettings candidate{1920, 1080, WindowMode::Windowed, false};
        ASSERT_TRUE(preview->apply(candidate, active, application(), now));
        std::filesystem::create_directory(path());
        EXPECT_FALSE(preview->confirm(now + std::chrono::seconds(10)));
        EXPECT_EQ(preview->remaining(now + std::chrono::seconds(10)), 5);
        EXPECT_EQ(player->settings(), defaults);
        EXPECT_EQ(active, candidate);
        EXPECT_FALSE(preview->apply(defaults, active, application(), now));
        EXPECT_EQ(applications, 1u);
        std::filesystem::remove(path());
        ASSERT_TRUE(preview->confirm(now + std::chrono::seconds(11)));
        EXPECT_EQ(player->settings(), candidate);

        ASSERT_TRUE(preview->apply(defaults, active, application(), now));
        EXPECT_FALSE(preview->expire(
            [](const auto&) { return Result<void>::failure("Display is unavailable"); },
            now + std::chrono::seconds(15)));
        EXPECT_TRUE(preview->is_pending());
        EXPECT_EQ(active, defaults);
        ASSERT_TRUE(preview->expire(application(), now + std::chrono::seconds(16)));
        EXPECT_EQ(active, candidate);
    }

    TEST_F(DisplaySettingsPreviewTest, TimingAndCalibrationSaveDirectlyAndSaveFailureDoesNotApply) {
        auto candidate = defaults;
        candidate.vsync = false;
        candidate.frame_rate_limit = 75;
        candidate.output.hdr_headroom = 8;
        candidate.output.hdr_white_level = 1.5f;
        ASSERT_TRUE(preview->apply(candidate, active, application(), now));
        EXPECT_FALSE(preview->is_pending());
        EXPECT_EQ(player->settings(), candidate);
        EXPECT_EQ(active, candidate);
        auto saved = PlayerDisplaySettings::load(project_id, defaults, path());
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().settings(), candidate);
        std::filesystem::remove(path());
        std::filesystem::create_directory(path());
        EXPECT_FALSE(preview->apply(defaults, active, application(), now));
        EXPECT_EQ(applications, 1u);
        EXPECT_EQ(active, candidate);
    }

    TEST_F(DisplaySettingsPreviewTest, FailedApplyAndExitDuringTrialLeavePersistedSettingsAlone) {
        const DisplaySettings candidate{1920, 1080, WindowMode::Fullscreen, false};
        EXPECT_FALSE(preview->apply(
            candidate, active,
            [](const auto&) { return Result<void>::failure("No display available"); }, now));
        EXPECT_FALSE(preview->is_pending());
        EXPECT_EQ(active, defaults);
        ASSERT_TRUE(preview->apply(candidate, active, application(), now));
        preview.reset();
        EXPECT_FALSE(std::filesystem::exists(path()));
        EXPECT_EQ(player->settings(), defaults);
    }
}
