#include "render/overlay_record_context.h"

#include "graphics/command/command_buffer.h"
#include "render/frame_scheduler.h"
#include "support/engine_fixture.h"

namespace Comet::Tests {
    class OverlayRecordContextTest: public EngineTest {};

    TEST_F(OverlayRecordContextTest, RetainsResourcesThroughSubmissionAndRetiresOnSlotReuse) {
        auto& renderer = engine->get_renderer();
        const auto slots = renderer.get_frame_scheduler().get_frame_slot_count();
        auto owner = std::make_shared<int>(42);
        const std::weak_ptr<int> retained = owner;
        uint32_t calls = 0;
        renderer.set_overlay({.render = [&](OverlayRecordContext& context) {
            EXPECT_EQ(context.frame_serial(), calls + 1);
            EXPECT_EQ(context.frame_slot(), calls % slots);
            EXPECT_EQ(&context.command_buffer(),
                &renderer.get_frame_scheduler().get_current_frame_slot().command_buffer);
            context.wait_for({}, Flags<PipelineStage>(PipelineStage::FragmentShader));
            if(calls++ == 0) {
                context.retain(owner);
                context.retain(owner);
                owner.reset();
            }
            return Result<void, GraphicsError>::success();
        }});
        for(uint32_t index = 0; index <= slots; ++index) {
            const auto prepared = renderer.prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error();
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            EXPECT_EQ(retained.expired(), index == slots);
            ASSERT_TRUE(renderer.render_frame());
            if(index < slots)
                EXPECT_FALSE(retained.expired());
        }
        EXPECT_TRUE(retained.expired());
        renderer.set_overlay({});
    }

    TEST_F(OverlayRecordContextTest, RecordingFailureStopsSubmissionAndFurtherFrames) {
        auto& renderer = engine->get_renderer();
        renderer.set_overlay({.render = [](OverlayRecordContext&) {
            return Result<void, GraphicsError>::failure({"overlay recording failed"});
        }});
        const auto prepared = renderer.prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        const auto result = renderer.render_frame();
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, "overlay recording failed");
        EXPECT_EQ(
            renderer.get_frame_scheduler().get_current_frame_slot().last_submission_serial, 0u);
        EXPECT_FALSE(renderer.prepare_frame());
        renderer.set_overlay({});
    }
}
