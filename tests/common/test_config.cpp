#include <gtest/gtest.h>

#include "common/config.h"
#include "common/config_loader.h"

#include <array>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

using namespace Comet;

namespace {
    template<typename T>
    concept HasImplicitLoad = requires(T loader) {
        { loader.load() } -> std::same_as<Config>;
    };

    template<typename T>
    concept HasExplicitLoad = requires(T loader, const std::string& path) {
        { loader.load(path) } -> std::same_as<Config>;
    };

    class TemporaryConfigFile final {
    public:
        explicit TemporaryConfigFile(const std::string& contents) {
            const auto id = std::random_device{}();
            m_path = std::filesystem::temp_directory_path()
                / ("comet_config_test_" + std::to_string(id) + ".yaml");

            std::ofstream output(m_path);
            output << contents;
        }

        ~TemporaryConfigFile() {
            std::error_code error;
            std::filesystem::remove(m_path, error);
        }

        [[nodiscard]] std::string path() const {
            return m_path.string();
        }

    private:
        std::filesystem::path m_path;
    };
}

TEST(ConfigInterfaceTest, RequiresExplicitConfigurationSources) {
    EXPECT_FALSE(HasImplicitLoad<Config>);
    EXPECT_FALSE(HasImplicitLoad<ConfigLoader>);
    EXPECT_TRUE(HasExplicitLoad<ConfigLoader>);
}

TEST(ConfigTest, ProjectProfilesDefineExpectedDiagnosticsPolicy) {
    struct ProfileExpectation {
        const char* name;
        const char* log_level;
        bool enable_profiler;
        bool enable_validation;
    };

    constexpr std::array expectations = {
        ProfileExpectation{"dev-debug", "trace", true, true},
        ProfileExpectation{"editor-dev", "info", false, false},
        ProfileExpectation{"app-release", "warn", false, false}
    };

    const std::filesystem::path config_directory =
        std::filesystem::path(std::string(PROJECT_ROOT_DIR)) / "engine/assets/config";
    for(const auto& expectation: expectations) {
        SCOPED_TRACE(expectation.name);
        const Config config = ConfigLoader{}.load(std::vector<std::string>{
            (config_directory / "common.yaml").string(),
            (config_directory / "profiles" / (std::string(expectation.name) + ".yaml")).string()
        });

        EXPECT_EQ(config.diagnostics.log.level, expectation.log_level);
        EXPECT_FALSE(config.diagnostics.log.enable_file_logging);
        EXPECT_EQ(config.diagnostics.enable_profiler, expectation.enable_profiler);
        EXPECT_EQ(config.vulkan.enable_validation, expectation.enable_validation);
    }
}

TEST(ConfigTest, ParsesExplicitConfiguration) {
    const TemporaryConfigFile file(R"(
vulkan:
  surface_format: rgba8_unorm
  color_space: srgb_nonlinear
  depth_format: d24_unorm_s8_uint
  present_mode: mailbox
  swapchain_image_count: 4
  msaa_samples: 8
render:
  max_frames_in_flight: 3
  clear_color: [0.9, 0.7, 0.5, 0.3]
  enable_vsync: true
  max_anisotropy: 16
window:
  width: 901
  height: 517
  title: "Config Test"
  fullscreen: true
  resizable: false
diagnostics:
  log_level: warn
  enable_file_logging: true
  enable_profiler: false
  enable_validation: false
)");

    const Config config = ConfigLoader{}.load(file.path());

    EXPECT_EQ(config.diagnostics.log.level, "warn");
    EXPECT_TRUE(config.diagnostics.log.enable_file_logging);
    EXPECT_FALSE(config.diagnostics.enable_profiler);

    EXPECT_EQ(config.window.width, 901);
    EXPECT_EQ(config.window.height, 517);
    EXPECT_EQ(config.window.title, "Config Test");
    EXPECT_TRUE(config.window.fullscreen);
    EXPECT_FALSE(config.window.resizable);

    EXPECT_EQ(config.vulkan.surface_format, Format::R8G8B8A8_UNORM);
    EXPECT_EQ(config.vulkan.color_space, ImageColorSpace::SrgbNonlinearKHR);
    EXPECT_EQ(config.vulkan.depth_format, Format::D24_UNORM_S8_UINT);
    EXPECT_EQ(config.vulkan.present_mode, PresentMode::Mailbox);
    EXPECT_EQ(config.vulkan.swapchain_image_count, 4u);
    EXPECT_EQ(config.vulkan.msaa_samples, SampleCount::Count8);
    EXPECT_FALSE(config.vulkan.enable_validation);

    EXPECT_EQ(config.render.max_frames_in_flight, 3u);
    EXPECT_TRUE(config.render.enable_vsync);
    EXPECT_FLOAT_EQ(config.render.max_anisotropy, 16.0f);
    EXPECT_EQ(config.render.clear_color, (std::array<float, 4>{0.9f, 0.7f, 0.5f, 0.3f}));
}

TEST(ConfigTest, UsesDefaultsForMissingFields) {
    const TemporaryConfigFile file("window:\n  width: 960\n");

    const Config config = ConfigLoader{}.load(file.path());

    EXPECT_EQ(config.window.width, 960);
    EXPECT_EQ(config.window.height, Config::Window{}.height);
    EXPECT_EQ(config.diagnostics.log.level, Config::Log{}.level);
    EXPECT_EQ(config.render.clear_color, Config::Render{}.clear_color);
    EXPECT_FLOAT_EQ(config.render.max_anisotropy, Config::Render{}.max_anisotropy);
}

TEST(ConfigTest, ExplicitValidationSettingOverridesDefault) {
    const bool expected = !Config::Vulkan{}.enable_validation;
    const TemporaryConfigFile file(
        std::string("diagnostics:\n  enable_validation: ")
        + (expected ? "true\n" : "false\n"));

    const Config config = ConfigLoader{}.load(file.path());

    EXPECT_EQ(config.vulkan.enable_validation, expected);
}

TEST(ConfigTest, LaterLayersOverrideEarlierLayersBeforeValidation) {
    const TemporaryConfigFile common(R"(
window:
  width: 0
  title: "Shared Title"
render:
  max_anisotropy: 4
diagnostics:
  log_level: info
  enable_profiler: true
)");
    const TemporaryConfigFile profile(R"(
window:
  width: 1200
diagnostics:
  log_level: warn
)");

    const Config config = ConfigLoader{}.load(
        std::vector<std::string>{common.path(), profile.path()});

    EXPECT_EQ(config.window.width, 1200);
    EXPECT_EQ(config.window.title, "Shared Title");
    EXPECT_FLOAT_EQ(config.render.max_anisotropy, 4.0f);
    EXPECT_EQ(config.diagnostics.log.level, "warn");
    EXPECT_TRUE(config.diagnostics.enable_profiler);
}

TEST(ConfigTest, RejectsInvalidFieldTypeWithFieldAndFileContext) {
    const TemporaryConfigFile file("window:\n  width: wide\n");

    try {
        static_cast<void>(ConfigLoader{}.load(file.path()));
        FAIL() << "Expected invalid window.width to fail";
    } catch(const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find(file.path()), std::string::npos);
        EXPECT_NE(message.find("window.width"), std::string::npos);
        EXPECT_NE(message.find("expected an integer"), std::string::npos);
    }
}

TEST(ConfigTest, RejectsInvalidClearColorLength) {
    const TemporaryConfigFile file("render:\n  clear_color: [0.1, 0.2, 0.3]\n");

    EXPECT_THROW(static_cast<void>(ConfigLoader{}.load(file.path())), std::runtime_error);
}

TEST(ConfigTest, ValidatesRequiredPositiveValues) {
    const TemporaryConfigFile file("render:\n  max_frames_in_flight: 0\n");

    EXPECT_THROW(static_cast<void>(ConfigLoader{}.load(file.path())), std::runtime_error);
}

TEST(ConfigTest, RejectsInvalidAnisotropy) {
    const TemporaryConfigFile file("render:\n  max_anisotropy: 0\n");

    EXPECT_THROW(static_cast<void>(ConfigLoader{}.load(file.path())), std::runtime_error);
}

TEST(ConfigTest, RejectsUnknownVulkanEnumName) {
    const TemporaryConfigFile file("vulkan:\n  present_mode: fastest\n");

    try {
        static_cast<void>(ConfigLoader{}.load(file.path()));
        FAIL() << "Expected unknown present mode to fail";
    } catch(const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("vulkan.present_mode"), std::string::npos);
        EXPECT_NE(message.find("fastest"), std::string::npos);
    }
}

TEST(ConfigTest, RejectsUnsupportedMsaaSampleCount) {
    const TemporaryConfigFile file("vulkan:\n  msaa_samples: 3\n");

    EXPECT_THROW(static_cast<void>(ConfigLoader{}.load(file.path())), std::runtime_error);
}

TEST(ConfigTest, ThrowsForMissingFile) {
    EXPECT_THROW(static_cast<void>(ConfigLoader{}.load("missing-config.yaml")), std::runtime_error);
}

TEST(ConfigTest, RejectsEmptyLayerList) {
    EXPECT_THROW(
        static_cast<void>(ConfigLoader{}.load(std::vector<std::string>{})),
        std::runtime_error);
}
