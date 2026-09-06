#include "core/window.h"

#include <gtest/gtest.h>
#include <memory>
#include <type_traits>

namespace Comet::Tests {
    static_assert(!std::is_copy_constructible_v<Window>);
    static_assert(!std::is_copy_assignable_v<Window>);

    class WindowTest: public testing::Test {
    protected:
        Config::Window config{.width = 64, .height = 64, .title = "Comet Window Test"};
    };

    TEST_F(WindowTest, InitializesPlatformAndCreatesVulkanWindowDirectly) {
        Window window(config);
        ASSERT_NE(window.get(), nullptr);
        EXPECT_TRUE(glfwVulkanSupported());
        EXPECT_EQ(glfwGetWindowAttrib(window.get(), GLFW_CLIENT_API), GLFW_NO_API);
        EXPECT_EQ(glfwGetWindowAttrib(window.get(), GLFW_RESIZABLE), GLFW_TRUE);
    }

    TEST_F(WindowTest, DestroyingOneWindowDoesNotDestroyAnother) {
        auto first = std::make_unique<Window>(config);
        Window second(config);
        first.reset();
        glfwSetWindowShouldClose(second.get(), GLFW_TRUE);
        EXPECT_TRUE(second.should_close());
        EXPECT_GT(second.get_framebuffer_size().x, 0u);
        second.poll_events();
    }

    TEST_F(WindowTest, RepeatedWindowsKeepPlatformAliveAndResetCreationHints) {
        for(unsigned index = 0; index < 24; ++index) {
            {
                Window window(config);
                // 这些是全局 creation hints，不应污染下一扇 Comet 窗口。
                glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
                glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
                EXPECT_EQ(glfwGetWindowAttrib(window.get(), GLFW_DECORATED), GLFW_TRUE);
                EXPECT_EQ(
                    glfwGetWindowAttrib(window.get(), GLFW_CLIENT_API), GLFW_NO_API);
            }
            glfwGetError(nullptr);
            EXPECT_GE(glfwGetTime(), 0.0);
            EXPECT_EQ(glfwGetError(nullptr), GLFW_NO_ERROR);
        }
    }
}
