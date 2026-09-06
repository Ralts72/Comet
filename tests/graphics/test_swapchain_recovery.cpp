#include "render/scene/scene_renderer.h"
#include "core/window.h"
#include "graphics/resource/image.h"
#include "diagnostics/logger.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <thread>

namespace {
    struct WsiCalls {
        bool fail_creation = false;
        bool fail_images = false;
        bool expire_present = false;
        unsigned expire_acquires = 0;
        unsigned creates = 0;
        unsigned acquires = 0;
        unsigned presents = 0;
        VkSwapchainKHR previous = VK_NULL_HANDLE;
        PFN_vkQueuePresentKHR real_present = nullptr;
    } calls;
}

// 只在本测试目标中重命名入口；生产源原样编译，正式 engine 没有测试开关。
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device,
    const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks* allocator,
    VkSwapchainKHR* result) {
    ++calls.creates;
    calls.previous = info->oldSwapchain;
    const auto create = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
    const auto status = create(device, info, allocator, result);
    if(status == VK_SUCCESS && calls.fail_creation) {
        // 真实调用让驱动退休 oldSwapchain，然后销毁候选并模拟创建失败。
        const auto destroy = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
            vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR"));
        destroy(device, *result, allocator);
        *result = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return status;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images) {
    if(calls.fail_images)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const auto query = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR"));
    return query(device, swapchain, count, images);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device,
    VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence,
    uint32_t* image) {
    ++calls.acquires;
    if(calls.expire_acquires) {
        --calls.expire_acquires;
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    const auto acquire = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
        vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR"));
    return acquire(device, swapchain, timeout, semaphore, fence, image);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR* info) {
    ++calls.presents;
    const auto result = calls.real_present(queue, info);
    if(calls.expire_present && result == VK_SUCCESS)
        return VK_ERROR_OUT_OF_DATE_KHR;
    return result;
}

namespace Comet::Tests {
    class SwapchainRecoveryTest: public testing::TestWithParam<bool> {
    protected:
        Config config;
        std::unique_ptr<Window> window;
        std::unique_ptr<RenderContext> context;
        std::unique_ptr<SceneRenderer> renderer;
        std::shared_ptr<RenderPass> overlay_pass;
        std::unique_ptr<RenderTarget> overlay_target;
        unsigned releases = 0;
        unsigned rebuilds = 0;
        unsigned original_image_count = 0;
        std::ostringstream messages;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink;

        void SetUp() override {
            calls = {};
            Logger::init();
            sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            Logger::add_custom_sink(sink);
            ASSERT_TRUE(glfwInit());
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.msaa_samples = SampleCount::Count1;
            config.vulkan.enable_validation = true;
            window = std::make_unique<Window>(config.window);
            context =
                std::make_unique<RenderContext>(*window, config.vulkan, config.render);
            calls.real_present = reinterpret_cast<PFN_vkQueuePresentKHR>(
                context->get_device().get().getProcAddr("vkQueuePresentKHR"));
            renderer =
                std::make_unique<SceneRenderer>(*context, config.vulkan, config.render);
            renderer->setup_render_pass();
            original_image_count = context->get_swapchain().get_images().size();
            if(GetParam()) {
                const auto format =
                    context->get_swapchain().get_images().front()->get_info().format;
                overlay_pass = std::make_shared<RenderPass>(context->get_device(),
                    std::vector<Attachment>{
                        Attachment::get_color_attachment(format, SampleCount::Count1)},
                    std::vector<RenderSubPass>{
                        {{}, {SubpassColorAttachment(0)}, {}, SampleCount::Count1}},
                    format);
                overlay_target = RenderTarget::create_swapchain_target(
                    context->get_device(), *overlay_pass, context->get_swapchain());
                renderer->setup_offscreen_render_pass({80, 60});
            }
            renderer->set_swapchain_resource_callbacks(
                [this] {
                    ++releases;
                    overlay_target.reset();
                },
                [this](const SwapchainCompatibility& compatibility) {
                    ++rebuilds;
                    EXPECT_FALSE(compatibility.format_changed);
                    EXPECT_EQ(context->get_swapchain().get_images().size(),
                        original_image_count);
                    if(GetParam())
                        overlay_target =
                            RenderTarget::create_swapchain_target(context->get_device(),
                                *overlay_pass, context->get_swapchain());
                });
            calls.creates = calls.acquires = calls.presents = 0;
        }

        void TearDown() override {
            if(context)
                context->wait_idle();
            overlay_target.reset();
            overlay_pass.reset();
            renderer.reset();
            context.reset();
            window.reset();
            glfwTerminate();
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos)
                << messages.str();
            Logger::shutdown();
        }

        void finish_frame() {
            const auto waits = renderer->render_scene_pass({});
            if(overlay_target) {
                overlay_target->begin_render_target(
                    renderer->get_current_command_buffer());
                overlay_target->end_render_target(renderer->get_current_command_buffer());
            }
            renderer->end_frame(waits);
        }

        void verify_failure_and_recovery(bool fail_images) {
            ASSERT_TRUE(renderer->begin_frame());
            finish_frame();
            auto old = std::weak_ptr(context->get_swapchain().get_active_generation());
            const auto old_handle = context->get_swapchain().get();
            auto* offscreen = GetParam() ? &renderer->get_render_target() : nullptr;
            calls.fail_creation = !fail_images;
            calls.fail_images = fail_images;
            ASSERT_FALSE(renderer->recreate_swapchain());
            EXPECT_EQ(calls.previous, static_cast<VkSwapchainKHR>(old_handle));
            EXPECT_FALSE(context->get_swapchain().get_active_generation());
            EXPECT_TRUE(old.expired());
            EXPECT_EQ(releases, 1u);
            EXPECT_EQ(rebuilds, 0u);
            const auto acquires = calls.acquires;
            EXPECT_EQ(context->get_swapchain()
                          .acquire_next_image(renderer->get_frame_scheduler()
                                  .get_current_frame_slot()
                                  .image_available_semaphore)
                          .second,
                vk::Result::eErrorOutOfDateKHR);
            const auto presents = calls.presents;
            const auto serial =
                renderer->get_frame_scheduler().get_current_frame_serial();
            for(unsigned index = 0; index < 10; ++index)
                EXPECT_FALSE(renderer->begin_frame());
            EXPECT_EQ(calls.acquires, acquires);
            EXPECT_EQ(calls.presents, presents);
            EXPECT_FALSE(renderer->get_frame_scheduler().is_frame_active());
            EXPECT_EQ(renderer->get_frame_scheduler().get_current_frame_serial(), serial);
            EXPECT_FALSE(renderer->recreate_swapchain());
            EXPECT_EQ(calls.previous, VK_NULL_HANDLE);
            EXPECT_EQ(releases, 1u);
            calls.fail_creation = calls.fail_images = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(110));
            ASSERT_TRUE(renderer->begin_frame());
            EXPECT_EQ(calls.previous, VK_NULL_HANDLE);
            EXPECT_EQ(rebuilds, 1u);
            if(offscreen)
                EXPECT_EQ(&renderer->get_render_target(), offscreen);
            finish_frame();
            ASSERT_TRUE(renderer->begin_frame());
            finish_frame();
        }
    };

    TEST_P(SwapchainRecoveryTest, RetiredCreationFailurePausesAndAutomaticallyRecovers) {
        verify_failure_and_recovery(false);
    }
    TEST_P(SwapchainRecoveryTest, ImageEnumerationFailureDiscardsCandidateAndRecovers) {
        verify_failure_and_recovery(true);
    }
    TEST_P(
        SwapchainRecoveryTest, RepeatedOutOfDateAcquireDoesNotResetAnUnsubmittedFence) {
        calls.expire_acquires = 2;
        EXPECT_FALSE(renderer->begin_frame());
        EXPECT_FALSE(renderer->get_frame_scheduler().is_frame_active());
        EXPECT_EQ(calls.acquires, 2u);
        ASSERT_TRUE(renderer->begin_frame());
        finish_frame();
    }
    TEST_P(SwapchainRecoveryTest, PresentFailureFinishesSubmittedFrameBeforeRetry) {
        ASSERT_TRUE(renderer->begin_frame());
        const auto serial = renderer->get_frame_scheduler().get_current_frame_serial();
        calls.expire_present = calls.fail_creation = true;
        finish_frame();
        EXPECT_FALSE(renderer->get_frame_scheduler().is_frame_active());
        EXPECT_EQ(renderer->get_frame_scheduler().get_current_frame_serial(), serial + 1);
        EXPECT_FALSE(context->get_swapchain().get_active_generation());
        EXPECT_EQ(releases, 1u);
        calls.expire_present = calls.fail_creation = false;
        ASSERT_TRUE(renderer->recreate_swapchain());
        EXPECT_EQ(calls.previous, VK_NULL_HANDLE);
        ASSERT_TRUE(renderer->begin_frame());
        finish_frame();
    }
    TEST_P(SwapchainRecoveryTest, ShutdownWhileNoPresentationIsPending) {
        calls.fail_creation = true;
        ASSERT_FALSE(renderer->recreate_swapchain());
        EXPECT_FALSE(context->get_swapchain().get_active_generation());
        EXPECT_FALSE(renderer->get_frame_scheduler().is_frame_active());
        // TearDown 等待并销毁全部真实资源，不要求再成功创建一代交换链。
    }
    INSTANTIATE_TEST_SUITE_P(
        RuntimeAndOffscreen, SwapchainRecoveryTest, testing::Values(false, true));
}
