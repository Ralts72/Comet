#include <gtest/gtest.h>
#include <GLFW/glfw3.h>

namespace Comet::Tests {
    class GLFWTest: public ::testing::Test {
    protected:
        void SetUp() override {
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }

        void TearDown() override {
            if(m_window) {
                glfwDestroyWindow(m_window);
            }
            glfwTerminate();
        }

        GLFWwindow* m_window = nullptr;
    };

    TEST_F(GLFWTest, CreatesHiddenVulkanWindow) {
        ASSERT_TRUE(glfwVulkanSupported());
        m_window = glfwCreateWindow(640, 480, "Comet Test", nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);
        EXPECT_EQ(glfwGetWindowAttrib(m_window, GLFW_VISIBLE), GLFW_FALSE);
        EXPECT_EQ(glfwGetWindowAttrib(m_window, GLFW_CLIENT_API), GLFW_NO_API);
    }
}
