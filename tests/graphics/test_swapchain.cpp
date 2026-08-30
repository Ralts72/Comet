#include <gtest/gtest.h>

#include "graphics/vk_capability.h"
#include "graphics/swapchain.h"

#include <limits>

namespace Comet::Tests {
    namespace {
        vk::SurfaceCapabilitiesKHR make_capabilities() {
            vk::SurfaceCapabilitiesKHR capabilities{};
            capabilities.minImageCount = 2;
            capabilities.maxImageCount = 4;
            capabilities.currentExtent = vk::Extent2D{800, 600};
            capabilities.minImageExtent = vk::Extent2D{64, 64};
            capabilities.maxImageExtent = vk::Extent2D{1920, 1080};
            capabilities.maxImageArrayLayers = 1;
            capabilities.supportedTransforms =
                vk::SurfaceTransformFlagBitsKHR::eIdentity
                | vk::SurfaceTransformFlagBitsKHR::eRotate90;
            capabilities.currentTransform = vk::SurfaceTransformFlagBitsKHR::eRotate90;
            capabilities.supportedCompositeAlpha =
                vk::CompositeAlphaFlagBitsKHR::eOpaque
                | vk::CompositeAlphaFlagBitsKHR::eInherit;
            capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eColorAttachment;
            return capabilities;
        }

        SwapchainRequest make_request() {
            return {
                .image_count = 3,
                .surface_format = {
                    vk::Format::eB8G8R8A8Srgb,
                    vk::ColorSpaceKHR::eSrgbNonlinear
                },
                .present_mode = vk::PresentModeKHR::eMailbox,
                .usage = vk::ImageUsageFlagBits::eColorAttachment
            };
        }

        std::vector<vk::SurfaceFormatKHR> make_surface_formats() {
            return {{
                vk::Format::eB8G8R8A8Srgb,
                vk::ColorSpaceKHR::eSrgbNonlinear
            }};
        }

        std::vector<vk::PresentModeKHR> make_present_modes() {
            return {
                vk::PresentModeKHR::eFifo,
                vk::PresentModeKHR::eMailbox
            };
        }
    }

    TEST(SwapchainConfigTest, UsesFixedExtentAndSurfaceTransform) {
        const auto result = select_swapchain(
            make_capabilities(),
            make_surface_formats(),
            make_present_modes(),
            vk::Extent2D{320, 240},
            make_request());

        ASSERT_EQ(result.status, SwapchainStatus::Ready);
        EXPECT_EQ(result.config.image_count, 3u);
        EXPECT_EQ(result.config.extent, (vk::Extent2D{800, 600}));
        EXPECT_EQ(result.config.transform,
            vk::SurfaceTransformFlagBitsKHR::eRotate90);
        EXPECT_EQ(result.config.composite_alpha,
            vk::CompositeAlphaFlagBitsKHR::eOpaque);
        EXPECT_EQ(result.config.present_mode, vk::PresentModeKHR::eMailbox);
        EXPECT_TRUE(result.config.clipped);
    }

    TEST(SwapchainConfigTest, ClampsVariableExtentWithoutImageCountUpperLimit) {
        auto capabilities = make_capabilities();
        capabilities.maxImageCount = 0;
        capabilities.currentExtent = vk::Extent2D{
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max()
        };
        auto request = make_request();
        request.image_count = 5;

        const auto result = select_swapchain(
            capabilities,
            make_surface_formats(),
            make_present_modes(),
            vk::Extent2D{4000, 10},
            request);

        ASSERT_EQ(result.status, SwapchainStatus::Ready);
        EXPECT_EQ(result.config.image_count, 5u);
        EXPECT_EQ(result.config.extent, (vk::Extent2D{1920, 64}));
    }

    TEST(SwapchainConfigTest, DefersZeroSizedFramebuffer) {
        const auto result = select_swapchain(
            make_capabilities(),
            make_surface_formats(),
            make_present_modes(),
            vk::Extent2D{0, 0},
            make_request());

        EXPECT_EQ(result.status, SwapchainStatus::Deferred);
        EXPECT_FALSE(result.message.empty());
    }

    TEST(SwapchainConfigTest, FallsBackToFifoAndSupportedCompositeAlpha) {
        auto capabilities = make_capabilities();
        capabilities.supportedCompositeAlpha =
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied
            | vk::CompositeAlphaFlagBitsKHR::eInherit;
        auto request = make_request();
        request.present_mode = vk::PresentModeKHR::eImmediate;

        const auto result = select_swapchain(
            capabilities,
            make_surface_formats(),
            make_present_modes(),
            vk::Extent2D{800, 600},
            request);

        ASSERT_EQ(result.status, SwapchainStatus::Ready);
        EXPECT_EQ(result.config.present_mode, vk::PresentModeKHR::eFifo);
        EXPECT_EQ(result.config.composite_alpha,
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied);
        EXPECT_FALSE(result.message.empty());
    }

    TEST(SwapchainConfigTest, RejectsUnsupportedRequiredImageUsage) {
        auto request = make_request();
        request.usage |= vk::ImageUsageFlagBits::eTransferSrc;

        const auto result = select_swapchain(
            make_capabilities(),
            make_surface_formats(),
            make_present_modes(),
            vk::Extent2D{800, 600},
            request);

        EXPECT_EQ(result.status, SwapchainStatus::Unsupported);
        EXPECT_NE(result.message.find("usage"), std::string::npos);
    }

    TEST(SwapchainConfigTest, RejectsZeroImageCount) {
        auto capabilities = make_capabilities();
        capabilities.minImageCount = 0;
        capabilities.maxImageCount = 0;
        auto request = make_request();
        request.image_count = 0;

        const auto result = select_swapchain(
            capabilities,
            make_surface_formats(),
            make_present_modes(),
            vk::Extent2D{800, 600},
            request);

        EXPECT_EQ(result.status, SwapchainStatus::Unsupported);
        EXPECT_NE(result.message.find("at least one image"), std::string::npos);
    }

    TEST(SwapchainConfigTest, AcceptsUndefinedSurfaceFormat) {
        const std::vector formats{
            vk::SurfaceFormatKHR{
                vk::Format::eUndefined,
                vk::ColorSpaceKHR::eSrgbNonlinear
            }
        };

        const auto result = select_swapchain(
            make_capabilities(),
            formats,
            make_present_modes(),
            vk::Extent2D{800, 600},
            make_request());

        ASSERT_EQ(result.status, SwapchainStatus::Ready);
        EXPECT_EQ(result.config.surface_format, make_request().surface_format);
    }

    TEST(SwapchainConfigTest, RejectsMissingFormatAndCompositeAlpha) {
        const auto missing_format = select_swapchain(
            make_capabilities(),
            {},
            make_present_modes(),
            vk::Extent2D{800, 600},
            make_request());
        EXPECT_EQ(missing_format.status, SwapchainStatus::Unsupported);

        auto capabilities = make_capabilities();
        capabilities.supportedCompositeAlpha = {};
        const auto missing_alpha = select_swapchain(
            capabilities,
            make_surface_formats(),
            make_present_modes(),
            vk::Extent2D{800, 600},
            make_request());
        EXPECT_EQ(missing_alpha.status, SwapchainStatus::Unsupported);
        EXPECT_NE(missing_alpha.message.find("composite alpha"), std::string::npos);
    }

    TEST(SwapchainConfigTest, ReportsDependentCompatibilityChanges) {
        SwapchainConfig previous{
            .image_count = 3,
            .extent = vk::Extent2D{1280, 720},
            .surface_format = {
                vk::Format::eB8G8R8A8Srgb,
                vk::ColorSpaceKHR::eSrgbNonlinear
            }
        };
        SwapchainConfig current = previous;

        const SwapchainCompatibility unchanged =
            compare_swapchain_configs(previous, current);
        EXPECT_FALSE(unchanged.extent_changed);
        EXPECT_FALSE(unchanged.format_changed);
        EXPECT_FALSE(unchanged.image_count_changed);

        current.extent = vk::Extent2D{1920, 1080};
        current.image_count = 2;
        current.surface_format.format = vk::Format::eR8G8B8A8Srgb;
        const SwapchainCompatibility changes =
            compare_swapchain_configs(previous, current);

        EXPECT_TRUE(changes.extent_changed);
        EXPECT_TRUE(changes.format_changed);
        EXPECT_TRUE(changes.image_count_changed);
    }
}
