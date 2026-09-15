#include <gtest/gtest.h>

#include "graphics/swapchain.h"
#include "graphics/vk_capability.h"
#include "graphics/device.h"
#include "graphics/context.h"
#include "render/scene/scene_renderer.h"
#include "support/engine_fixture.h"

#include <limits>
#include <chrono>
#include <thread>
#include <type_traits>

namespace Comet::Tests {
    static_assert(!std::is_constructible_v<Swapchain, const Window&, Context&, Device&,
        const SwapchainRequest&>);
    static_assert(noexcept(std::declval<Device&>().wait_idle_for_shutdown()));
    using SwapchainLifecycleTest = EngineTest;

    TEST_F(SwapchainLifecycleTest, InvalidCreationReturnsErrorWithoutChangingActivePresentation) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto generation = context.get_swapchain().get_active_generation();
        SwapchainRequest request;
        request.usage = Flags<ImageUsage>(ImageUsage::Sampled);
        auto rejected = Swapchain::create(
            engine->get_window(), context.get_context(), context.get_device(), request);
        ASSERT_FALSE(rejected);
        EXPECT_FALSE(rejected.error().result.has_value());
        EXPECT_EQ(context.get_swapchain().get_active_generation(), generation);
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_TRUE(preparation.value());
        }
        EXPECT_TRUE(renderer.render_frame({}));
    }

    TEST_F(SwapchainLifecycleTest, RebuildInstallsNewGenerationAndContinuesRendering) {
        auto& renderer = engine->get_renderer();
        auto& swapchain = renderer.get_render_context().get_swapchain();
        auto previous = swapchain.get_active_generation();
        renderer.request_swapchain_recreation();
        EXPECT_EQ(swapchain.get_active_generation(), previous);
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_TRUE(preparation.value());
        }
        EXPECT_NE(swapchain.get_active_generation(), previous);
        previous.reset();
        EXPECT_TRUE(renderer.render_frame({}));
        renderer.request_swapchain_recreation();
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_TRUE(preparation.value());
        }
        EXPECT_TRUE(renderer.render_frame({}));
    }

    TEST_F(SwapchainLifecycleTest, CoalescesRequestsUntilNextFramePreparation) {
        auto& renderer = engine->get_renderer();
        auto& swapchain = renderer.get_render_context().get_swapchain();
        const auto previous = swapchain.get_active_generation();
        auto preparation = renderer.prepare_frame();
        ASSERT_TRUE(preparation);
        ASSERT_TRUE(preparation.value());
        renderer.request_swapchain_recreation();
        renderer.request_swapchain_recreation();
        EXPECT_EQ(swapchain.get_active_generation(), previous);
        EXPECT_TRUE(renderer.render_frame({}));
        EXPECT_EQ(swapchain.get_active_generation(), previous);
        preparation = renderer.prepare_frame();
        ASSERT_TRUE(preparation);
        ASSERT_TRUE(preparation.value());
        EXPECT_NE(swapchain.get_active_generation(), previous);
        EXPECT_TRUE(renderer.render_frame({}));
    }

    TEST_F(SwapchainLifecycleTest, RetriesDependentWithoutReplacingSuccessfulGeneration) {
        auto& renderer = engine->get_renderer();
        auto& swapchain = renderer.get_render_context().get_swapchain();
        auto previous = swapchain.get_active_generation();
        int rebuilds = 0;
        renderer.set_swapchain_resource_callbacks([] {},
            [&](const SwapchainCompatibility&) {
                if(++rebuilds == 1)
                    return Result<void, GraphicsError>::failure(
                        {"temporary overlay allocation failure",
                            vk::Result::eErrorOutOfDeviceMemory});
                return Result<void, GraphicsError>::success();
            });
        renderer.request_swapchain_recreation();
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            EXPECT_FALSE(preparation.value());
        }
        auto candidate = swapchain.get_active_generation();
        ASSERT_NE(candidate, previous);
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            EXPECT_FALSE(preparation.value());
        }
        EXPECT_EQ(rebuilds, 1);
        EXPECT_FALSE(renderer.get_frame_scheduler().is_frame_active());
        renderer.request_swapchain_recreation();
        EXPECT_EQ(swapchain.get_active_generation(), candidate);
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_TRUE(preparation.value());
        }
        EXPECT_TRUE(renderer.render_frame({}));
        renderer.set_swapchain_resource_callbacks({}, {});
    }

    TEST_F(SwapchainLifecycleTest, AutomaticallyResumesAfterTemporaryDependentFailure) {
        auto& renderer = engine->get_renderer();
        int rebuilds = 0;
        renderer.set_swapchain_resource_callbacks([] {},
            [&](const SwapchainCompatibility&) {
                if(++rebuilds == 1)
                    return Result<void, GraphicsError>::failure(
                        {"temporary allocation failure", vk::Result::eErrorOutOfHostMemory});
                return Result<void, GraphicsError>::success();
            });
        renderer.request_swapchain_recreation();
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_FALSE(preparation.value());
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool resumed = false;
        while(std::chrono::steady_clock::now() < deadline) {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            if(preparation.value()) {
                resumed = true;
                EXPECT_TRUE(renderer.render_frame({}));
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        renderer.set_swapchain_resource_callbacks({}, {});
        EXPECT_TRUE(resumed);
        EXPECT_EQ(rebuilds, 2);
    }

    TEST_F(SwapchainLifecycleTest, DoesNotRetryDeviceLoss) {
        auto& renderer = engine->get_renderer();
        renderer.set_swapchain_resource_callbacks([] {},
            [](const SwapchainCompatibility&) {
                return Result<void, GraphicsError>::failure(
                    {"device lost", vk::Result::eErrorDeviceLost});
            });
        renderer.request_swapchain_recreation();
        const auto preparation = renderer.prepare_frame();
        ASSERT_FALSE(preparation);
        EXPECT_EQ(preparation.error().result, vk::Result::eErrorDeviceLost);
        renderer.set_swapchain_resource_callbacks({}, {});
    }

    TEST_F(SwapchainLifecycleTest, SurfaceLossReplacesSurfaceAndResumesPresentation) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context().get_context();
        auto surface = context.get_surface();
        auto generation = renderer.get_render_context().get_swapchain().get_active_generation();
        int rebuilds = 0;
        renderer.set_swapchain_resource_callbacks([] {},
            [&](const SwapchainCompatibility&) {
                if(++rebuilds == 1)
                    return Result<void, GraphicsError>::failure(
                        {"surface lost during rebuild", vk::Result::eErrorSurfaceLostKHR});
                return Result<void, GraphicsError>::success();
            });
        renderer.request_swapchain_recreation();
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            EXPECT_FALSE(preparation.value());
        }
        renderer.request_swapchain_recreation();
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_TRUE(preparation.value());
        }
        EXPECT_NE(context.get_surface(), surface);
        EXPECT_TRUE(
            context.get_physical_device().getSurfaceCapabilitiesKHR(surface).maxImageArrayLayers
            > 0);
        EXPECT_TRUE(renderer.render_frame({}));
        renderer.set_swapchain_resource_callbacks({}, {});
    }

    namespace {
        vk::SurfaceCapabilitiesKHR make_capabilities() {
            vk::SurfaceCapabilitiesKHR capabilities{};
            capabilities.minImageCount = 2;
            capabilities.maxImageCount = 4;
            capabilities.currentExtent = vk::Extent2D{800, 600};
            capabilities.minImageExtent = vk::Extent2D{64, 64};
            capabilities.maxImageExtent = vk::Extent2D{1920, 1080};
            capabilities.maxImageArrayLayers = 1;
            capabilities.supportedTransforms = vk::SurfaceTransformFlagBitsKHR::eIdentity
                                               | vk::SurfaceTransformFlagBitsKHR::eRotate90;
            capabilities.currentTransform = vk::SurfaceTransformFlagBitsKHR::eRotate90;
            capabilities.supportedCompositeAlpha =
                vk::CompositeAlphaFlagBitsKHR::eOpaque | vk::CompositeAlphaFlagBitsKHR::eInherit;
            capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eColorAttachment;
            return capabilities;
        }

        SwapchainRequest make_request() {
            return {.image_count = 3,
                .surface_format = Format::B8G8R8A8_SRGB,
                .color_space = ImageColorSpace::SrgbNonlinearKHR,
                .present_mode = PresentMode::Mailbox,
                .usage = Flags<ImageUsage>(ImageUsage::ColorAttachment)};
        }

        std::vector<vk::SurfaceFormatKHR> make_surface_formats() {
            return {{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear}};
        }

        std::vector<vk::PresentModeKHR> make_present_modes() {
            return {vk::PresentModeKHR::eFifo, vk::PresentModeKHR::eMailbox};
        }
    }

    TEST(SwapchainConfigTest, UsesFixedExtentAndSurfaceTransform) {
        const auto result = select_swapchain(make_capabilities(), make_surface_formats(),
            make_present_modes(), vk::Extent2D{320, 240}, make_request());

        ASSERT_EQ(result.status, SwapchainStatus::Ready);
        EXPECT_EQ(result.config.image_count, 3u);
        EXPECT_EQ(result.config.extent, (vk::Extent2D{800, 600}));
        EXPECT_EQ(result.config.transform, vk::SurfaceTransformFlagBitsKHR::eRotate90);
        EXPECT_EQ(result.config.composite_alpha, vk::CompositeAlphaFlagBitsKHR::eOpaque);
        EXPECT_EQ(result.config.present_mode, vk::PresentModeKHR::eMailbox);
        EXPECT_TRUE(result.config.clipped);
    }

    TEST(SwapchainConfigTest, ClampsVariableExtentWithoutImageCountUpperLimit) {
        auto capabilities = make_capabilities();
        capabilities.maxImageCount = 0;
        capabilities.currentExtent = vk::Extent2D{
            std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
        auto request = make_request();
        request.image_count = 5;

        const auto result = select_swapchain(capabilities, make_surface_formats(),
            make_present_modes(), vk::Extent2D{4000, 10}, request);

        ASSERT_EQ(result.status, SwapchainStatus::Ready);
        EXPECT_EQ(result.config.image_count, 5u);
        EXPECT_EQ(result.config.extent, (vk::Extent2D{1920, 64}));
    }

    TEST(SwapchainConfigTest, DefersZeroSizedFramebuffer) {
        const auto result = select_swapchain(make_capabilities(), make_surface_formats(),
            make_present_modes(), vk::Extent2D{0, 0}, make_request());

        EXPECT_EQ(result.status, SwapchainStatus::Deferred);
        EXPECT_FALSE(result.message.empty());
    }

    TEST(SwapchainConfigTest, FallsBackToFifoAndSupportedCompositeAlpha) {
        auto capabilities = make_capabilities();
        capabilities.supportedCompositeAlpha =
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied | vk::CompositeAlphaFlagBitsKHR::eInherit;
        auto request = make_request();
        request.present_mode = PresentMode::Immediate;

        const auto result = select_swapchain(capabilities, make_surface_formats(),
            make_present_modes(), vk::Extent2D{800, 600}, request);

        ASSERT_EQ(result.status, SwapchainStatus::Ready);
        EXPECT_EQ(result.config.present_mode, vk::PresentModeKHR::eFifo);
        EXPECT_EQ(result.config.composite_alpha, vk::CompositeAlphaFlagBitsKHR::ePreMultiplied);
        EXPECT_FALSE(result.message.empty());
    }

    TEST(SwapchainConfigTest, RejectsUnsupportedRequiredImageUsage) {
        auto request = make_request();
        request.usage |= ImageUsage::CopySrc;

        const auto result = select_swapchain(make_capabilities(), make_surface_formats(),
            make_present_modes(), vk::Extent2D{800, 600}, request);

        EXPECT_EQ(result.status, SwapchainStatus::Unsupported);
        EXPECT_NE(result.message.find("usage"), std::string::npos);
    }

    TEST(SwapchainConfigTest, RejectsZeroImageCount) {
        auto capabilities = make_capabilities();
        capabilities.minImageCount = 0;
        capabilities.maxImageCount = 0;
        auto request = make_request();
        request.image_count = 0;

        const auto result = select_swapchain(capabilities, make_surface_formats(),
            make_present_modes(), vk::Extent2D{800, 600}, request);

        EXPECT_EQ(result.status, SwapchainStatus::Unsupported);
        EXPECT_NE(result.message.find("at least one image"), std::string::npos);
    }

    TEST(SwapchainConfigTest, AcceptsUndefinedSurfaceFormat) {
        const std::vector formats{
            vk::SurfaceFormatKHR{vk::Format::eUndefined, vk::ColorSpaceKHR::eSrgbNonlinear}};

        const auto result = select_swapchain(make_capabilities(), formats, make_present_modes(),
            vk::Extent2D{800, 600}, make_request());

        ASSERT_EQ(result.status, SwapchainStatus::Ready);
        // make_request() 请求 B8G8R8A8_SRGB / SrgbNonlinear，结果按 Vulkan 类型比较
        EXPECT_EQ(result.config.surface_format,
            (vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear}));
    }

    TEST(SwapchainConfigTest, RejectsMissingFormatAndCompositeAlpha) {
        const auto missing_format = select_swapchain(
            make_capabilities(), {}, make_present_modes(), vk::Extent2D{800, 600}, make_request());
        EXPECT_EQ(missing_format.status, SwapchainStatus::Unsupported);

        auto capabilities = make_capabilities();
        capabilities.supportedCompositeAlpha = {};
        const auto missing_alpha = select_swapchain(capabilities, make_surface_formats(),
            make_present_modes(), vk::Extent2D{800, 600}, make_request());
        EXPECT_EQ(missing_alpha.status, SwapchainStatus::Unsupported);
        EXPECT_NE(missing_alpha.message.find("composite alpha"), std::string::npos);
    }

    TEST(SwapchainConfigTest, ReportsDependentCompatibilityChanges) {
        SwapchainConfig previous{.image_count = 3,
            .extent = vk::Extent2D{1280, 720},
            .surface_format = {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear}};
        SwapchainConfig current = previous;

        const SwapchainCompatibility unchanged = compare_swapchain_configs(previous, current);
        EXPECT_FALSE(unchanged.extent_changed);
        EXPECT_FALSE(unchanged.format_changed);
        EXPECT_FALSE(unchanged.image_count_changed);

        current.extent = vk::Extent2D{1920, 1080};
        current.image_count = 2;
        current.surface_format.format = vk::Format::eR8G8B8A8Srgb;
        const SwapchainCompatibility changes = compare_swapchain_configs(previous, current);

        EXPECT_TRUE(changes.extent_changed);
        EXPECT_TRUE(changes.format_changed);
        EXPECT_TRUE(changes.image_count_changed);
    }

    TEST(SwapchainConfigTest, TreatsColorSpaceAsFormatCompatibility) {
        SwapchainConfig previous{.image_count = 3,
            .extent = vk::Extent2D{1280, 720},
            .surface_format = {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear}};
        SwapchainConfig current = previous;
        current.surface_format.colorSpace = vk::ColorSpaceKHR::eExtendedSrgbLinearEXT;

        const SwapchainCompatibility changes = compare_swapchain_configs(previous, current);

        EXPECT_FALSE(changes.extent_changed);
        EXPECT_TRUE(changes.format_changed);
        EXPECT_FALSE(changes.image_count_changed);
    }
}
