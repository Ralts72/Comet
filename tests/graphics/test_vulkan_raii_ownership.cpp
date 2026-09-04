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
    constexpr bool NONCOPYABLE_IMMOVABLE_OWNER =
        !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>
        && !std::is_move_constructible_v<T> && !std::is_move_assignable_v<T>;

    template<typename T>
    constexpr bool NONCOPYABLE_MOVABLE_OWNER =
        !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>
        && std::is_move_constructible_v<T> && std::is_move_assignable_v<T>;

    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Buffer>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<GPUBuffer>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<CPUBuffer>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Image>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<OwnedImage>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<BorrowedImage>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<ImageView>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Sampler>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Shader>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<DescriptorSetLayout>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<DescriptorPool>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<PipelineLayout>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Pipeline>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<FrameBuffer>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<RenderPass>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<CommandPool>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<CommandContext>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<UploadManager>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Swapchain>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Allocator>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Device>);
    static_assert(NONCOPYABLE_IMMOVABLE_OWNER<Context>);

    static_assert(NONCOPYABLE_MOVABLE_OWNER<Fence>);
    static_assert(NONCOPYABLE_MOVABLE_OWNER<Semaphore>);
    static_assert(!std::is_copy_constructible_v<Queue>);
    static_assert(!std::is_copy_assignable_v<Queue>);
    static_assert(std::is_move_constructible_v<Queue>);
    static_assert(!std::is_move_assignable_v<Queue>);
}
