#include <gtest/gtest.h>

#include "graphics/vk_capability.h"

#include <algorithm>
#include <string>

namespace Comet::Tests {
    namespace {
        DeviceCandidateInfo make_suitable_candidate() {
            return {
                .api_version = REQUIRED_VULKAN_API_VERSION,
                .device_type = vk::PhysicalDeviceType::eIntegratedGpu,
                .max_image_dimension_2d = 8192,
                .has_graphics_queue = true,
                .has_present_queue = true,
                .shares_graphics_present_queue = true,
                .swapchain_status = SwapchainStatus::Ready,
                .requested_present_mode_supported = true,
                .color_format_supported = true,
                .depth_format_supported = true,
                .synchronization2_supported = true
            };
        }

        bool contains_reason(const DeviceCandidateEvaluation& evaluation,
                             const std::string& text) {
            return std::ranges::any_of(
                evaluation.rejection_reasons,
                [&text](const std::string& reason) {
                    return reason.find(text) != std::string::npos;
                });
        }
    }

    TEST(DeviceCandidateEvaluationTest, RejectsApiVersionBelowRequirement) {
        auto candidate = make_suitable_candidate();
        candidate.api_version = VK_API_VERSION_1_2;

        const auto evaluation = evaluate_device_candidate(
            candidate, DeviceCapabilityRequest{});

        EXPECT_FALSE(evaluation.is_suitable());
        EXPECT_TRUE(contains_reason(evaluation, "Vulkan API"));
    }

    TEST(DeviceCandidateEvaluationTest, ReportsAllMissingHardRequirements) {
        auto candidate = make_suitable_candidate();
        candidate.has_graphics_queue = false;
        candidate.has_present_queue = false;
        candidate.missing_required_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        candidate.swapchain_status = SwapchainStatus::Unsupported;
        candidate.swapchain_message = "no surface formats";
        candidate.color_format_supported = false;
        candidate.depth_format_supported = false;
        candidate.synchronization2_supported = false;

        const auto evaluation = evaluate_device_candidate(
            candidate, DeviceCapabilityRequest{});

        EXPECT_FALSE(evaluation.is_suitable());
        EXPECT_TRUE(contains_reason(evaluation, "graphics queue"));
        EXPECT_TRUE(contains_reason(evaluation, "present queue"));
        EXPECT_TRUE(contains_reason(evaluation, VK_KHR_SWAPCHAIN_EXTENSION_NAME));
        EXPECT_TRUE(contains_reason(evaluation, "swapchain configuration"));
        EXPECT_TRUE(contains_reason(evaluation, "color format"));
        EXPECT_TRUE(contains_reason(evaluation, "depth format"));
        EXPECT_TRUE(contains_reason(evaluation, "synchronization2"));
    }

    TEST(DeviceCandidateEvaluationTest, ScoresPreferredCapabilitiesDeterministically) {
        auto integrated = make_suitable_candidate();
        integrated.shares_graphics_present_queue = false;
        auto discrete = integrated;
        discrete.device_type = vk::PhysicalDeviceType::eDiscreteGpu;
        discrete.shares_graphics_present_queue = true;

        const auto integrated_evaluation = evaluate_device_candidate(
            integrated, DeviceCapabilityRequest{});
        const auto discrete_evaluation = evaluate_device_candidate(
            discrete, DeviceCapabilityRequest{});

        ASSERT_TRUE(integrated_evaluation.is_suitable());
        ASSERT_TRUE(discrete_evaluation.is_suitable());
        EXPECT_GT(discrete_evaluation.score, integrated_evaluation.score);
    }

    TEST(DeviceCandidateEvaluationTest, ClampsOrDisablesOptionalAnisotropy) {
        DeviceCapabilityRequest request;
        request.max_sampler_anisotropy = 16.0f;
        auto candidate = make_suitable_candidate();
        candidate.sampler_anisotropy_supported = true;
        candidate.max_sampler_anisotropy = 8.0f;

        const auto clamped = evaluate_device_candidate(candidate, request);
        EXPECT_TRUE(clamped.enabled_features.samplerAnisotropy);
        EXPECT_FLOAT_EQ(clamped.max_sampler_anisotropy, 8.0f);

        candidate.sampler_anisotropy_supported = false;
        const auto disabled = evaluate_device_candidate(candidate, request);
        EXPECT_FALSE(disabled.enabled_features.samplerAnisotropy);
        EXPECT_FLOAT_EQ(disabled.max_sampler_anisotropy, 1.0f);
        EXPECT_FALSE(disabled.notes.empty());
    }

    TEST(DeviceCandidateEvaluationTest, EnablesRequiredSynchronization2Feature) {
        const auto evaluation = evaluate_device_candidate(
            make_suitable_candidate(), DeviceCapabilityRequest{});

        ASSERT_TRUE(evaluation.is_suitable());
        EXPECT_TRUE(evaluation.enabled_vulkan13_features.synchronization2);
    }
}
