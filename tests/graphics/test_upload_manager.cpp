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

        template<typename T>
        concept SupportsRangedBufferCopy = requires(
            T& context,
            const Buffer& source,
            const Buffer& destination) {
            context.copy_buffer(source, destination, 4, 8, 12);
        };

        template<typename T>
        concept SupportsOffsetImageCopy = requires(
            T& context,
            const Buffer& source,
            const Image& destination) {
            context.copy_buffer_to_image(
                source,
                destination,
                ImageLayout::TransferDstOptimal,
                vk::Extent3D{1, 1, 1},
                0,
                1,
                0,
                16);
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
        using UploadBufferFactory = decltype(&Buffer::create_upload_buffer);

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
        EXPECT_TRUE((std::same_as<
            std::invoke_result_t<
                UploadBufferFactory,
                Device&,
                Flags<BufferUsage>,
                size_t,
                const void*,
                std::string_view>,
            std::shared_ptr<CPUBuffer>>));
    }

    TEST(UploadManagerInterfaceTest, SupportsStagingPageSuballocations) {
        constexpr UploadManager::CreateInfo create_info;

        EXPECT_EQ(create_info.staging_page_size, 4U * 1024U * 1024U);
        EXPECT_EQ(create_info.max_cached_staging_pages, 4U);
        EXPECT_EQ(create_info.memory_pressure_threshold_percent, 90U);
        EXPECT_TRUE(SupportsRangedBufferCopy<CommandContext>);
        EXPECT_TRUE(SupportsOffsetImageCopy<CommandContext>);
    }
}
