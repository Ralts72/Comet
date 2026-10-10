#include "config/player_settings.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    TEST(AudioSettingsTest, RejectsEveryInvalidVolumeBeforeApplyingOrSaving) {
        EXPECT_TRUE(AudioSettings{}.validate());
        for(const auto value : {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity()}) {
            EXPECT_FALSE((AudioSettings{value, 1, 1}.validate()));
            EXPECT_FALSE((AudioSettings{1, value, 1}.validate()));
            EXPECT_FALSE((AudioSettings{1, 1, value}.validate()));
        }
        EXPECT_TRUE((AudioSettings{0, 0, 0}.validate()));
    }

    TEST(PlayerAudioSettingsTest, PersistsPerProjectAndDoesNotApplyOnSaveFailure) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "audio.json";
        const auto id = Uuid::generate();
        const AudioSettings defaults{1, 0.8f, 0.7f};
        auto loaded = PlayerAudioSettings::load(id, defaults, path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().settings(), defaults);
        EXPECT_FALSE(std::filesystem::exists(path));
        const AudioSettings choice{0.5f, 0.4f, 0.3f};
        unsigned applications = 0;
        const auto apply = [&](const AudioSettings& settings) {
            EXPECT_EQ(settings, choice);
            ++applications;
            return Result<void>::success();
        };
        ASSERT_TRUE(loaded.value().save_and_apply(choice, apply));
        EXPECT_EQ(applications, 1u);
        auto reopened = PlayerAudioSettings::load(id, defaults, path);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().settings(), choice);
        EXPECT_FALSE(PlayerAudioSettings::load(Uuid::generate(), defaults, path));
        EXPECT_FALSE(loaded.value().save_and_apply({1, 2, 1}, apply));
        std::filesystem::remove(path);
        std::filesystem::create_directory(path);
        EXPECT_FALSE(loaded.value().save_and_apply(defaults, apply));
        EXPECT_EQ(applications, 1u);
        EXPECT_EQ(loaded.value().settings(), choice);
        std::filesystem::remove(path);
        ASSERT_TRUE(write_text_file_atomic(path, "{broken"));
        EXPECT_FALSE(PlayerAudioSettings::load(id, defaults, path));
    }
}
