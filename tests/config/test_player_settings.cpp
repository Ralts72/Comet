#include "config/player_settings.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    namespace {
        template<typename Store, typename Settings>
        void expect_persistence(const char* filename, const Settings& defaults,
            const Settings& choice, const Settings& invalid) {
            TemporaryDirectory directory;
            const auto path = directory.path() / filename;
            const auto id = Uuid::generate();
            auto loaded = Store::load(id, defaults, path);
            ASSERT_TRUE(loaded) << loaded.error();
            EXPECT_EQ(loaded.value().settings(), defaults);
            EXPECT_FALSE(std::filesystem::exists(path));
            unsigned applications = 0;
            const auto apply = [&](const Settings& settings) {
                EXPECT_EQ(settings, choice);
                ++applications;
                return Result<void>::success();
            };
            ASSERT_TRUE(loaded.value().save_and_apply(choice, apply));
            EXPECT_EQ(applications, 1u);
            auto reopened = Store::load(id, defaults, path);
            ASSERT_TRUE(reopened) << reopened.error();
            EXPECT_EQ(reopened.value().settings(), choice);
            EXPECT_FALSE(Store::load(Uuid::generate(), defaults, path));
            EXPECT_FALSE(loaded.value().save_and_apply(invalid, apply));
            std::filesystem::remove(path);
            std::filesystem::create_directory(path);
            EXPECT_FALSE(loaded.value().save_and_apply(defaults, apply));
            EXPECT_EQ(applications, 1u);
            EXPECT_EQ(loaded.value().settings(), choice);
            std::filesystem::remove(path);
            const auto failed = loaded.value().save_and_apply(defaults,
                [](const Settings&) { return Result<void>::failure("Runtime unavailable"); });
            ASSERT_FALSE(failed);
            EXPECT_NE(failed.error().find("saved but not applied"), std::string::npos);
            const auto saved = Store::load(id, choice, path);
            ASSERT_TRUE(saved);
            EXPECT_EQ(saved.value().settings(), defaults);
            EXPECT_EQ(loaded.value().settings(), defaults);
            ASSERT_TRUE(write_text_file_atomic(path, "{broken"));
            EXPECT_FALSE(Store::load(id, defaults, path));
        }
    }

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
        expect_persistence<PlayerAudioSettings>(
            "audio.json", AudioSettings{1, 0.8f, 0.7f}, AudioSettings{0.5f, 0.4f, 0.3f}, {1, 2, 1});
    }

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
        expect_persistence<PlayerQualitySettings>(
            "quality.json", QualitySettings{4, 8, 1}, QualitySettings{1, 2, 0.75f, true}, {3, 8, 1});
    }
}
