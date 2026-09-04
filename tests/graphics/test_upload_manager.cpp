#include "graphics/command/upload_manager.h"

#include "config/config.h"
#include "core/window.h"
#include "graphics/command/command_context.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"

#include <array>
#include <concepts>
#include <gtest/gtest.h>
#include <optional>
#include <type_traits>

namespace Comet::Tests {
    namespace {
        template<typename T>
        concept SupportsOwnedBufferUpload =
            requires(T& manager, std::shared_ptr<Buffer> destination,
                std::span<const std::byte> data, const ResourceState& after) {
                manager.enqueue_upload(destination, data, after);
            };

        template<typename T>
        concept SupportsRecoverableBufferUpload =
            requires(T& manager, std::shared_ptr<Buffer> destination,
                std::span<const std::byte> data, const ResourceState& after) {
                {
                    manager.try_enqueue_upload(destination, data, after, true)
                } -> std::same_as<GpuResourceResult<void>>;
            };

        template<typename T>
        concept SupportsBorrowedBufferUpload = requires(T& manager, Buffer* destination,
            std::span<const std::byte> data, const ResourceState& after) {
            manager.enqueue_upload(destination, data, after);
        };

        template<typename T>
        concept SupportsOwnedImageUpload = requires(T& manager,
            std::shared_ptr<Image> destination, std::span<const std::byte> data,
            const ImageState& before, const ImageState& after) {
            manager.enqueue_upload(destination, data, before, after);
        };

        template<typename T>
        concept SupportsRecoverableImageUpload = requires(T& manager,
            std::shared_ptr<Image> destination, std::span<const std::byte> data,
            const ImageState& before, const ImageState& after) {
            {
                manager.try_enqueue_upload(destination, data, before, after, true)
            } -> std::same_as<GpuResourceResult<void>>;
        };

        template<typename T>
        concept SupportsBatchAbort = requires(T& batch) { batch.abort(); };

        template<typename T>
        concept SupportsCommandDiscard = requires(T& context) { context.discard(); };

        template<typename T>
        concept ReturnsUploadBatch = requires(T& manager) {
            { manager.begin_batch() } -> std::same_as<UploadBatch>;
        };

        template<typename T>
        concept ReturnsGpuCompletion = requires(T& context) {
            { context.submit() } -> std::same_as<GpuCompletionPoint>;
        };

        template<typename T>
        concept SupportsRangedBufferCopy =
            requires(T& context, const Buffer& source, const Buffer& destination) {
                context.copy_buffer(source, destination, 4, 8, 12);
            };

        template<typename T>
        concept SupportsOffsetImageCopy =
            requires(T& context, const Buffer& source, const Image& destination) {
                context.copy_buffer_to_image(source, destination,
                    ImageLayout::TransferDstOptimal, vk::Extent3D{1, 1, 1}, 0, 1, 0, 16);
            };

        template<typename T>
        concept SupportsRawBufferCopy =
            requires(T& context, const vk::Buffer source, const vk::Buffer destination) {
                context.copy_buffer(source, destination, 4, 8, 12);
            };

        template<typename T>
        concept SupportsRawImageCopy =
            requires(T& context, const vk::Buffer source, const vk::Image destination) {
                context.copy_buffer_to_image(source, destination,
                    ImageLayout::TransferDstOptimal, vk::Extent3D{1, 1, 1}, 0, 1, 0, 16);
            };

        template<typename T>
        concept SupportsRawImageTransition = requires(T& context, const vk::Image image,
            const ImageState& before, const ImageState& after) {
            context.transition_image_state(image, before, after);
        };

        template<typename T>
        concept SupportsRawBufferTransition =
            requires(T& context, const vk::Buffer buffer, const ResourceState& before,
                const ResourceState& after) {
                context.transition_buffer_state(buffer, before, after);
            };

        using GpuBufferFactory = decltype(&Buffer::create_gpu_buffer);
        using RecoverableGpuBufferFactory = decltype(&Buffer::try_create_gpu_buffer);
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

        static_assert(std::is_invocable_v<GpuBufferFactory, Device&, Flags<BufferUsage>,
            size_t, std::string_view>);
        static_assert(!std::is_invocable_v<GpuBufferFactory, Device&, Flags<BufferUsage>,
            size_t, const void*, std::string_view>);
        static_assert(std::is_invocable_r_v<GpuResourceResult<std::shared_ptr<Buffer>>,
            RecoverableGpuBufferFactory, Device&, Flags<BufferUsage>, size_t, bool,
            std::string_view>);
        static_assert(std::is_invocable_r_v<GpuResourceResult<std::shared_ptr<CPUBuffer>>,
            RecoverableUploadBufferFactory, Device&, Flags<BufferUsage>, size_t, bool,
            const void*, std::string_view>);
        static_assert(
            std::same_as<std::invoke_result_t<UploadBufferFactory, Device&,
                             Flags<BufferUsage>, size_t, const void*, std::string_view>,
                std::shared_ptr<CPUBuffer>>);
        static_assert(!std::is_constructible_v<GPUBuffer, Device&, Flags<BufferUsage>,
            size_t, std::string_view>);
        static_assert(!std::is_constructible_v<CPUBuffer, Device&, Flags<BufferUsage>,
            size_t, const void*, AllocationUsage, std::string_view>);

        static_assert(SupportsRangedBufferCopy<CommandContext>);
        static_assert(SupportsOffsetImageCopy<CommandContext>);
        static_assert(!SupportsRawBufferCopy<CommandContext>);
        static_assert(!SupportsRawImageCopy<CommandContext>);
        static_assert(!SupportsRawImageTransition<CommandContext>);
        static_assert(!SupportsRawImageTransition<CommandBuffer>);
        static_assert(!SupportsRawBufferTransition<CommandContext>);
        static_assert(!SupportsRawBufferTransition<CommandBuffer>);

        class UploadBatchGpuTest: public ::testing::Test {
        protected:
            void SetUp() override {
                if(glfwInit() != GLFW_TRUE) {
                    GTEST_SKIP() << "GLFW initialization failed";
                }
                m_glfw_initialized = true;
                if(glfwVulkanSupported() != GLFW_TRUE) {
                    glfwTerminate();
                    m_glfw_initialized = false;
                    GTEST_SKIP() << "Vulkan is unavailable through GLFW";
                }

                Config::Window window_config;
                window_config.width = 64;
                window_config.height = 64;
                window_config.title = "Comet UploadBatch Test";
                window_config.resizable = false;
                m_window = std::make_unique<Window>(window_config);
                m_context = std::make_unique<Context>(
                    *m_window, Config::Vulkan{}, DeviceCapabilityRequest{});
                m_device = std::make_unique<Device>(*m_context);
            }

            void TearDown() override {
                m_device.reset();
                m_context.reset();
                if(m_window) {
                    m_window.reset();
                    m_glfw_initialized = false;
                }
                if(m_glfw_initialized) {
                    glfwTerminate();
                }
            }

            std::unique_ptr<Window> m_window;
            std::unique_ptr<Context> m_context;
            std::unique_ptr<Device> m_device;
            bool m_glfw_initialized = false;
        };
    }

    TEST(UploadManagerInterfaceTest, HasExpectedStagingDefaults) {
        const UploadManager::CreateInfo create_info;

        EXPECT_EQ(create_info.staging_page_size, 4U * 1024U * 1024U);
        EXPECT_EQ(create_info.max_cached_staging_pages, 4U);
        EXPECT_EQ(create_info.memory_pressure_threshold_percent, 90U);
        EXPECT_FALSE(static_cast<bool>(create_info.staging_growth_guard));
    }

    TEST_F(UploadBatchGpuTest, StagingFailureOnlyAbortsOwningBatch) {
        ASSERT_NE(m_device, nullptr);

        size_t growth_attempts = 0;
        UploadManager manager(*m_device,
            {.staging_page_size = 4,
                .max_cached_staging_pages = 4,
                .memory_pressure_threshold_percent = 90,
                .staging_growth_guard = [&growth_attempts](const size_t,
                                            const bool) -> std::optional<vk::Result> {
                    ++growth_attempts;
                    if(growth_attempts == 3) {
                        return vk::Result::eErrorOutOfDeviceMemory;
                    }
                    return std::nullopt;
                }});
        const auto after = resolve_resource_state(ResourceUsage::VertexBuffer);
        ASSERT_TRUE(after.has_value());

        auto batch_b_buffer = Buffer::create_gpu_buffer(*m_device,
            Flags<BufferUsage>(BufferUsage::Vertex), 4, "upload batch B buffer");
        auto batch_a_first_buffer = Buffer::create_gpu_buffer(*m_device,
            Flags<BufferUsage>(BufferUsage::Vertex), 4, "upload batch A first buffer");
        auto batch_a_second_buffer = Buffer::create_gpu_buffer(*m_device,
            Flags<BufferUsage>(BufferUsage::Vertex), 4, "upload batch A second buffer");
        const std::array<std::byte, 4> data{};

        auto batch_b = manager.begin_batch();
        ASSERT_TRUE(batch_b.try_enqueue_upload(batch_b_buffer, data, *after, true));

        auto batch_a = manager.begin_batch();
        ASSERT_TRUE(batch_a.try_enqueue_upload(batch_a_first_buffer, data, *after, true));
        const auto rejected =
            batch_a.try_enqueue_upload(batch_a_second_buffer, data, *after, true);

        EXPECT_FALSE(static_cast<bool>(rejected));
        EXPECT_EQ(rejected.result(), vk::Result::eErrorOutOfDeviceMemory);
        EXPECT_EQ(growth_attempts, 3U);

        const GpuCompletionPoint completion = batch_b.submit();
        ASSERT_TRUE(completion.is_valid());
        completion.wait();
        EXPECT_TRUE(completion.is_complete());
        manager.collect_completed();
    }
}
