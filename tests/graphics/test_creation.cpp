#include "graphics/result.h"
#include "graphics/creation.h"
#include "graphics/device.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "core/engine.h"
#include "support/engine_fixture.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace Comet::Tests {
    static_assert(!std::is_default_constructible_v<GpuResourceResult<int>>);
    static_assert(!std::is_default_constructible_v<GpuResourceResult<void>>);

    TEST(GpuResourceResultTest, DistinguishesSuccessFromFailure) {
        const auto failure = GpuResourceResult<int>::failure(vk::Result::eErrorOutOfDeviceMemory);
        const auto success = GpuResourceResult<int>::success(42);
        const auto normalized_failure = GpuResourceResult<int>::failure(vk::Result::eSuccess);

        EXPECT_FALSE(static_cast<bool>(failure));
        EXPECT_EQ(failure.result(), vk::Result::eErrorOutOfDeviceMemory);
        EXPECT_EQ(failure.error().message, vk::to_string(failure.result()));
        EXPECT_EQ(failure.error().result, failure.result());
        EXPECT_FALSE(failure.error().is_device_lost());
        EXPECT_TRUE(static_cast<bool>(success));
        EXPECT_EQ(success.value(), 42);
        EXPECT_FALSE(static_cast<bool>(normalized_failure));
        EXPECT_EQ(normalized_failure.result(), vk::Result::eErrorUnknown);

        const auto empty_failure =
            GpuResourceResult<void>::failure(vk::Result::eErrorOutOfDeviceMemory);
        const auto empty_success = GpuResourceResult<void>::success();
        EXPECT_FALSE(static_cast<bool>(empty_failure));
        EXPECT_TRUE(static_cast<bool>(empty_success));
        EXPECT_EQ(empty_failure.error().result, empty_failure.result());
        const auto device_lost = GpuResourceResult<void>::failure(vk::Result::eErrorDeviceLost);
        EXPECT_TRUE(device_lost.error().is_device_lost());
        EXPECT_FALSE(GraphicsError{"Invalid descriptor binding"}.is_device_lost());
    }

    TEST(GpuResourceResultTest, RejectsFailedValueAccess) {
        EXPECT_DEATH(
            {
                auto failure = GpuResourceResult<int>::failure(vk::Result::eErrorOutOfDeviceMemory);
                static_cast<void>(failure.value());
            },
            "");
    }

    TEST(GpuResourceResultTest, TransfersMoveOnlyOwnership) {
        auto result = GpuResourceResult<std::unique_ptr<int>>::success(std::make_unique<int>(42));
        auto owner = std::move(result).value();
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(*owner, 42);
    }

    using GraphicsCreationGpuTest = EngineTest;

    TEST(GraphicsCreationTest, PreservesNativeFailuresAndRejectsEmptySuccess) {
        for(const auto status :
            {vk::Result::eErrorOutOfHostMemory, vk::Result::eErrorOutOfDeviceMemory,
                vk::Result::eErrorDeviceLost, vk::Result::ePipelineCompileRequiredEXT}) {
            const auto result = Graphics::create_handle<vk::Pipeline>(
                vk::Device{}, "pipeline", [status](vk::Pipeline*) noexcept { return status; });
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error().result, status);
            EXPECT_EQ(result.error().is_device_lost(), status == vk::Result::eErrorDeviceLost);
            EXPECT_NE(result.error().message.find(vk::to_string(status)), std::string::npos);
        }
        const auto empty = Graphics::create_handle<vk::ShaderModule>(vk::Device{}, "shader",
            [](vk::ShaderModule*) noexcept { return vk::Result::eSuccess; });
        ASSERT_FALSE(empty);
        EXPECT_EQ(empty.error().result, vk::Result::eSuccess);
        EXPECT_NE(empty.error().message.find("without a handle"), std::string::npos);
    }

    TEST_F(GraphicsCreationGpuTest, ReclaimsPartialHandlesAndTransfersSuccessfulOwnership) {
        struct CountingDispatch: VULKAN_HPP_DEFAULT_DISPATCHER_TYPE {
            mutable int destroyed = 0;
            void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout layout,
                const VkAllocationCallbacks* allocator) const noexcept {
                ++destroyed;
                VULKAN_HPP_DEFAULT_DISPATCHER_TYPE::vkDestroyPipelineLayout(
                    device, layout, allocator);
            }
        } dispatch;
        const auto device = engine->get_renderer().get_render_context().get_device().get();
        const vk::PipelineLayoutCreateInfo info;
        // 先创建真实句柄，再模拟部分创建失败，验证失败候选会释放。
        const auto failed = Graphics::create_handle<vk::PipelineLayout>(
            device, "layout",
            [&](vk::PipelineLayout* output) noexcept {
                const auto status = device.createPipelineLayout(&info, nullptr, output);
                if(status != vk::Result::eSuccess)
                    return status;
                return vk::Result::eErrorOutOfDeviceMemory;
            },
            dispatch);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().result, vk::Result::eErrorOutOfDeviceMemory);
        EXPECT_EQ(dispatch.destroyed, 1);
        {
            auto success = Graphics::create_handle<vk::PipelineLayout>(
                device, "layout",
                [&](vk::PipelineLayout* output) noexcept {
                    return device.createPipelineLayout(&info, nullptr, output);
                },
                dispatch);
            ASSERT_TRUE(success) << success.error();
            auto owner = std::move(success).value();
            EXPECT_TRUE(owner);
            EXPECT_EQ(dispatch.destroyed, 1);
        }
        EXPECT_EQ(dispatch.destroyed, 2);
    }

}
