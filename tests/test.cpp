#include <gtest/gtest.h>
#include "common/config.h"
#include "common/diagnostics.h"

#include <memory>

// 全局测试环境设置
class CometTestEnvironment: public ::testing::Environment {
public:
    void SetUp() override {
        Comet::Config::Diagnostics diagnostics_config;
        diagnostics_config.log.enable_file_logging = false;
        diagnostics_config.log.level = "warn";
        diagnostics_config.enable_profiler = false;
        m_diagnostics = std::make_unique<Comet::Diagnostics>(diagnostics_config);

        std::cout << "=== Comet Engine Test Suite ===" << std::endl;
        std::cout << "Initializing test environment..." << std::endl;
    }

    void TearDown() override {
        m_diagnostics.reset();
        std::cout << "Test environment cleaned up." << std::endl;
        std::cout << "=== Test Suite Completed ===" << std::endl;
    }

private:
    std::unique_ptr<Comet::Diagnostics> m_diagnostics;
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
