#include "render/frame_scheduler.h"

#include "graphics/device.h"
#include "graphics/queue.h"
#include "support/engine_fixture.h"

#include <array>

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
        int identity = 0;
        for(uint32_t image = 0; image < 2; ++image) {
            frames.wait_for_current_slot();
            frames.begin_frame(image);
            auto& slot = frames.get_current_frame_slot();
            EXPECT_EQ(
                device.get().getFenceStatus(slot.in_flight_fence.get()), vk::Result::eSuccess);
            EXPECT_FALSE(frames.get_swapchain_image_state(image).in_flight_frame_slot);
            EXPECT_EQ(frames.get_swapchain_image_state(image).last_submission_serial, 0u);
            EXPECT_EQ(slot.last_submission_serial, image);

            auto resource = std::make_shared<int>(42);
            const std::weak_ptr<int> retained = resource;
            // The same address represents a new owner after the previous frame completed.
            frames.retain_current_frame_resource(std::shared_ptr<void>(resource, &identity));
            frames.retain_current_frame_resource(std::shared_ptr<void>(resource, &identity));
            slot.command_buffer.begin();
            slot.command_buffer.end();
            const auto completion = frames.submit({}, {});
            ASSERT_TRUE(completion) << completion.error();
            EXPECT_TRUE(completion.value().is_valid());
            EXPECT_EQ(slot.last_submission_serial, image + 1);
            EXPECT_EQ(frames.get_swapchain_image_state(image).in_flight_frame_slot, 0u);
            EXPECT_EQ(frames.get_swapchain_image_state(image).last_submission_serial, image + 1);
            frames.end_frame();

            resource.reset();
            EXPECT_FALSE(retained.expired());
            frames.wait_for_all_slots();
            EXPECT_TRUE(completion.value().is_complete());
            EXPECT_TRUE(retained.expired());
            EXPECT_EQ(frames.get_completed_frame_serial(), image + 1);
        }
    }

    TEST_F(FrameSchedulerTest, SharedAddressKeepsOwnersIndependentBetweenSlots) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        int identity = 0;
        std::array<std::weak_ptr<int>, 2> retained;
        for(uint32_t image = 0; image < 2; ++image) {
            frames.wait_for_current_slot();
            frames.begin_frame(image);
            auto resource = std::make_shared<int>(image);
            retained[image] = resource;
            frames.retain_current_frame_resource(std::shared_ptr<void>(resource, &identity));
            auto& command = frames.get_current_command_buffer();
            command.begin();
            command.end();
            ASSERT_TRUE(frames.submit({}, {}));
            frames.end_frame();
        }
        EXPECT_FALSE(retained[0].expired());
        EXPECT_FALSE(retained[1].expired());
        frames.wait_for_current_slot();
        EXPECT_TRUE(retained[0].expired());
        EXPECT_FALSE(retained[1].expired());
        frames.wait_for_all_slots();
        EXPECT_TRUE(retained[1].expired());
    }

    TEST_F(FrameSchedulerTest, CompletedImageDoesNotCollectANewerSubmissionInItsPreviousSlot) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(3);
        std::weak_ptr<int> newer_owner;
        for(uint32_t image = 0; image < 3; ++image) {
            frames.wait_for_current_slot();
            frames.begin_frame(image);
            if(image == 2) {
                auto resource = std::make_shared<int>(42);
                newer_owner = resource;
                frames.retain_current_frame_resource(resource);
            }
            auto& command = frames.get_current_command_buffer();
            command.begin();
            command.end();
            const auto submitted = frames.submit({}, {});
            ASSERT_TRUE(submitted) << submitted.error();
            frames.end_frame();
            submitted.value().wait();
        }
        EXPECT_EQ(frames.get_swapchain_image_state(0).last_submission_serial, 1u);
        EXPECT_EQ(frames.get_current_frame_slot().last_submission_serial, 2u);
        frames.wait_for_current_slot();
        EXPECT_EQ(frames.get_completed_frame_serial(), 2u);
        frames.begin_frame(0);
        // Image 0's submission completed before slot 0 was reused for submission 3.
        EXPECT_EQ(frames.get_completed_frame_serial(), 2u);
        EXPECT_FALSE(newer_owner.expired());
        auto& command = frames.get_current_command_buffer();
        command.begin();
        command.end();
        ASSERT_TRUE(frames.submit({}, {}));
        frames.end_frame();
        frames.wait_for_all_slots();
        EXPECT_TRUE(newer_owner.expired());
    }
}
