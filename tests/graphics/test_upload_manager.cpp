#include "graphics/command/upload_manager.h"

#include "graphics/command/command_context.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"

#include <concepts>
#include <gtest/gtest.h>
#include <type_traits>

namespace Comet::Tests {
    namespace {
        template<typename T>
        concept SupportsOwnedBufferUpload = requires(
            T& manager,
            std::shared_ptr<Buffer> destination,
            std::span<const std::byte> data,
            const ResourceState& after) {
            manager.enqueue_upload(destination, data, after);
        };

        template<typename T>
        concept SupportsBorrowedBufferUpload = requires(
            T& manager,
            Buffer* destination,
            std::span<const std::byte> data,
            const ResourceState& after) {
            manager.enqueue_upload(destination, data, after);
        };

        template<typename T>
        concept SupportsOwnedImageUpload = requires(
            T& manager,
            std::shared_ptr<Image> destination,
            std::span<const std::byte> data,
            const ImageState& before,
            const ImageState& after) {
            manager.enqueue_upload(destination, data, before, after);
        };

        template<typename T>
        concept ReturnsOptionalCompletion = requires(T& manager) {
            {
                manager.flush_batch()
            } -> std::same_as<std::optional<GpuCompletionPoint>>;
        };

        template<typename T>
        concept ReturnsGpuCompletion = requires(T& context) {
            {
                context.submit()
            } -> std::same_as<GpuCompletionPoint>;
        };
    }

    TEST(UploadManagerInterfaceTest, OwnsResourcesUntilBatchCompletion) {
        EXPECT_TRUE(SupportsOwnedBufferUpload<UploadManager>);
        EXPECT_TRUE(SupportsOwnedImageUpload<UploadManager>);
        EXPECT_FALSE(SupportsBorrowedBufferUpload<UploadManager>);
        EXPECT_TRUE(ReturnsOptionalCompletion<UploadManager>);
        EXPECT_TRUE(ReturnsGpuCompletion<CommandContext>);
    }

    TEST(UploadManagerInterfaceTest, HasSingleOwnerThreadSemantics) {
        EXPECT_FALSE(std::is_copy_constructible_v<UploadManager>);
        EXPECT_FALSE(std::is_copy_assignable_v<UploadManager>);
        EXPECT_FALSE(std::is_move_constructible_v<UploadManager>);
        EXPECT_FALSE(std::is_move_assignable_v<UploadManager>);
    }

    TEST(UploadManagerInterfaceTest, SeparatesAllocationFromUploadData) {
        using GpuBufferFactory = decltype(&Buffer::create_gpu_buffer);

        EXPECT_TRUE((std::is_invocable_v<
            GpuBufferFactory,
            Device&,
            Flags<BufferUsage>,
            size_t,
            std::string_view>));
        EXPECT_FALSE((std::is_invocable_v<
            GpuBufferFactory,
            Device&,
            Flags<BufferUsage>,
            size_t,
            const void*,
            std::string_view>));
    }
}
