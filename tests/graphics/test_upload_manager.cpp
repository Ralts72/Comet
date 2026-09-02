#include "graphics/command/upload_manager.h"

#include "graphics/command/command_context.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"

#include <concepts>
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
        concept SupportsRecoverableBufferUpload = requires(
            T& manager,
            std::shared_ptr<Buffer> destination,
            std::span<const std::byte> data,
            const ResourceState& after) {
            {
                manager.try_enqueue_upload(
                    destination, data, after, true)
            } -> std::same_as<GpuResourceResult<void>>;
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
        concept SupportsRecoverableImageUpload = requires(
            T& manager,
            std::shared_ptr<Image> destination,
            std::span<const std::byte> data,
            const ImageState& before,
            const ImageState& after) {
            {
                manager.try_enqueue_upload(
                    destination, data, before, after, true)
            } -> std::same_as<GpuResourceResult<void>>;
        };

        template<typename T>
        concept SupportsBatchAbort = requires(T& batch) {
            batch.abort();
        };

        template<typename T>
        concept SupportsCommandDiscard = requires(T& context) {
            context.discard();
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

        template<typename T>
        concept SupportsRawBufferCopy = requires(
            T& context,
            const vk::Buffer source,
            const vk::Buffer destination) {
            context.copy_buffer(source, destination, 4, 8, 12);
        };

        template<typename T>
        concept SupportsRawImageCopy = requires(
            T& context,
            const vk::Buffer source,
            const vk::Image destination) {
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

        template<typename T>
        concept SupportsRawImageTransition = requires(
            T& context,
            const vk::Image image,
            const ImageState& before,
            const ImageState& after) {
            context.transition_image_state(image, before, after);
        };

        template<typename T>
        concept SupportsRawBufferTransition = requires(
            T& context,
            const vk::Buffer buffer,
            const ResourceState& before,
            const ResourceState& after) {
            context.transition_buffer_state(buffer, before, after);
        };

        using GpuBufferFactory = decltype(&Buffer::create_gpu_buffer);
        using RecoverableGpuBufferFactory =
            decltype(&Buffer::try_create_gpu_buffer);
        using RecoverableUploadBufferFactory =
            decltype(&Buffer::try_create_upload_buffer);
        using UploadBufferFactory = decltype(&Buffer::create_upload_buffer);

        static_assert(ReturnsUploadBatch<UploadManager>);
        static_assert(!SupportsOwnedBufferUpload<UploadManager>);
        static_assert(!SupportsRecoverableBufferUpload<UploadManager>);
        static_assert(SupportsOwnedBufferUpload<UploadBatch>);
        static_assert(SupportsRecoverableBufferUpload<UploadBatch>);
        static_assert(!SupportsRecoverableImageUpload<UploadManager>);
        static_assert(SupportsOwnedImageUpload<UploadBatch>);
        static_assert(SupportsRecoverableImageUpload<UploadBatch>);
        static_assert(!SupportsBorrowedBufferUpload<UploadBatch>);
        static_assert(ReturnsGpuCompletion<UploadBatch>);
        static_assert(ReturnsGpuCompletion<CommandContext>);
        static_assert(SupportsBatchAbort<UploadBatch>);
        static_assert(!SupportsBatchAbort<UploadManager>);
        static_assert(SupportsCommandDiscard<CommandContext>);

        static_assert(!std::is_copy_constructible_v<UploadManager>);
        static_assert(!std::is_copy_assignable_v<UploadManager>);
        static_assert(!std::is_move_constructible_v<UploadManager>);
        static_assert(!std::is_move_assignable_v<UploadManager>);
        static_assert(!std::is_copy_constructible_v<UploadBatch>);
        static_assert(!std::is_copy_assignable_v<UploadBatch>);
        static_assert(!std::is_move_constructible_v<UploadBatch>);
        static_assert(!std::is_move_assignable_v<UploadBatch>);
        static_assert(!std::is_constructible_v<UploadBatch, UploadManager&>);

        static_assert(std::is_invocable_v<
            GpuBufferFactory,
            Device&,
            Flags<BufferUsage>,
            size_t,
            std::string_view>);
        static_assert(!std::is_invocable_v<
            GpuBufferFactory,
            Device&,
            Flags<BufferUsage>,
            size_t,
            const void*,
            std::string_view>);
        static_assert(std::is_invocable_r_v<
            GpuResourceResult<std::shared_ptr<Buffer>>,
            RecoverableGpuBufferFactory,
            Device&,
            Flags<BufferUsage>,
            size_t,
            bool,
            std::string_view>);
        static_assert(std::is_invocable_r_v<
            GpuResourceResult<std::shared_ptr<CPUBuffer>>,
            RecoverableUploadBufferFactory,
            Device&,
            Flags<BufferUsage>,
            size_t,
            bool,
            const void*,
            std::string_view>);
        static_assert(std::same_as<
            std::invoke_result_t<
                UploadBufferFactory,
                Device&,
                Flags<BufferUsage>,
                size_t,
                const void*,
                std::string_view>,
            std::shared_ptr<CPUBuffer>>);
        static_assert(!std::is_constructible_v<
            GPUBuffer,
            Device&,
            Flags<BufferUsage>,
            size_t,
            std::string_view>);
        static_assert(!std::is_constructible_v<
            CPUBuffer,
            Device&,
            Flags<BufferUsage>,
            size_t,
            const void*,
            AllocationUsage,
            std::string_view>);

        constexpr UploadManager::CreateInfo DEFAULT_CREATE_INFO;
        static_assert(
            DEFAULT_CREATE_INFO.staging_page_size == 4U * 1024U * 1024U);
        static_assert(DEFAULT_CREATE_INFO.max_cached_staging_pages == 4U);
        static_assert(
            DEFAULT_CREATE_INFO.memory_pressure_threshold_percent == 90U);
        static_assert(SupportsRangedBufferCopy<CommandContext>);
        static_assert(SupportsOffsetImageCopy<CommandContext>);
        static_assert(!SupportsRawBufferCopy<CommandContext>);
        static_assert(!SupportsRawImageCopy<CommandContext>);
        static_assert(!SupportsRawImageTransition<CommandContext>);
        static_assert(!SupportsRawImageTransition<CommandBuffer>);
        static_assert(!SupportsRawBufferTransition<CommandContext>);
        static_assert(!SupportsRawBufferTransition<CommandBuffer>);
    }
}
