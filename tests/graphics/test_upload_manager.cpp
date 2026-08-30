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
        concept SupportsBatchAbort = requires(T& manager) {
            manager.abort();
        };

        template<typename T>
        concept SupportsCommandDiscard = requires(T& context) {
            context.discard();
        };

        template<typename T>
        concept BeginsUploadBatch = requires(T& manager) {
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

        class UploadBatchGpuTest : public ::testing::Test {
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
                    *m_window,
                    Config::Vulkan{},
                    DeviceCapabilityRequest{});
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

    TEST(UploadManagerInterfaceTest, OwnsResourcesUntilBatchCompletion) {
        EXPECT_TRUE(BeginsUploadBatch<UploadManager>);
        EXPECT_FALSE(SupportsOwnedBufferUpload<UploadManager>);
        EXPECT_FALSE(SupportsRecoverableBufferUpload<UploadManager>);
        EXPECT_FALSE(SupportsOwnedImageUpload<UploadManager>);
        EXPECT_FALSE(SupportsRecoverableImageUpload<UploadManager>);
        EXPECT_TRUE(SupportsOwnedBufferUpload<UploadBatch>);
        EXPECT_TRUE(SupportsRecoverableBufferUpload<UploadBatch>);
        EXPECT_TRUE(SupportsOwnedImageUpload<UploadBatch>);
        EXPECT_TRUE(SupportsRecoverableImageUpload<UploadBatch>);
        EXPECT_FALSE(SupportsBorrowedBufferUpload<UploadBatch>);
        EXPECT_TRUE(ReturnsGpuCompletion<UploadBatch>);
        EXPECT_TRUE(ReturnsGpuCompletion<CommandContext>);
        EXPECT_TRUE(SupportsBatchAbort<UploadBatch>);
        EXPECT_FALSE(SupportsBatchAbort<UploadManager>);
        EXPECT_TRUE(SupportsCommandDiscard<CommandContext>);
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
        EXPECT_FALSE((std::is_constructible_v<UploadBatch, UploadManager&>));
    }

    TEST(UploadManagerInterfaceTest, SeparatesAllocationFromUploadData) {
        using GpuBufferFactory = decltype(&Buffer::create_gpu_buffer);
        using RecoverableGpuBufferFactory =
            decltype(&Buffer::try_create_gpu_buffer);
        using RecoverableUploadBufferFactory =
            decltype(&Buffer::try_create_upload_buffer);
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
        EXPECT_TRUE((std::is_invocable_r_v<
            GpuResourceResult<std::shared_ptr<Buffer>>,
            RecoverableGpuBufferFactory,
            Device&,
            Flags<BufferUsage>,
            size_t,
            bool,
            std::string_view>));
        EXPECT_TRUE((std::is_invocable_r_v<
            GpuResourceResult<std::shared_ptr<CPUBuffer>>,
            RecoverableUploadBufferFactory,
            Device&,
            Flags<BufferUsage>,
            size_t,
            bool,
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
        EXPECT_FALSE((std::is_constructible_v<
            GPUBuffer,
            Device&,
            Flags<BufferUsage>,
            size_t,
            std::string_view>));
        EXPECT_FALSE((std::is_constructible_v<
            CPUBuffer,
            Device&,
            Flags<BufferUsage>,
            size_t,
            const void*,
            AllocationUsage,
            std::string_view>));
    }

    TEST(UploadManagerInterfaceTest, SupportsStagingPageSuballocations) {
        const UploadManager::CreateInfo create_info;

        EXPECT_EQ(create_info.staging_page_size, 4U * 1024U * 1024U);
        EXPECT_EQ(create_info.max_cached_staging_pages, 4U);
        EXPECT_EQ(create_info.memory_pressure_threshold_percent, 90U);
        EXPECT_TRUE(SupportsRangedBufferCopy<CommandContext>);
        EXPECT_TRUE(SupportsOffsetImageCopy<CommandContext>);
    }

    TEST_F(UploadBatchGpuTest, StagingFailureOnlyAbortsOwningBatch) {
        ASSERT_NE(m_device, nullptr);

        size_t growth_attempts = 0;
        UploadManager manager(*m_device, {
            .staging_page_size = 4,
            .max_cached_staging_pages = 4,
            .memory_pressure_threshold_percent = 90,
            .staging_growth_guard = [&growth_attempts](
                const size_t,
                const bool) -> std::optional<vk::Result> {
                ++growth_attempts;
                if(growth_attempts == 3) {
                    return vk::Result::eErrorOutOfDeviceMemory;
                }
                return std::nullopt;
            }
        });
        const auto after = resolve_resource_state(
            ResourceUsage::VertexBuffer);
        ASSERT_TRUE(after.has_value());

        auto batch_b_buffer = Buffer::create_gpu_buffer(
            *m_device,
            Flags<BufferUsage>(BufferUsage::Vertex),
            4,
            "upload batch B buffer");
        auto batch_a_first_buffer = Buffer::create_gpu_buffer(
            *m_device,
            Flags<BufferUsage>(BufferUsage::Vertex),
            4,
            "upload batch A first buffer");
        auto batch_a_second_buffer = Buffer::create_gpu_buffer(
            *m_device,
            Flags<BufferUsage>(BufferUsage::Vertex),
            4,
            "upload batch A second buffer");
        const std::array<std::byte, 4> data{};

        auto batch_b = manager.begin_batch();
        ASSERT_TRUE(batch_b.try_enqueue_upload(
            batch_b_buffer, data, *after, true));

        auto batch_a = manager.begin_batch();
        ASSERT_TRUE(batch_a.try_enqueue_upload(
            batch_a_first_buffer, data, *after, true));
        const auto rejected = batch_a.try_enqueue_upload(
            batch_a_second_buffer, data, *after, true);

        EXPECT_FALSE(static_cast<bool>(rejected));
        EXPECT_EQ(rejected.result(), vk::Result::eErrorOutOfDeviceMemory);
        EXPECT_EQ(growth_attempts, 3U);

        const GpuCompletionPoint completion = batch_b.submit();
        ASSERT_TRUE(completion.is_valid());
        EXPECT_TRUE(completion.wait());
        manager.collect_completed();
    }
}
