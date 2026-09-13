#include "render/frame_scheduler.h"

#include "graphics/device.h"
#include "graphics/queue.h"
#include "support/engine_fixture.h"

namespace Comet::Tests {
    class FrameSchedulerTest: public EngineTest {};

    TEST_F(FrameSchedulerTest, DoesNotWaitOnResetFenceWithoutSubmission) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        auto& slot = frames.get_current_frame_slot();
        device.reset_fences(std::span(&slot.in_flight_fence, 1));
        ASSERT_EQ(device.get().getFenceStatus(slot.in_flight_fence.get()), vk::Result::eNotReady);

        frames.wait_for_all_slots();
        frames.wait_for_current_slot();

        EXPECT_EQ(frames.get_completed_frame_serial(), 0u);
        EXPECT_EQ(slot.last_submission_serial, 0u);
        EXPECT_EQ(device.get().getFenceStatus(slot.in_flight_fence.get()), vk::Result::eNotReady);
    }

    TEST_F(FrameSchedulerTest, RegistersImageAndRetainsOwnersOnlyForSubmittedFrames) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(2);
        for(uint32_t image = 0; image < 2; ++image) {
            frames.wait_for_current_slot();
            frames.begin_frame(image);
            auto& slot = frames.get_current_frame_slot();
            EXPECT_EQ(
                device.get().getFenceStatus(slot.in_flight_fence.get()), vk::Result::eSuccess);
            EXPECT_FALSE(frames.get_swapchain_image_state(image).in_flight_frame_slot);
            EXPECT_EQ(slot.last_submission_serial, image);

            auto resource = std::make_shared<int>(42);
            const std::weak_ptr<int> retained = resource;
            frames.retain_current_frame_resource(resource);
            slot.command_buffer.begin();
            slot.command_buffer.end();
            const auto completion = frames.submit({}, {});
            ASSERT_TRUE(completion) << completion.error();
            EXPECT_TRUE(completion.value().is_valid());
            EXPECT_EQ(slot.last_submission_serial, image + 1);
            EXPECT_EQ(frames.get_swapchain_image_state(image).in_flight_frame_slot, 0u);
            frames.end_frame();

            resource.reset();
            EXPECT_FALSE(retained.expired());
            frames.wait_for_all_slots();
            EXPECT_TRUE(completion.value().is_complete());
            EXPECT_TRUE(retained.expired());
            EXPECT_EQ(frames.get_completed_frame_serial(), image + 1);
        }
    }
}
