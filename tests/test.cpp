#include <gtest/gtest.h>
#include "../engine/src/common/config.h"
#include "../engine/src/common/logger.h"

// 全局测试环境设置
class CometTestEnvironment: public ::testing::Environment {
public:
    void SetUp() override {
        Comet::Config config;
        const auto runtime_config = config.load_runtime_config();
        Comet::Logger::init(runtime_config.log);

        std::cout << "=== Comet Engine Test Suite ===" << std::endl;
        std::cout << "Initializing test environment..." << std::endl;
    }

    void TearDown() override {
        Comet::Logger::shutdown();
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
