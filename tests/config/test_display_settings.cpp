#include "config/player_settings.h"
#include "common/file_io.h"
#include "common/player_settings_path.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <limits>

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
        const DisplaySettings candidate{
            960, 720, WindowMode::Borderless, false, {OutputMode::Auto, 6.0f, 1.25f}, 144};
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
        auto invalid_output = defaults;
        invalid_output.output.mode = static_cast<OutputMode>(99);
        EXPECT_FALSE(loaded.value().save_and_apply(invalid_output, apply));
        invalid_output = defaults;
        for(const float value : {0.0f, 17.0f, std::numeric_limits<float>::quiet_NaN()}) {
            invalid_output.output.hdr_headroom = value;
            EXPECT_FALSE(loaded.value().save_and_apply(invalid_output, apply));
        }
        invalid_output = defaults;
        for(const float value : {0.4f, 2.1f, std::numeric_limits<float>::infinity()}) {
            invalid_output.output.hdr_white_level = value;
            EXPECT_FALSE(loaded.value().save_and_apply(invalid_output, apply));
        }
        std::filesystem::create_directory(path());
        EXPECT_FALSE(loaded.value().save_and_apply({960, 720}, apply));
        EXPECT_EQ(applications, 0U);
        EXPECT_EQ(loaded.value().settings(), defaults);
        EXPECT_FALSE(loaded.value().save_and_apply(defaults, {}));
    }

    TEST_F(PlayerDisplaySettingsTest, MissingFieldsInheritDefaultsAndExplicitZeroOverridesThem) {
        defaults.output = {OutputMode::Hdr, 8.0f, 0.75f};
        defaults.frame_rate_limit = 240;
        for(const auto& [field, expected] : {std::pair{"", 240}, {",\"frame_rate_limit\":0", 0}}) {
            SCOPED_TRACE(field);
            const auto contents = std::string("{\"version\":1,\"project_id\":\"")
                                  + project_id.to_string()
                                  + "\",\"display\":{\"width\":960,\"height\":720,"
                                    "\"mode\":\"borderless\",\"vsync\":false"
                                  + field + "}}";
            ASSERT_TRUE(write_text_file_atomic(path(), contents));
            const auto loaded = PlayerDisplaySettings::load(project_id, defaults, path());
            ASSERT_TRUE(loaded) << loaded.error();
            EXPECT_EQ(loaded.value().settings(), (DisplaySettings{960, 720, WindowMode::Borderless,
                                                     false, defaults.output, expected}));
        }
    }

    TEST_F(PlayerDisplaySettingsTest, RejectsInvalidFrameRateLimitsBeforeSavingOrLoading) {
        auto loaded = PlayerDisplaySettings::load(project_id, defaults, path());
        ASSERT_TRUE(loaded);
        for(const auto limit : {-1, 1001}) {
            auto candidate = defaults;
            candidate.frame_rate_limit = limit;
            EXPECT_FALSE(loaded.value().save(candidate));
            EXPECT_FALSE(std::filesystem::exists(path()));
        }
        for(const auto* value : {"-1", "1001", "0.5", "true", "\"60\"", "null"}) {
            SCOPED_TRACE(value);
            const auto text = std::string("{\"version\":1,\"project_id\":\"")
                              + project_id.to_string()
                              + "\",\"display\":{\"width\":960,\"height\":720,"
                                "\"mode\":\"windowed\",\"vsync\":false,\"frame_rate_limit\":"
                              + value + "}}";
            ASSERT_TRUE(write_text_file_atomic(path(), text));
            EXPECT_FALSE(PlayerDisplaySettings::load(project_id, defaults, path()));
        }
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
