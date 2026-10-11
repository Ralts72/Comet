#include "support/engine_fixture.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"

#include <thread>

namespace Comet::Tests {
    namespace {
        uint64_t allocation_count(const Device& device) {
            uint64_t count = 0;
            for(const auto& heap : device.query_memory_budget().heaps)
                count += heap.allocation_count;
            return count;
        }

        std::shared_ptr<Image> make_image(Device& device) {
            return Image::create(device, {.format = Format::R8G8B8A8_UNORM,
                                             .extent = {4, 4, 1},
                                             .usage = Flags<ImageUsage>(ImageUsage::Sampled)});
        }
    }

    using ResourceRetirementTest = EngineTest;

    TEST_F(ResourceRetirementTest, WorkerReleaseKeepsNativeResourcesUntilOwnerCollection) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const auto baseline = allocation_count(device);
        auto buffer =
            Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::Vertex), 64);
        auto mapped =
            Buffer::create_upload_buffer(device, Flags<BufferUsage>(BufferUsage::CopySrc), 64);
        auto image = make_image(device);
        auto view = ImageView::create(device, image, Flags<ImageAspect>(ImageAspect::Color));
        auto sampler = Sampler::create(device);
        ASSERT_TRUE(sampler) << sampler.error();
        const auto native_buffer = buffer->get();
        const auto native_mapped = mapped->get();
        const auto native_image = image->get();
        const std::weak_ptr<Buffer> released_buffer = buffer;
        const std::weak_ptr<ImageView> released_view = view;
        const std::weak_ptr<Image> retained_image = image;
        ASSERT_EQ(allocation_count(device), baseline + 3);
        std::thread worker(
            [buffer = std::move(buffer), mapped = std::move(mapped), view = std::move(view),
                sampler = std::move(sampler).value()]() mutable {
                buffer.reset();
                mapped.reset();
                view.reset();
                sampler.reset();
            });
        worker.join();
        image.reset();

        EXPECT_TRUE(released_buffer.expired());
        EXPECT_TRUE(released_view.expired());
        EXPECT_FALSE(retained_image.expired());
        EXPECT_EQ(allocation_count(device), baseline + 3);
        EXPECT_GE(device.get().getBufferMemoryRequirements(native_buffer).size, 64U);
        EXPECT_GE(device.get().getBufferMemoryRequirements(native_mapped).size, 64U);
        EXPECT_GT(device.get().getImageMemoryRequirements(native_image).size, 0U);
        device.collect_retired_resources();
        EXPECT_TRUE(retained_image.expired());
        EXPECT_EQ(allocation_count(device), baseline);
    }

    TEST_F(ResourceRetirementTest, NextFrameCollectsWorkerReleasesAndOwnerReleaseStaysImmediate) {
        auto& renderer = engine->get_renderer();
        auto& device = renderer.get_render_context().get_device();
        const auto baseline = allocation_count(device);
        auto buffer =
            Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::Vertex), 64);
        buffer.reset();
        EXPECT_EQ(allocation_count(device), baseline);

        auto image = make_image(device);
        std::thread worker([image = std::move(image)]() mutable { image.reset(); });
        worker.join();
        EXPECT_EQ(allocation_count(device), baseline + 1);
        auto prepared = renderer.prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        EXPECT_EQ(allocation_count(device), baseline);
        ASSERT_TRUE(renderer.render_frame());
    }

    TEST_F(ResourceRetirementTest, ShutdownDrainsReleasesWithoutAnotherFrame) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto image = make_image(device);
        auto view = ImageView::create(device, image, Flags<ImageAspect>(ImageAspect::Color));
        const std::weak_ptr<Image> retained_image = image;
        image.reset();
        std::thread worker([view = std::move(view)]() mutable { view.reset(); });
        worker.join();
        EXPECT_FALSE(retained_image.expired());
        engine.reset();
        EXPECT_TRUE(retained_image.expired());
    }

    TEST_F(ResourceRetirementTest, CollectionPreservesResourcesRetainedBySubmittedFrame) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const auto baseline = allocation_count(device);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        auto buffer =
            Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::Vertex), 64);
        const std::weak_ptr<Buffer> retained_buffer = buffer;
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        auto& command = frames.get_current_command_buffer();
        command.begin();
        command.get().fillBuffer(buffer->get(), 0, 64, 0x12345678);
        frames.retain_current_frame_resource(buffer);
        command.end();
        ASSERT_TRUE(frames.submit({}, {}));
        frames.end_frame();

        std::thread worker([buffer = std::move(buffer)]() mutable { buffer.reset(); });
        worker.join();
        device.collect_retired_resources();
        EXPECT_FALSE(retained_buffer.expired());
        EXPECT_EQ(allocation_count(device), baseline + 1);
        frames.wait_for_all_slots();
        EXPECT_TRUE(retained_buffer.expired());
        EXPECT_EQ(allocation_count(device), baseline);
    }
}
