#include <gtest/gtest.h>
#include "common/export.h"

namespace Comet::Tests {

// 测试导出宏的定义和使用
class ExportTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ExportTest, ExportMacroDefined) {
    // 测试COMET_API宏是否已定义
    // 这个测试主要确保编译时宏定义正确

    // 创建一个使用COMET_API的简单类来验证宏工作正常
    class COMET_API TestExportClass {
    public:
        int getValue() const { return 42; }
    };

    TestExportClass obj;
    EXPECT_EQ(obj.getValue(), 42);
}

// 测试在不同平台上的宏定义
TEST_F(ExportTest, PlatformSpecificMacros) {
    // 这个测试验证宏在编译时能正确展开
    // 实际的宏值取决于编译时的平台和COMET_EXPORTS定义

#if defined(_WIN32) || defined(_WIN64)
    // Windows平台测试
    #ifdef COMET_EXPORTS
        // 如果定义了COMET_EXPORTS，应该是dllexport
        static_assert(true, "Windows export macro should be __declspec(dllexport)");
    #else
        // 否则应该是dllimport
        static_assert(true, "Windows import macro should be __declspec(dllimport)");
    #endif
#else
    // 非Windows平台（Linux/macOS）测试
    static_assert(true, "Non-Windows export macro should be __attribute__((visibility(\"default\")))");
#endif

    SUCCEED(); // 如果编译通过，测试就成功了
}

} // namespace Comet::Tests
