#include <gtest/gtest.h>
#include "core/window/window.h"
#include <GLFW/glfw3.h>

namespace Comet::Tests {

class WindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化GLFW
        if (!glfwInit()) {
            FAIL() << "Failed to initialize GLFW";
        }

        // 设置GLFW不创建OpenGL上下文，因为我们只测试窗口功能
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    void TearDown() override {
        // 清理GLFW
        glfwTerminate();
    }
};

TEST_F(WindowTest, WindowCreation) {
    // 测试窗口创建
    EXPECT_NO_THROW({
        Window window("Test Window", 800, 600);
        EXPECT_NE(window.get_window(), nullptr);
    });
}

TEST_F(WindowTest, WindowParameters) {
    Window window("Test Window", 800, 600);

    // 测试窗口参数
    EXPECT_NE(window.get_window(), nullptr);

    Vec2i size = window.get_size();
    EXPECT_EQ(size.x, 800);
    EXPECT_EQ(size.y, 600);
}

TEST_F(WindowTest, WindowTitle) {
    Window window("My Game Window", 1024, 768);

    // 验证窗口标题（通过GLFW API）
    GLFWwindow* glfwWindow = window.get_window();
    EXPECT_NE(glfwWindow, nullptr);

    // 注意：GLFW没有直接获取窗口标题的API，但我们可以验证窗口创建成功
    EXPECT_TRUE(glfwWindow != nullptr);
}

TEST_F(WindowTest, InitialCloseStatus) {
    Window window("Test Window", 640, 480);

    // 新创建的窗口不应该标记为关闭
    EXPECT_FALSE(window.should_close());
}

TEST_F(WindowTest, EventPolling) {
    Window window("Test Window", 800, 600);

    // 测试事件轮询不会崩溃
    EXPECT_NO_THROW(window.poll_events());

    // 多次调用事件轮询
    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_THROW(window.poll_events());
    }
}

TEST_F(WindowTest, BufferSwapping) {
    Window window("Test Window", 800, 600);

    // 测试缓冲区交换（即使没有OpenGL上下文也不应该崩溃）
    EXPECT_NO_THROW(window.swap_buffers());
}

TEST_F(WindowTest, DifferentSizes) {
    // 测试不同尺寸的窗口
    std::vector<std::pair<int, int>> sizes = {
        {320, 240},   // 小窗口
        {1920, 1080}, // 大窗口
        {1, 1},       // 最小窗口
        {800, 600}    // 标准窗口
    };

    for (const auto& [width, height] : sizes) {
        EXPECT_NO_THROW({
            Window window("Size Test", width, height);
            Vec2i size = window.get_size();

            // 验证尺寸设置正确
            EXPECT_EQ(size.x, width);
            EXPECT_EQ(size.y, height);
        });
    }
}

TEST_F(WindowTest, MultipleWindows) {
    // 测试创建多个窗口
    Window window1("Window 1", 400, 300);
    Window window2("Window 2", 600, 400);
    Window window3("Window 3", 800, 600);

    // 所有窗口都应该有效
    EXPECT_NE(window1.get_window(), nullptr);
    EXPECT_NE(window2.get_window(), nullptr);
    EXPECT_NE(window3.get_window(), nullptr);

    // 所有窗口都应该是不同的
    EXPECT_NE(window1.get_window(), window2.get_window());
    EXPECT_NE(window2.get_window(), window3.get_window());
    EXPECT_NE(window1.get_window(), window3.get_window());
}

TEST_F(WindowTest, WindowLifecycle) {
    // 测试窗口的生命周期
    GLFWwindow* glfwWindow = nullptr;

    {
        Window window("Lifecycle Test", 800, 600);
        glfwWindow = window.get_window();
        EXPECT_NE(glfwWindow, nullptr);

        // 在作用域内窗口应该有效
        EXPECT_FALSE(window.should_close());
    }

    // 窗口对象销毁后，GLFW窗口也应该被清理
    // 注意：这个测试依赖于Window析构函数正确清理GLFW窗口
}

TEST_F(WindowTest, LongWindowTitle) {
    // 测试长窗口标题
    std::string long_title(1000, 'A');

    EXPECT_NO_THROW({
        Window window(long_title, 800, 600);
        EXPECT_NE(window.get_window(), nullptr);
    });
}

TEST_F(WindowTest, EmptyWindowTitle) {
    // 测试空窗口标题
    EXPECT_NO_THROW({
        Window window("", 800, 600);
        EXPECT_NE(window.get_window(), nullptr);
    });
}

TEST_F(WindowTest, SpecialCharactersInTitle) {
    // 测试标题中的特殊字符
    std::string special_title = "Test Window with Special Characters: !@#$%^&*()_+-=[]{}|;':\",./<>?";

    EXPECT_NO_THROW({
        Window window(special_title, 800, 600);
        EXPECT_NE(window.get_window(), nullptr);
    });
}

// 注意：某些测试可能需要根据实际的Window实现进行调整
// 如果Window类有额外的功能（如全屏模式、窗口位置设置等），可以添加相应的测试

} // namespace Comet::Tests
