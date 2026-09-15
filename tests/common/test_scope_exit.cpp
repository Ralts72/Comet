#include "common/scope_exit.h"

#include <gtest/gtest.h>

#include <memory>

namespace Comet::Tests {
    TEST(ScopeExitTest, CleansUpOnEarlyReturn) {
        int cleanups = 0;
        const auto operation = [&] {
            ScopeExit cleanup([&] { ++cleanups; });
            return false;
        };
        EXPECT_FALSE(operation());
        EXPECT_EQ(cleanups, 1);
    }

    TEST(ScopeExitTest, ReleaseCommitsWithoutRunningCleanup) {
        int cleanups = 0;
        {
            ScopeExit cleanup([&] { ++cleanups; });
            cleanup.release();
            cleanup.release();
        }
        EXPECT_EQ(cleanups, 0);
    }

    TEST(ScopeExitTest, SupportsMoveOnlyCleanupAndReverseDestruction) {
        int state = 0;
        {
            ScopeExit first(
                [token = std::make_unique<int>(1), &state] { state = state * 10 + *token; });
            ScopeExit second([&] { state = 2; });
        }
        EXPECT_EQ(state, 21);
    }
}
