#include <gtest/gtest.h>
#include <type_traits>

#include "graphics/buffer.h"
#include "graphics/command_buffer.h"
#include "graphics/context.h"
#include "graphics/descriptor_set.h"
#include "graphics/device.h"
#include "graphics/fence.h"
#include "graphics/frame_buffer.h"
#include "graphics/image.h"
#include "graphics/image_view.h"
#include "graphics/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/sampler.h"
#include "graphics/semaphore.h"
#include "graphics/shader.h"
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
    expect_noncopyable_immovable_owner<Swapchain>();
    expect_noncopyable_immovable_owner<Device>();
    expect_noncopyable_immovable_owner<Context>();
}

TEST(VulkanRaiiOwnershipTest, ExplicitlyMovableSyncWrappersDoNotCopy) {
    expect_noncopyable_movable_owner<Fence>();
    expect_noncopyable_movable_owner<Semaphore>();
}
