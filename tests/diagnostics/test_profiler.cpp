#include <gtest/gtest.h>

#include "diagnostics/profiler.h"

#include <type_traits>

using namespace Comet;

static_assert(!std::is_copy_constructible_v<ScopedSample>);
static_assert(!std::is_copy_assignable_v<ScopedSample>);
static_assert(!std::is_move_constructible_v<ScopedSample>);
static_assert(!std::is_move_assignable_v<ScopedSample>);

TEST(ProfilerTest, RuntimeSwitchCannotBypassBuildCapability) {
    const bool was_enabled = Profiler::is_enabled();

    Profiler::set_enabled(false);
    EXPECT_FALSE(Profiler::is_enabled());
    EXPECT_FALSE(Profiler::begin_sample("disabled"));

    Profiler::set_enabled(true);
    {
        PROFILE_SCOPE("first");
        PROFILE_SCOPE("second");
    }
    EXPECT_EQ(Profiler::is_enabled(), Profiler::is_available());
    if(Profiler::begin_sample("enabled")) {
        Profiler::end_sample();
    }

    Profiler::set_enabled(was_enabled);
}
