#include <gtest/gtest.h>
#include <type_traits>

#include "graphics/resource/buffer.h"
#include "graphics/resource/allocator.h"
#include "graphics/command/command_buffer.h"
#include "graphics/command/command_context.h"
#include "graphics/command/upload_manager.h"
#include "graphics/context.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/device.h"
#include "graphics/synchronization/fence.h"
#include "graphics/synchronization/gpu_retirement_queue.h"
#include "graphics/frame_buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/resource/sampler.h"
#include "graphics/synchronization/semaphore.h"
#include "graphics/pipeline/shader.h"
#include "graphics/queue.h"
#include "graphics/swapchain.h"

using namespace Comet;

namespace {
    template<typename T>
    void expect_noncopyable_immovable_owner() {
        EXPECT_FALSE(std::is_copy_constructible_v<T>);
        EXPECT_FALSE(std::is_copy_assignable_v<T>);
        EXPECT_FALSE(std::is_move_constructible_v<T>);
        EXPECT_FALSE(std::is_move_assignable_v<T>);
    }

    template<typename T>
    void expect_noncopyable_movable_owner() {
        EXPECT_FALSE(std::is_copy_constructible_v<T>);
        EXPECT_FALSE(std::is_copy_assignable_v<T>);
        EXPECT_TRUE(std::is_move_constructible_v<T>);
        EXPECT_TRUE(std::is_move_assignable_v<T>);
    }
}

TEST(VulkanRaiiOwnershipTest, OwningWrappersDoNotCopyOrMoveByDefault) {
    expect_noncopyable_immovable_owner<Buffer>();
    expect_noncopyable_immovable_owner<GPUBuffer>();
    expect_noncopyable_immovable_owner<CPUBuffer>();
    expect_noncopyable_immovable_owner<Image>();
    expect_noncopyable_immovable_owner<OwnedImage>();
    expect_noncopyable_immovable_owner<BorrowedImage>();
    expect_noncopyable_immovable_owner<ImageView>();
    expect_noncopyable_immovable_owner<Sampler>();
    expect_noncopyable_immovable_owner<Shader>();
    expect_noncopyable_immovable_owner<DescriptorSetLayout>();
    expect_noncopyable_immovable_owner<DescriptorPool>();
    expect_noncopyable_immovable_owner<PipelineLayout>();
    expect_noncopyable_immovable_owner<Pipeline>();
    expect_noncopyable_immovable_owner<FrameBuffer>();
    expect_noncopyable_immovable_owner<RenderPass>();
    expect_noncopyable_immovable_owner<CommandPool>();
    expect_noncopyable_immovable_owner<CommandContext>();
    expect_noncopyable_immovable_owner<UploadManager>();
    expect_noncopyable_immovable_owner<GpuRetirementQueue>();
    expect_noncopyable_immovable_owner<SwapchainGeneration>();
    expect_noncopyable_immovable_owner<Swapchain>();
    expect_noncopyable_immovable_owner<Allocator>();
    expect_noncopyable_immovable_owner<Device>();
    expect_noncopyable_immovable_owner<Context>();
}

TEST(VulkanRaiiOwnershipTest, ExplicitlyMovableSyncWrappersDoNotCopy) {
    expect_noncopyable_movable_owner<Fence>();
    expect_noncopyable_movable_owner<Semaphore>();
    EXPECT_FALSE(std::is_copy_constructible_v<Queue>);
    EXPECT_FALSE(std::is_copy_assignable_v<Queue>);
    EXPECT_TRUE(std::is_move_constructible_v<Queue>);
    EXPECT_FALSE(std::is_move_assignable_v<Queue>);
}

TEST(VulkanRaiiOwnershipTest, QueueExposesScopedIdleFallback) {
    EXPECT_TRUE((std::is_invocable_r_v<void, decltype(&Queue::wait_idle), const Queue&>));
}

TEST(VulkanRaiiOwnershipTest, SwapchainPublishesSharedGenerationOwnership) {
    EXPECT_TRUE((std::is_same_v<
        decltype(std::declval<const Swapchain&>().get_active_generation()),
        const std::shared_ptr<SwapchainGeneration>&>));
}
