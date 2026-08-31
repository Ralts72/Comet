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
        concept ReturnsUploadBatch = requires(T& manager) {
            {
                manager.begin_batch()
            } -> std::same_as<UploadBatch>;
        };

        template<typename T>
        concept ReturnsGpuCompletion = requires(T& context) {
            {
                context.submit()
            } -> std::same_as<GpuCompletionPoint>;
        };
    }

    TEST(UploadManagerInterfaceTest, UsesExplicitOwnedBatches) {
        EXPECT_TRUE(ReturnsUploadBatch<UploadManager>);
        EXPECT_FALSE(SupportsOwnedBufferUpload<UploadManager>);
        EXPECT_TRUE(SupportsOwnedBufferUpload<UploadBatch>);
        EXPECT_TRUE(SupportsOwnedImageUpload<UploadBatch>);
        EXPECT_FALSE(SupportsBorrowedBufferUpload<UploadBatch>);
        EXPECT_TRUE(ReturnsGpuCompletion<UploadBatch>);
        EXPECT_TRUE(ReturnsGpuCompletion<CommandContext>);
    }

    TEST(UploadManagerInterfaceTest, HasSingleOwnerThreadSemantics) {
        EXPECT_FALSE(std::is_copy_constructible_v<UploadManager>);
        EXPECT_FALSE(std::is_copy_assignable_v<UploadManager>);
        EXPECT_FALSE(std::is_move_constructible_v<UploadManager>);
        EXPECT_FALSE(std::is_move_assignable_v<UploadManager>);
        EXPECT_FALSE(std::is_copy_constructible_v<UploadBatch>);
        EXPECT_FALSE(std::is_copy_assignable_v<UploadBatch>);
        EXPECT_FALSE(std::is_move_constructible_v<UploadBatch>);
        EXPECT_FALSE(std::is_move_assignable_v<UploadBatch>);
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
