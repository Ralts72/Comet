#include <gtest/gtest.h>

#include "common/config.h"
#include "common/config_loader.h"

#include <concepts>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

using namespace Comet;

namespace {
    template<typename T>
    concept HasPublicLoad = requires(T loader) {
        { loader.load() } -> std::same_as<Config>;
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

TEST(ConfigInterfaceTest, SeparatesDataFromLoading) {
    EXPECT_FALSE(HasPublicLoad<Config>);
    EXPECT_TRUE(HasPublicLoad<ConfigLoader>);
}

TEST(ConfigTest, LoadsProjectConfiguration) {
    const Config config = ConfigLoader{}.load();

    EXPECT_EQ(config.log.level, "info");
    EXPECT_FALSE(config.log.enable_file_logging);

    EXPECT_EQ(config.window.width, 720);
    EXPECT_EQ(config.window.height, 720);
    EXPECT_EQ(config.window.title, "Comet Engine");
    EXPECT_FALSE(config.window.fullscreen);
    EXPECT_TRUE(config.window.resizable);

    EXPECT_EQ(config.vulkan.surface_format, 50);
    EXPECT_EQ(config.vulkan.color_space, 0);
    EXPECT_EQ(config.vulkan.depth_format, 126);
    EXPECT_EQ(config.vulkan.present_mode, 0);
    EXPECT_EQ(config.vulkan.swapchain_image_count, 3u);
    EXPECT_EQ(config.vulkan.msaa_samples, 4);
    EXPECT_TRUE(config.vulkan.enable_validation);

    EXPECT_EQ(config.render.max_frames_in_flight, 2u);
    EXPECT_FALSE(config.render.enable_vsync);
    EXPECT_FLOAT_EQ(config.render.max_anisotropy, 8.0f);
    EXPECT_EQ(config.render.clear_color, (std::array<float, 4>{0.2f, 0.4f, 0.1f, 1.0f}));
}

TEST(ConfigTest, UsesDefaultsForMissingFields) {
    const TemporaryConfigFile file("window:\n  width: 960\n");

    const Config config = ConfigLoader{}.load(file.path());

    EXPECT_EQ(config.window.width, 960);
    EXPECT_EQ(config.window.height, Config::Window{}.height);
    EXPECT_EQ(config.log.level, Config::Log{}.level);
    EXPECT_EQ(config.render.clear_color, Config::Render{}.clear_color);
    EXPECT_FLOAT_EQ(config.render.max_anisotropy, Config::Render{}.max_anisotropy);
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

TEST(ConfigTest, ThrowsForMissingFile) {
    EXPECT_THROW(static_cast<void>(ConfigLoader{}.load("missing-config.yaml")), std::runtime_error);
}
