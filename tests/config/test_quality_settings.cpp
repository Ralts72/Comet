#include "config/player_settings.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    TEST(QualitySettingsTest, ValidatesDeviceIndependentRangesAndScalesOddTinyTargets) {
        QualitySettings settings;
        EXPECT_TRUE(settings.validate());
        settings.render_scale = 0.5f;
        EXPECT_EQ(settings.scene_size({101, 53}), Math::Vec2u(51, 27));
        EXPECT_EQ(settings.scene_size({1, 1}), Math::Vec2u(1, 1));
        settings.msaa_samples = 3;
        EXPECT_FALSE(settings.validate());
        settings.msaa_samples = 1;
        for(const auto value : {0.0f, 17.0f, std::numeric_limits<float>::quiet_NaN()}) {
            settings.max_anisotropy = value;
            EXPECT_FALSE(settings.validate());
        }
        settings.max_anisotropy = 1;
        for(const auto value : {0.49f, 1.01f, std::numeric_limits<float>::infinity()}) {
            settings.render_scale = value;
            EXPECT_FALSE(settings.validate());
        }
    }

    TEST(PlayerQualitySettingsTest, PersistsPerProjectAndDoesNotApplyOnSaveFailure) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "quality.json";
        const auto id = Uuid::generate();
        const QualitySettings defaults{4, 8, 1};
        auto loaded = PlayerQualitySettings::load(id, defaults, path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().settings(), defaults);
        EXPECT_FALSE(std::filesystem::exists(path));
        const QualitySettings choice{1, 2, 0.75f};
        unsigned applications = 0;
        const auto apply = [&](const QualitySettings& settings) {
            EXPECT_EQ(settings, choice);
            ++applications;
            return Result<void>::success();
        };
        ASSERT_TRUE(loaded.value().save_and_apply(choice, apply));
        EXPECT_EQ(applications, 1u);
        auto reopened = PlayerQualitySettings::load(id, defaults, path);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().settings(), choice);
        EXPECT_FALSE(PlayerQualitySettings::load(Uuid::generate(), defaults, path));
        EXPECT_FALSE(loaded.value().save_and_apply({3, 8, 1}, apply));
        std::filesystem::remove(path);
        std::filesystem::create_directory(path);
        EXPECT_FALSE(loaded.value().save_and_apply(defaults, apply));
        EXPECT_EQ(applications, 1u);
        EXPECT_EQ(loaded.value().settings(), choice);
        std::filesystem::remove(path);
        ASSERT_TRUE(write_text_file_atomic(path, "{broken"));
        EXPECT_FALSE(PlayerQualitySettings::load(id, defaults, path));
    }
}
