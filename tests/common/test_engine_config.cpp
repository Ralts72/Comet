#include <gtest/gtest.h>
#include "common/config_loader.h"

using namespace Comet;

TEST(RuntimeConfigTest, ParsesRuntimeConfigurationFromLoadedConfig) {
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
    ASSERT_EQ(config.render.clear_color.size(), 4u);
    EXPECT_FLOAT_EQ(config.render.clear_color[0], 0.2f);
    EXPECT_FLOAT_EQ(config.render.clear_color[1], 0.4f);
    EXPECT_FLOAT_EQ(config.render.clear_color[2], 0.1f);
    EXPECT_FLOAT_EQ(config.render.clear_color[3], 1.0f);
}
