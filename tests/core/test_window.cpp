#include "core/window.h"
#include "config/config.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#include <memory>

namespace Comet::Tests {
    TEST(WindowTest, WindowsOwnBackendLifetimeAndCloseIndependently) {
        Config::Window config;
        config.width = 64;
        config.height = 64;
        config.title = "Comet Window Lifetime Test";
        {
            auto first = std::make_unique<Window>(config);
            Window second(config);
            EXPECT_FALSE(first->should_close());
            first->request_close();
            EXPECT_TRUE(first->should_close());
            EXPECT_FALSE(second.should_close());
            first.reset();

            second.poll_events();
            EXPECT_GT(second.get_framebuffer_size().x, 0U);
            EXPECT_EQ(second.is_minimized(),
                glfwGetWindowAttrib(second.get(), GLFW_ICONIFIED) == GLFW_TRUE);
            second.request_close();
            EXPECT_TRUE(second.should_close());
        }

        Window reopened(config);
        EXPECT_FALSE(reopened.should_close());
        EXPECT_GT(reopened.get_framebuffer_size().x, 0U);
    }

    TEST(WindowTest, FileDropCopiesCallbackPathsAndConsumesEachEventOnce) {
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
