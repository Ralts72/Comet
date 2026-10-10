#include "config/player_display_settings.h"
#include "common/file_io.h"
#include "common/player_settings_path.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class PlayerDisplaySettingsTest: public testing::Test {
    protected:
        TemporaryDirectory directory;
        Uuid project_id = Uuid::generate();
        DisplaySettings defaults{1280, 720, WindowMode::Windowed, true};
        std::filesystem::path path() const { return directory.path() / "display.json"; }
    };

    TEST_F(PlayerDisplaySettingsTest, StartsWithProjectDefaultsAndRestoresUserChoice) {
        auto loaded = PlayerDisplaySettings::load(project_id, defaults, path());
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().settings(), defaults);
        EXPECT_FALSE(std::filesystem::exists(path()));
        const DisplaySettings candidate{960, 720, WindowMode::Borderless, false};
        bool applied = false;
        ASSERT_TRUE(loaded.value().save_and_apply(candidate, [&](const DisplaySettings& value) {
            EXPECT_EQ(value, candidate);
            EXPECT_TRUE(std::filesystem::exists(path()));
            applied = true;
            return Result<void>::success();
        }));
        EXPECT_TRUE(applied);
        auto reopened = PlayerDisplaySettings::load(project_id, DisplaySettings{}, path());
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().settings(), candidate);
        EXPECT_FALSE(PlayerDisplaySettings::load(Uuid::generate(), defaults, path()));
        const auto first = player_settings_directory(project_id);
        const auto other = player_settings_directory(Uuid::generate());
        ASSERT_TRUE(first);
        ASSERT_TRUE(other);
        EXPECT_NE(first.value(), other.value());
    }

    TEST_F(PlayerDisplaySettingsTest, InvalidValuesAndSaveFailureNeverApply) {
        auto loaded = PlayerDisplaySettings::load(project_id, defaults, path());
        ASSERT_TRUE(loaded);
        unsigned applications = 0;
        const auto apply = [&](const DisplaySettings&) {
            ++applications;
            return Result<void>::success();
        };
        EXPECT_FALSE(loaded.value().save_and_apply({0, 720}, apply));
        EXPECT_FALSE(loaded.value().save_and_apply({960, -1}, apply));
        EXPECT_FALSE(loaded.value().save_and_apply({960, 720, static_cast<WindowMode>(99)}, apply));
        std::filesystem::create_directory(path());
        EXPECT_FALSE(loaded.value().save_and_apply({960, 720}, apply));
        EXPECT_EQ(applications, 0U);
        EXPECT_EQ(loaded.value().settings(), defaults);
        EXPECT_FALSE(loaded.value().save_and_apply(defaults, {}));
    }

    TEST_F(PlayerDisplaySettingsTest, ReportsPersistedButUnappliedStateAndRejectsMalformedFiles) {
        auto loaded = PlayerDisplaySettings::load(project_id, defaults, path());
        ASSERT_TRUE(loaded);
        const DisplaySettings candidate{1920, 1080, WindowMode::Fullscreen, false};
        const auto result = loaded.value().save_and_apply(candidate,
            [](const DisplaySettings&) { return Result<void>::failure("No display available"); });
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find("saved but not applied"), std::string::npos);
        EXPECT_EQ(loaded.value().settings(), candidate);
        ASSERT_TRUE(write_text_file_atomic(path(), "{broken"));
        EXPECT_FALSE(PlayerDisplaySettings::load(project_id, defaults, path()));
    }
}
