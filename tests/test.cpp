#include <gtest/gtest.h>
#include "common/log_system/log_system.h"
#include <GLFW/glfw3.h>

// 全局测试环境设置
class CometTestEnvironment: public ::testing::Environment {
public:
    void SetUp() override {
        // 初始化GLFW（窗口测试需要）
        if(!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW for tests");
        }

        // 设置GLFW为无API模式（避免创建OpenGL上下文）
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        // 初始化日志系统
        Comet::LogSystem& logSystem = Comet::LogSystem::instance();

        std::cout << "=== Comet Engine Test Suite ===" << std::endl;
        std::cout << "Initializing test environment..." << std::endl;
    }

    void TearDown() override {
        // 清理GLFW
        glfwTerminate();

        std::cout << "Test environment cleaned up." << std::endl;
        std::cout << "=== Test Suite Completed ===" << std::endl;
    }
};

// 主函数
int main(int argc, char** argv) {
    std::cout << "Starting Comet Engine Test Suite..." << std::endl;

    // 初始化Google Test
    ::testing::InitGoogleTest(&argc, argv);

    // 添加全局测试环境
    ::testing::AddGlobalTestEnvironment(new CometTestEnvironment);
    // 运行所有测试
    int result = RUN_ALL_TESTS();

    return result;
}
