#pragma once

#include <cstdlib>
#include <string>

namespace Comet::Tests {

// 测试环境配置类
class TestConfig {
public:
    // 检查是否在CI环境中运行
    static bool IsCI() {
        return std::getenv("CI") != nullptr;
    }

    // 检查是否在headless环境中运行
    static bool IsHeadless() {
        return std::getenv("DISPLAY") == nullptr && 
               std::getenv("XDG_SESSION_TYPE") == nullptr;
    }

    // 检查是否应该跳过图形相关测试
    static bool ShouldSkipGraphicsTests() {
        return IsCI() && IsHeadless();
    }

    // 检查Vulkan是否可用
    static bool IsVulkanAvailable() {
        // 在CI环境中，Vulkan可能不可用
        if (IsCI()) {
            return std::getenv("VULKAN_SDK") != nullptr;
        }
        return true;
    }

    // 获取测试窗口的默认大小
    static std::pair<int, int> GetTestWindowSize() {
        if (IsCI()) {
            return {800, 600}; // CI环境中使用较小的窗口
        }
        return {1024, 768}; // 本地环境中使用标准大小
    }

    // 检查是否应该使用软件渲染
    static bool ShouldUseSoftwareRendering() {
        return IsCI() || std::getenv("COMET_SOFTWARE_RENDERING") != nullptr;
    }
};

// 用于跳过图形测试的宏
#define SKIP_IF_NO_GRAPHICS() \
    if (Comet::Tests::TestConfig::ShouldSkipGraphicsTests()) { \
        GTEST_SKIP() << "Skipping graphics test in headless/CI environment"; \
    }

// 用于跳过Vulkan测试的宏
#define SKIP_IF_NO_VULKAN() \
    if (!Comet::Tests::TestConfig::IsVulkanAvailable()) { \
        GTEST_SKIP() << "Skipping Vulkan test - Vulkan not available"; \
    }

} // namespace Comet::Tests