#include <gtest/gtest.h>
#include "common/config.h"

using namespace Comet;

namespace {
    template<typename T>
    concept HasPublicLoad = requires(T config) {
        config.load();
    };

    template<typename T>
    concept HasPublicReload = requires(T config) {
        config.reload();
    };

    template<typename T>
    concept HasPublicGetRuntimeConfig = requires(T config) {
        { config.get_runtime_config() } -> std::same_as<Config::Runtime>;
    };

    template<typename T>
    concept HasPublicGet = requires(T config) {
        { config.template get<int>("window.width") } -> std::same_as<int>;
    };

    template<typename T>
    concept HasPublicGetWithDefault = requires(T config) {
        { config.template get<int>("window.width", 0) } -> std::same_as<int>;
    };

    template<typename T>
    concept HasPublicHas = requires(T config) {
        { config.has("window.width") } -> std::same_as<bool>;
    };

    template<typename T>
    concept HasPublicGetNode = requires(T config) {
        config.get_node("window.width");
    };
}

TEST(ConfigInterfaceTest, ExposesOnlyRuntimeConfigLoading) {
    EXPECT_FALSE(HasPublicLoad<Config>);
    EXPECT_FALSE(HasPublicReload<Config>);
    EXPECT_FALSE(HasPublicGetRuntimeConfig<Config>);
    EXPECT_FALSE(HasPublicGet<Config>);
    EXPECT_FALSE(HasPublicGetWithDefault<Config>);
    EXPECT_FALSE(HasPublicHas<Config>);
    EXPECT_FALSE(HasPublicGetNode<Config>);
}

TEST(ConfigTest, LoadRuntimeConfigParsesScalarValues) {
    Config loader;
    const Config::Runtime config = loader.load_runtime_config();

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
}

TEST(ConfigTest, LoadRuntimeConfigParsesRenderValues) {
    Config loader;
    const Config::Runtime config = loader.load_runtime_config();

    EXPECT_FALSE(config.render.enable_vsync);
    EXPECT_FLOAT_EQ(config.render.clear_color[0], 0.2f);
    EXPECT_FLOAT_EQ(config.render.clear_color[1], 0.4f);
    EXPECT_FLOAT_EQ(config.render.clear_color[2], 0.1f);
    EXPECT_FLOAT_EQ(config.render.clear_color[3], 1.0f);
}

TEST(ConfigTest, LoadRuntimeConfigThrowsForMissingFile) {
    Config loader;
    EXPECT_THROW(loader.load_runtime_config("missing-config.yaml"), std::runtime_error);
}
