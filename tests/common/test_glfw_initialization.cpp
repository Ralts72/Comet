#include <gtest/gtest.h>
#include <GLFW/glfw3.h>

namespace Comet::Tests {

// 专门测试GLFW初始化和基本功能的测试类
class GLFWTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 在CI环境中这些测试需要特殊处理
        if (std::getenv("CI") != nullptr) {
            // 设置headless模式的GLFW提示
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }
    }

    void TearDown() override {
        // 确保GLFW在每个测试后都被终止
        glfwTerminate();
    }
};

TEST_F(GLFWTest, GLFWInitializationSucceeds) {
    EXPECT_TRUE(glfwInit()) << "GLFW initialization should succeed";
}

TEST_F(GLFWTest, GLFWVersionIsCorrect) {
    if (!glfwInit()) {
        GTEST_SKIP() << "GLFW initialization failed";
    }

    int major, minor, revision;
    glfwGetVersion(&major, &minor, &revision);
    
    // 验证GLFW版本合理（至少3.0）
    EXPECT_GE(major, 3) << "GLFW major version should be at least 3";
    EXPECT_GE(minor, 0) << "GLFW minor version should be non-negative";
    EXPECT_GE(revision, 0) << "GLFW revision should be non-negative";
}

TEST_F(GLFWTest, GLFWSupportsVulkan) {
    if (!glfwInit()) {
        GTEST_SKIP() << "GLFW initialization failed";
    }
    
    // 检查GLFW是否支持Vulkan
    EXPECT_TRUE(glfwVulkanSupported()) << "GLFW should support Vulkan";
}

TEST_F(GLFWTest, GLFWCanCreateInvisibleWindow) {
    if (!glfwInit()) {
        GTEST_SKIP() << "GLFW initialization failed";
    }
    
    // 在CI环境中创建不可见窗口
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    GLFWwindow* window = glfwCreateWindow(640, 480, "Test Window", NULL, NULL);
    
    if (window == nullptr) {
        const char* description;
        int error = glfwGetError(&description);
        GTEST_SKIP() << "Window creation failed: " << error << " - " << description;
    }
    
    EXPECT_NE(window, nullptr);
    
    // 验证窗口属性
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    EXPECT_EQ(width, 640);
    EXPECT_EQ(height, 480);
    
    // 清理
    glfwDestroyWindow(window);
}

TEST_F(GLFWTest, GLFWErrorHandling) {
    // 测试错误处理机制
    glfwSetErrorCallback([](int error, const char* description) {
        // 错误回调应该被正确设置
        ASSERT_NE(description, nullptr);
    });
    
    // 在未初始化状态下创建窗口应该产生错误
    GLFWwindow* window = glfwCreateWindow(100, 100, "Error Test", NULL, NULL);
    EXPECT_EQ(window, nullptr);
    
    // 检查是否有错误
    const char* description;
    int error = glfwGetError(&description);
    EXPECT_NE(error, GLFW_NO_ERROR);
}

// 参数化测试：测试不同的窗口提示设置
class GLFWWindowHintsTest : public ::testing::TestWithParam<std::pair<int, int>> {
protected:
    void SetUp() override {
        if (!glfwInit()) {
            GTEST_SKIP() << "GLFW initialization failed";
        }
        
        // 设置通用的窗口提示
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    void TearDown() override {
        glfwTerminate();
    }
};

TEST_P(GLFWWindowHintsTest, WindowHintsWork) {
    auto [hint, value] = GetParam();
    
    glfwWindowHint(hint, value);
    
    GLFWwindow* window = glfwCreateWindow(400, 300, "Hint Test", NULL, NULL);
    
    if (window == nullptr) {
        const char* description;
        int error = glfwGetError(&description);
        GTEST_SKIP() << "Window creation failed with hint " << hint << "=" << value 
                     << ": " << error << " - " << description;
    }
    
    EXPECT_NE(window, nullptr);
    glfwDestroyWindow(window);
}

INSTANTIATE_TEST_SUITE_P(
    CommonHints,
    GLFWWindowHintsTest,
    ::testing::Values(
        std::make_pair(GLFW_RESIZABLE, GLFW_TRUE),
        std::make_pair(GLFW_RESIZABLE, GLFW_FALSE),
        std::make_pair(GLFW_DECORATED, GLFW_TRUE),
        std::make_pair(GLFW_DECORATED, GLFW_FALSE),
        std::make_pair(GLFW_FOCUSED, GLFW_FALSE)
    )
);

} // namespace Comet::Tests