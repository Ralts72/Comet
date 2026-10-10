#include <gtest/gtest.h>

#include "config/config.h"
#include "config/config_loader.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

using namespace Comet;

namespace {
    class TemporaryConfigFile final {
    public:
        explicit TemporaryConfigFile(const std::string& contents) {
            const auto id = std::random_device{}();
            m_path = std::filesystem::temp_directory_path()
                     / ("comet_config_test_" + std::to_string(id) + ".json");

            std::ofstream output(m_path);
            output << "{\"test\": " << contents << '}';
        }

        ~TemporaryConfigFile() {
            std::error_code error;
            std::filesystem::remove(m_path, error);
        }

        [[nodiscard]] std::string path() const { return m_path.string(); }

    private:
        std::filesystem::path m_path;
    };
}

TEST(ConfigTest, ProjectProfilesDefineExpectedDiagnosticsPolicy) {
    struct ProfileExpectation {
        const char* name;
        const char* log_level;
        bool enable_profiler;
        bool enable_validation;
    };

    constexpr std::array expectations = {ProfileExpectation{"dev-debug", "trace", true, true},
        ProfileExpectation{"editor-dev", "info", false, false},
        ProfileExpectation{"app-release", "warn", false, false}};

    const std::filesystem::path config_directory =
        std::filesystem::path(std::string(PROJECT_ROOT_DIR)) / "config";
    for(const auto& expectation : expectations) {
        SCOPED_TRACE(expectation.name);
        const auto loaded =
            ConfigLoader{}.load((config_directory / "profiles.json").string(), expectation.name);
        ASSERT_TRUE(loaded) << loaded.error();
        const Config& config = loaded.value();

        EXPECT_EQ(config.diagnostics.log.level, expectation.log_level);
        EXPECT_FALSE(config.diagnostics.log.enable_file_logging);
        EXPECT_EQ(config.diagnostics.enable_profiler, expectation.enable_profiler);
        EXPECT_EQ(config.diagnostics.enable_render_diagnostics,
            std::string_view(expectation.name) == "dev-debug");
        EXPECT_EQ(config.vulkan.enable_validation, expectation.enable_validation);
    }
}

TEST(ConfigTest, RenderDiagnosticsIsIndependentAndValidatesBoolean) {
    EXPECT_FALSE(Config{}.diagnostics.enable_render_diagnostics);
    const TemporaryConfigFile enabled(
        R"({"diagnostics": {"enable_render_diagnostics": true, "enable_profiler": false}})");
    auto loaded = ConfigLoader{}.load(enabled.path(), "test");
    ASSERT_TRUE(loaded) << loaded.error();
    EXPECT_TRUE(loaded.value().diagnostics.enable_render_diagnostics);
    EXPECT_FALSE(loaded.value().diagnostics.enable_profiler);
    const TemporaryConfigFile invalid(R"({"diagnostics": {"enable_render_diagnostics": "wrong"}})");
    loaded = ConfigLoader{}.load(invalid.path(), "test");
    ASSERT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("diagnostics.enable_render_diagnostics"), std::string::npos);
}

TEST(ConfigTest, ParsesExplicitConfiguration) {
    const TemporaryConfigFile file(R"({
        "vulkan": {
            "surface_format": "rgba8_unorm",
            "color_space": "srgb_nonlinear",
            "depth_format": "d24_unorm_s8_uint",
            "swapchain_image_count": 4
        },
        "render": {"max_frames_in_flight": 3},
        "diagnostics": {
            "log_level": "warn",
            "enable_file_logging": true,
            "enable_profiler": false,
            "enable_validation": false
        }
    })");

    const auto loaded = ConfigLoader{}.load(file.path(), "test");
    ASSERT_TRUE(loaded) << loaded.error();
    const Config& config = loaded.value();

    EXPECT_EQ(config.diagnostics.log.level, "warn");
    EXPECT_TRUE(config.diagnostics.log.enable_file_logging);
    EXPECT_FALSE(config.diagnostics.enable_profiler);

    EXPECT_EQ(config.vulkan.surface_format, Format::R8G8B8A8_UNORM);
    EXPECT_EQ(config.vulkan.color_space, ImageColorSpace::SrgbNonlinearKHR);
    EXPECT_EQ(config.vulkan.depth_format, Format::D24_UNORM_S8_UINT);
    EXPECT_EQ(config.vulkan.swapchain_image_count, 4u);
    EXPECT_FALSE(config.vulkan.enable_validation);

    EXPECT_EQ(config.render.max_frames_in_flight, 3u);
}

TEST(ConfigTest, UsesDefaultsForMissingFields) {
    const TemporaryConfigFile file(R"({"diagnostics": {"log_level": "warn"}})");

    const auto loaded = ConfigLoader{}.load(file.path(), "test");
    ASSERT_TRUE(loaded) << loaded.error();
    const Config& config = loaded.value();

    EXPECT_EQ(config.window.width, Config::Window{}.width);
    EXPECT_EQ(config.window.height, Config::Window{}.height);
    EXPECT_EQ(config.diagnostics.log.level, "warn");
    EXPECT_FLOAT_EQ(config.render.max_anisotropy, Config::Render{}.max_anisotropy);
    EXPECT_EQ(config.assets.async.working_bytes, AssetImportLimits{}.async.working_bytes);
}

TEST(ConfigTest, EmptyProfileUsesCppDefaults) {
    const TemporaryConfigFile file("{}");
    const auto loaded = ConfigLoader{}.load(file.path(), "test");
    ASSERT_TRUE(loaded) << loaded.error();
    const auto& config = loaded.value();
    EXPECT_EQ(config.window.width, 960);
    EXPECT_EQ(config.window.height, 720);
    EXPECT_EQ(config.vulkan.msaa_samples, SampleCount::Count4);
    EXPECT_EQ(config.vulkan.present_mode, PresentMode::Immediate);
    EXPECT_FLOAT_EQ(config.render.max_anisotropy, 8);
    EXPECT_EQ(config.render.output_mode, OutputMode::Sdr);
    EXPECT_EQ(config.diagnostics.log.level, Config::Log{}.level);
    EXPECT_EQ(config.assets.source_bytes, AssetImportLimits{}.source_bytes);
}

TEST(ConfigTest, ParsesAssetImportBudgetsAndRejectsInvalidValues) {
    const TemporaryConfigFile file(R"({
        "assets": {
            "source_max_mib": 128,
            "texture_working_mib": 512,
            "mesh_working_mib": 768,
            "mesh_owner_inspect_kib": 32,
            "external_file_mib": 256,
            "external_file_queue": 3,
            "async": {"in_flight": 2, "queued": 16, "working_mib": 1536}
        }
    })");
    const auto loaded = ConfigLoader{}.load(file.path(), "test");
    ASSERT_TRUE(loaded) << loaded.error();
    const auto& assets = loaded.value().assets;
    EXPECT_EQ(assets.source_bytes, 128ull * 1024 * 1024);
    EXPECT_EQ(assets.texture_working_bytes, 512ull * 1024 * 1024);
    EXPECT_EQ(assets.mesh_working_bytes, 768ull * 1024 * 1024);
    EXPECT_EQ(assets.mesh_owner_inspect_bytes, 32ull * 1024);
    EXPECT_EQ(assets.external_file_bytes, 256ull * 1024 * 1024);
    EXPECT_EQ(assets.external_file_queue, 3u);
    EXPECT_EQ(assets.async.in_flight, 2u);
    EXPECT_EQ(assets.async.queued, 16u);
    EXPECT_EQ(assets.async.working_bytes, 1536ull * 1024 * 1024);

    for(const auto invalid : {R"({"assets": {"source_max_mib": 0}})",
            R"({"assets": {"source_max_mib": 2048}})", R"({"assets": {"texture_working_mib": 16}})",
            R"({"assets": {"mesh_owner_inspect_kib": 1048576}})",
            R"({"assets": {"external_file_queue": 0}})",
            R"({"assets": {"async": {"in_flight": 65}}})",
            R"({"assets": {"mesh_working_mib": 4097}})",
            R"({"assets": {"async": {"in_flight": 0}}})"}) {
        const TemporaryConfigFile invalid_file(invalid);
        const auto result = ConfigLoader{}.load(invalid_file.path(), "test");
        ASSERT_FALSE(result) << invalid;
        EXPECT_NE(result.error().find("assets"), std::string::npos);
    }
}

TEST(ConfigTest, ExplicitValidationSettingOverridesDefault) {
    const bool expected = !Config::Vulkan{}.enable_validation;
    const TemporaryConfigFile file(std::string(R"({"diagnostics": {"enable_validation": )")
                                   + (expected ? "true}}" : "false}}"));

    const auto loaded = ConfigLoader{}.load(file.path(), "test");
    ASSERT_TRUE(loaded) << loaded.error();
    const Config& config = loaded.value();

    EXPECT_EQ(config.vulkan.enable_validation, expected);
}

TEST(ConfigTest, RejectsInvalidFieldTypeWithFieldAndFileContext) {
    const TemporaryConfigFile file(R"({"render": {"max_frames_in_flight": "many"}})");

    const auto result = ConfigLoader{}.load(file.path(), "test");
    ASSERT_FALSE(result);
    const auto& message = result.error();
    EXPECT_NE(message.find(file.path()), std::string::npos);
    EXPECT_NE(message.find("render.max_frames_in_flight"), std::string::npos);
    EXPECT_NE(message.find("expected a non-negative integer"), std::string::npos);
}

TEST(ConfigTest, ValidatesRequiredPositiveValues) {
    const TemporaryConfigFile file(R"({"render": {"max_frames_in_flight": 0}})");

    EXPECT_FALSE(ConfigLoader{}.load(file.path(), "test"));
}

TEST(ConfigTest, RejectsUnknownVulkanEnumName) {
    const TemporaryConfigFile file(R"({"vulkan": {"depth_format": "unknown"}})");

    const auto result = ConfigLoader{}.load(file.path(), "test");
    ASSERT_FALSE(result);
    const auto& message = result.error();
    EXPECT_NE(message.find("vulkan.depth_format"), std::string::npos);
    EXPECT_NE(message.find("unknown"), std::string::npos);
}

TEST(ConfigTest, RejectsPlayerSettingsAndUnknownDeveloperKeys) {
    constexpr std::array cases = {std::pair{R"({"window": {"width": 1200}})", "window"},
        std::pair{R"({"vulkan": {"msaa_samples": 8}})", "vulkan.msaa_samples"},
        std::pair{R"({"vulkan": {"present_mode": "fifo"}})", "vulkan.present_mode"},
        std::pair{R"({"render": {"max_anisotropy": 16}})", "render.max_anisotropy"},
        std::pair{R"({"render": {"output_mode": "auto"}})", "render.output_mode"},
        std::pair{R"({"render": {"hdr_headroom": 8}})", "render.hdr_headroom"},
        std::pair{R"({"render": {"hdr_white_level": 1.25}})", "render.hdr_white_level"},
        std::pair{R"({"render": {"enable_vsync": true}})", "render.enable_vsync"},
        std::pair{R"({"diagnostics": {"log_levle": "warn"}})", "diagnostics.log_levle"},
        std::pair{R"({"assets": {"async": {"unknown": 2}}})", "assets.async.unknown"}};
    for(const auto& [contents, key] : cases) {
        SCOPED_TRACE(contents);
        const TemporaryConfigFile file(contents);
        const auto result = ConfigLoader{}.load(file.path(), "test");
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find(file.path()), std::string::npos);
        EXPECT_NE(result.error().find(key), std::string::npos);
        EXPECT_NE(result.error().find("unknown developer setting"), std::string::npos);
    }
}

TEST(ConfigTest, RejectsMalformedProfileAndSectionTypes) {
    for(const auto contents : {"", "null", "[]", R"({"diagnostics": true})", R"({"assets": null})",
            R"({"assets": {"async": []}})", R"({"diagnostics": {"log_level": ["warn"]}})",
            R"({"diagnostics": [})",
            R"({"diagnostics": {"log_level": "info", "log_level": "warn"}})",
            R"({"diagnostics": {}, "diagnostics": {"log_level": "warn"}})",
            R"({"diagnostics": {"enable_validation": "true"}})",
            R"({"vulkan": {"depth_format": 1}})", R"({"render": {"max_frames_in_flight": -1}})",
            R"({"render": {"max_frames_in_flight": 2.5}})"}) {
        SCOPED_TRACE(contents);
        const TemporaryConfigFile file(contents);
        const auto result = ConfigLoader{}.load(file.path(), "test");
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find(file.path()), std::string::npos);
    }
}

TEST(ConfigTest, ReportsMissingFile) {
    EXPECT_FALSE(ConfigLoader{}.load("missing-config.json", "test"));
}

TEST(ConfigTest, RejectsMissingAndMalformedProfileContainers) {
    Tests::TemporaryDirectory directory;
    const auto path = directory.path() / "profiles.json";
    for(const auto contents : {"{}", "[]", "null", R"({"test": {}, "test": {}})"}) {
        SCOPED_TRACE(contents);
        ASSERT_TRUE(write_text_file_atomic(path, contents));
        const auto result = ConfigLoader{}.load(path.string(), "test");
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find(path.string()), std::string::npos);
        EXPECT_NE(result.error().find("test"), std::string::npos);
    }
}
