#include "common/process.h"
#include <array>
#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(ProcessTest, WindowsArgumentsPreserveQuotesEmptyValuesAndTrailingBackslashes) {
        EXPECT_EQ(quote_windows_argument(L""), L"\"\"");
        EXPECT_EQ(quote_windows_argument(L"a b"), L"\"a b\"");
        EXPECT_EQ(quote_windows_argument(L"a\"b"), L"\"a\\\"b\"");
        EXPECT_EQ(quote_windows_argument(L"C:\\folder\\"), L"\"C:\\folder\\\\\"");
    }
#ifndef _WIN32
    TEST(ProcessTest, DeliversLiteralArgumentsAndReportsExitStatus) {
        const std::string literal = "中文 'quoted' $HOME `literal`\\\"";
        const std::array<std::string, 3> arguments{"/usr/bin/printf", "%s", literal};
        std::string output;
        const auto result = run_process(arguments, [&](auto text) { output.append(text); });
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(result.value(), 0);
        EXPECT_EQ(output, literal);
        const std::array<std::string, 1> failure{"/usr/bin/false"};
        EXPECT_EQ(run_process(failure).value(), 1);
        EXPECT_FALSE(run_process({}));
    }
#endif
}
