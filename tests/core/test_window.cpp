#include "core/window.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(WindowTest, FileDropCopiesCallbackPathsAndConsumesEachEventOnce) {
        if(glfwInit() != GLFW_TRUE)
            GTEST_SKIP() << "GLFW initialization failed";
        Config::Window config;
        config.width = 64;
        config.height = 64;
        config.title = "Comet File Drop Test";
        Window window(config);
        const auto callback = glfwSetDropCallback(window.get(), nullptr);
        ASSERT_NE(callback, nullptr);
        glfwSetDropCallback(window.get(), callback);
        char first[] = "/external/model.gltf";
        const char* paths[]{first, "/external/纹理.png"};
        double x, y;
        glfwGetCursorPos(window.get(), &x, &y);
        callback(window.get(), 2, paths);
        first[1] = 'X';
        callback(window.get(), 1, paths + 1);
        const auto events = window.take_file_drops();
        ASSERT_EQ(events.size(), 2);
        ASSERT_EQ(events[0].paths.size(), 2);
        EXPECT_EQ(events[0].paths[0], "/external/model.gltf");
        EXPECT_EQ(
            events[0].position, Math::Vec2(static_cast<float>(x), static_cast<float>(y)));
        EXPECT_EQ(events[1].paths[0], std::filesystem::path(u8"/external/纹理.png"));
        EXPECT_TRUE(window.take_file_drops().empty());
    }
}
