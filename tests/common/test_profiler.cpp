#include <gtest/gtest.h>

#include "common/profiler.h"

using namespace Comet;

TEST(ProfilerTest, RuntimeSwitchCannotBypassBuildCapability) {
    const bool was_enabled = Profiler::is_enabled();

    Profiler::set_enabled(false);
    EXPECT_FALSE(Profiler::is_enabled());
    EXPECT_FALSE(Profiler::begin_sample("disabled"));

    Profiler::set_enabled(true);
    EXPECT_EQ(Profiler::is_enabled(), Profiler::is_available());
    if(Profiler::begin_sample("enabled")) {
        Profiler::end_sample();
    }

    Profiler::set_enabled(was_enabled);
}
