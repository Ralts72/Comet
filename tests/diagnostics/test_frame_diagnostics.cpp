#include "diagnostics/frame_diagnostics.h"

#include <gtest/gtest.h>

namespace Comet {
    TEST(FrameDiagnosticsTest, PublishesOnlyCompletedEnabledFrames) {
        FrameDiagnostics diagnostics;
        diagnostics.begin_frame(true);
        diagnostics.mark_events();
        diagnostics.set_frame_index(7);
        diagnostics.mark_update();
        diagnostics.mark_prepare();
        diagnostics.mark_runtime_update();
        diagnostics.mark_render_submit();
        diagnostics.finish_frame(true, true);

        ASSERT_TRUE(diagnostics.current());
        const auto& timing = *diagnostics.current();
        EXPECT_EQ(timing.frame_index, 7);
        EXPECT_TRUE(timing.rendered);
        EXPECT_DOUBLE_EQ(timing.total_ms,
            timing.events_ms + timing.update_ms + timing.prepare_ms + timing.render_submit_ms);
        EXPECT_EQ(diagnostics.history().summarize().total.count, 1);

        diagnostics.begin_frame(true);
        diagnostics.cancel_pending_frame();
        ASSERT_TRUE(diagnostics.current());
        EXPECT_EQ(diagnostics.current()->frame_index, 7);
        EXPECT_EQ(diagnostics.history().summarize().total.count, 1);

        diagnostics.begin_frame(false);
        EXPECT_FALSE(diagnostics.current());
        diagnostics.begin_frame(true);
        diagnostics.mark_events();
        diagnostics.finish_frame(false, false);
        EXPECT_FALSE(diagnostics.current());

        diagnostics.begin_frame(true);
        diagnostics.mark_events();
        diagnostics.mark_prepare();
        diagnostics.mark_deferred_wait();
        diagnostics.finish_frame(false, true);
        ASSERT_TRUE(diagnostics.current());
        EXPECT_FALSE(diagnostics.current()->rendered);
        EXPECT_EQ(diagnostics.history().summarize().total.count, 1);
    }
}
