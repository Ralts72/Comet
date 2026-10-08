#include "render/presentation.h"
#include "render/render_context.h"
#include "render/render_target.h"
#include "render/frame_scheduler.h"
#include "core/window.h"
#include "graphics/device.h"
#include "graphics/render_pass.h"
#include "graphics/resource/image.h"
#include "diagnostics/logger.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <thread>

#undef vkGetPhysicalDeviceSurfaceFormatsKHR
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t* count, VkSurfaceFormatKHR* formats);

namespace {
    struct WsiCalls {
        bool fail_creation = false;
        bool fail_images = false;
        bool fail_acquire = false;
        bool fail_present = false;
        bool expire_present = false;
        bool incomplete_formats = false;
        VkResult memory_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        unsigned format_queries = 0;
        unsigned expire_acquires = 0;
        unsigned creates = 0;
        unsigned acquires = 0;
        unsigned presents = 0;
        VkSwapchainKHR previous = VK_NULL_HANDLE;
        PFN_vkQueuePresentKHR real_present = nullptr;
    } calls;
}

VKAPI_ATTR VkResult VKAPI_CALL comet_test_get_surface_formats(
    VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t* count, VkSurfaceFormatKHR* formats) {
    const auto result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, count, formats);
    if(formats && calls.incomplete_formats) {
        ++calls.format_queries;
        return VK_INCOMPLETE;
    }
    return result;
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
        return calls.memory_error;
    }
    return status;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images) {
    if(calls.fail_images)
        return calls.memory_error;
    const auto query = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR"));
    return query(device, swapchain, count, images);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
    uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* image) {
    ++calls.acquires;
    if(calls.fail_acquire)
        return calls.memory_error;
    if(calls.expire_acquires) {
        --calls.expire_acquires;
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    const auto acquire = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
        vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR"));
    return acquire(device, swapchain, timeout, semaphore, fence, image);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    ++calls.presents;
    const auto result = calls.real_present(queue, info);
    if(calls.fail_present && result == VK_SUCCESS)
        return calls.memory_error;
    if(calls.expire_present && result == VK_SUCCESS)
        return VK_ERROR_OUT_OF_DATE_KHR;
    return result;
}

namespace Comet::Tests {
    class SwapchainRecoveryTest: public testing::TestWithParam<bool> {
    protected:
        std::unique_ptr<Window> window;
        std::unique_ptr<RenderContext> context;
        std::unique_ptr<FrameScheduler> frames;
        std::unique_ptr<Presentation> presentation;
        std::unique_ptr<RenderPass> pass;
        std::unique_ptr<RenderTarget> target;
        std::unique_ptr<RenderPass> offscreen_pass;
        std::unique_ptr<RenderTarget> offscreen;
        unsigned releases = 0;
        unsigned rebuilds = 0;
        std::ostringstream messages;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink;

        void SetUp() override {
            calls = {};
            Logger::init();
            sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            Logger::add_custom_sink(sink);
            VulkanSettings vulkan;
            vulkan.msaa_samples = SampleCount::Count1;
            vulkan.enable_validation = true;
            const RenderSettings render;
            window = std::make_unique<Window>(WindowSettings{});
            auto created = RenderContext::create(*window, vulkan, render);
            ASSERT_TRUE(created);
            context = std::move(created).value();
            auto& device = context->get_device();
            auto& swapchain = context->get_swapchain();
            calls.real_present = reinterpret_cast<PFN_vkQueuePresentKHR>(
                device.get().getProcAddr("vkQueuePresentKHR"));
            frames = std::make_unique<FrameScheduler>(device, render.max_frames_in_flight);
            frames->initialize_swapchain_images(
                static_cast<uint32_t>(swapchain.get_images().size()));
            const auto format = swapchain.get_images().front()->get_info().format;
            const auto color = Attachment::get_color_attachment(format);
            const std::vector<RenderSubPass> subpasses{
                {{}, {SubpassColorAttachment(0)}, {}, SampleCount::Count1}};
            auto render_pass = RenderPass::create(device, {color}, subpasses, format);
            ASSERT_TRUE(render_pass);
            pass = std::move(render_pass).value();
            ASSERT_TRUE(rebuild_target());
            if(GetParam()) {
                auto attachment = color;
                attachment.description.final_layout = ImageLayout::ShaderReadOnlyOptimal;
                attachment.usage |= ImageUsage::Sampled;
                auto offscreen_render_pass =
                    RenderPass::create(device, {attachment}, subpasses, format);
                ASSERT_TRUE(offscreen_render_pass);
                offscreen_pass = std::move(offscreen_render_pass).value();
                auto multi = RenderTarget::try_create_multi_target(device, *offscreen_pass,
                    window->get_framebuffer_size(), render.max_frames_in_flight);
                ASSERT_TRUE(multi);
                offscreen = std::move(multi).value();
            }
            presentation = std::make_unique<Presentation>(*context, *frames,
                Presentation::Dependent{[this] {
                                            ++releases;
                                            target.reset();
                                        },
                    [this](const SwapchainCompatibility& compatibility) {
                        ++rebuilds;
                        EXPECT_FALSE(compatibility.format_changed);
                        return rebuild_target();
                    }});
            calls.creates = calls.acquires = calls.presents = 0;
        }

        Result<void, GraphicsError> rebuild_target() {
            auto next = RenderTarget::create_swapchain_target(
                context->get_device(), *pass, context->get_swapchain());
            if(!next)
                return Result<void, GraphicsError>::failure(next.error());
            target = std::move(next).value();
            return Result<void, GraphicsError>::success();
        }

        void TearDown() override {
            if(context)
                context->get_device().wait_idle_for_shutdown();
            presentation.reset();
            target.reset();
            offscreen.reset();
            pass.reset();
            offscreen_pass.reset();
            frames.reset();
            context.reset();
            window.reset();
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos) << messages.str();
            Logger::shutdown();
        }

        void finish_frame() {
            auto& command = frames->get_current_command_buffer();
            if(offscreen) {
                offscreen->begin_render_target(command, frames->get_current_frame_slot_index());
                offscreen->end_render_target(command);
            }
            target->begin_render_target(command);
            target->end_render_target(command);
            ASSERT_TRUE(presentation->end_frame({}));
        }

        void resume() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while(std::chrono::steady_clock::now() < deadline) {
                const auto ready = presentation->begin_frame();
                ASSERT_TRUE(ready) << ready.error().message;
                if(ready.value()) {
                    finish_frame();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            FAIL() << "Presentation did not resume";
        }

        void verify_retirement(bool fail_images) {
            auto ready = presentation->begin_frame();
            ASSERT_TRUE(ready);
            ASSERT_TRUE(ready.value());
            finish_frame();
            auto& swapchain = context->get_swapchain();
            const auto old_handle = swapchain.get();
            std::weak_ptr previous(swapchain.get_active_generation());
            auto* retained_offscreen = offscreen.get();
            const auto serial = frames->get_current_frame_serial();
            calls.fail_creation = !fail_images;
            calls.fail_images = fail_images;
            presentation->request_recreation();
            ready = presentation->begin_frame();
            ASSERT_FALSE(ready);
            EXPECT_EQ(ready.error().result, static_cast<vk::Result>(calls.memory_error));
            EXPECT_EQ(calls.creates, 1u);
            EXPECT_EQ(calls.previous, static_cast<VkSwapchainKHR>(old_handle));
            EXPECT_FALSE(swapchain.get_active_generation());
            EXPECT_TRUE(previous.expired());
            EXPECT_FALSE(target);
            EXPECT_EQ(rebuilds, 0u);
            const auto acquires = calls.acquires;
            EXPECT_FALSE(swapchain.acquire_next_image(
                frames->get_current_frame_slot().image_available_semaphore));
            EXPECT_EQ(calls.acquires, acquires);
            EXPECT_FALSE(frames->is_frame_active());
            EXPECT_EQ(frames->get_current_frame_serial(), serial);
            EXPECT_EQ(offscreen.get(), retained_offscreen);
            EXPECT_EQ(messages.str().find("scheduled retry"), std::string::npos);
        }
    };

    TEST_P(SwapchainRecoveryTest, CreationOutOfMemoryRetiresOldGenerationAndFailsImmediately) {
        calls.memory_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        verify_retirement(false);
    }
    TEST_P(SwapchainRecoveryTest, ImageEnumerationOutOfMemoryReleasesCandidateAndFailsImmediately) {
        verify_retirement(true);
    }

    TEST_P(SwapchainRecoveryTest, AcquireOutOfMemoryDoesNotBeginOrSubmitFrame) {
        calls.fail_acquire = true;
        const auto serial = frames->get_current_frame_serial();
        const auto ready = presentation->begin_frame();
        ASSERT_FALSE(ready);
        EXPECT_EQ(ready.error().result, vk::Result::eErrorOutOfDeviceMemory);
        EXPECT_EQ(calls.acquires, 1u);
        EXPECT_EQ(calls.creates, 0u);
        EXPECT_EQ(calls.presents, 0u);
        EXPECT_FALSE(frames->is_frame_active());
        EXPECT_EQ(frames->get_current_frame_serial(), serial);
        EXPECT_TRUE(context->get_swapchain().get_active_generation());
    }

    TEST_P(SwapchainRecoveryTest, PresentOutOfMemoryFinishesSubmittedFrameAndReturnsError) {
        const auto ready = presentation->begin_frame();
        ASSERT_TRUE(ready);
        ASSERT_TRUE(ready.value());
        const auto serial = frames->get_current_frame_serial();
        calls.fail_present = true;
        calls.memory_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        auto& command = frames->get_current_command_buffer();
        target->begin_render_target(command);
        target->end_render_target(command);
        const auto presented = presentation->end_frame({});
        ASSERT_FALSE(presented);
        EXPECT_EQ(presented.error().result, vk::Result::eErrorOutOfHostMemory);
        EXPECT_EQ(calls.presents, 1u);
        EXPECT_EQ(calls.creates, 0u);
        EXPECT_FALSE(frames->is_frame_active());
        EXPECT_EQ(frames->get_current_frame_serial(), serial + 1);
    }

    TEST_P(SwapchainRecoveryTest, DependentOutOfMemoryFailsAfterCreatingNewGeneration) {
        const auto previous = context->get_swapchain().get();
        presentation->set_overlay({.rebuild = [](const SwapchainCompatibility&) {
            return Result<void, GraphicsError>::failure(
                {"Cannot rebuild overlay", vk::Result::eErrorOutOfHostMemory});
        }});
        presentation->request_recreation();
        const auto ready = presentation->begin_frame();
        ASSERT_FALSE(ready);
        EXPECT_EQ(ready.error().result, vk::Result::eErrorOutOfHostMemory);
        EXPECT_EQ(calls.creates, 1u);
        EXPECT_EQ(calls.acquires, 0u);
        EXPECT_EQ(releases, 1u);
        EXPECT_EQ(rebuilds, 1u);
        EXPECT_TRUE(target);
        EXPECT_NE(context->get_swapchain().get(), previous);
        EXPECT_TRUE(context->get_swapchain().get_active_generation());
        EXPECT_FALSE(frames->is_frame_active());
    }
    TEST_P(SwapchainRecoveryTest, RepeatedOutOfDateAcquireDoesNotSubmitOrResetFence) {
        calls.expire_acquires = 2;
        const auto serial = frames->get_current_frame_serial();
        for(unsigned i = 0; i < 2; ++i) {
            const auto ready = presentation->begin_frame();
            ASSERT_TRUE(ready);
            EXPECT_FALSE(ready.value());
            EXPECT_FALSE(frames->is_frame_active());
            EXPECT_EQ(frames->get_current_frame_serial(), serial);
        }
        EXPECT_EQ(calls.acquires, 2u);
        resume();
        EXPECT_EQ(frames->get_current_frame_serial(), serial + 1);
    }
    TEST_P(SwapchainRecoveryTest, OutOfDatePresentFinishesFrameBeforeRecreationOutOfMemory) {
        const auto ready = presentation->begin_frame();
        ASSERT_TRUE(ready);
        ASSERT_TRUE(ready.value());
        const auto serial = frames->get_current_frame_serial();
        calls.expire_present = calls.fail_creation = true;
        finish_frame();
        EXPECT_FALSE(frames->is_frame_active());
        EXPECT_EQ(frames->get_current_frame_serial(), serial + 1);
        const auto failed = presentation->begin_frame();
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().result, vk::Result::eErrorOutOfDeviceMemory);
        EXPECT_EQ(calls.creates, 1u);
        EXPECT_FALSE(context->get_swapchain().get_active_generation());
        EXPECT_FALSE(frames->is_frame_active());
    }

    TEST_P(SwapchainRecoveryTest, IncompleteSurfaceEnumerationIsBoundedAndRetried) {
        const auto previous = context->get_swapchain().get_active_generation();
        calls.incomplete_formats = true;
        presentation->request_recreation();
        const auto ready = presentation->begin_frame();
        ASSERT_TRUE(ready);
        EXPECT_FALSE(ready.value());
        EXPECT_GT(calls.format_queries, 0u);
        EXPECT_LE(calls.format_queries, 4u);
        EXPECT_EQ(calls.creates, 0u);
        EXPECT_EQ(calls.acquires, 0u);
        EXPECT_EQ(context->get_swapchain().get_active_generation(), previous);
        calls.incomplete_formats = false;
        resume();
        EXPECT_NE(context->get_swapchain().get_active_generation(), previous);
    }

    TEST_P(SwapchainRecoveryTest, PersistentIncompleteSurfaceEnumerationExhaustsRetryBudget) {
        const auto previous = context->get_swapchain().get_active_generation();
        calls.incomplete_formats = true;
        presentation->request_recreation();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while(std::chrono::steady_clock::now() < deadline) {
            const auto ready = presentation->begin_frame();
            if(!ready) {
                EXPECT_EQ(ready.error().result, vk::Result::eIncomplete);
                EXPECT_EQ(calls.format_queries, 16u);
                EXPECT_EQ(calls.creates, 0u);
                EXPECT_EQ(calls.acquires, 0u);
                EXPECT_FALSE(frames->is_frame_active());
                EXPECT_EQ(context->get_swapchain().get_active_generation(), previous);
                return;
            }
            EXPECT_FALSE(ready.value());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        FAIL() << "Automatic recovery did not exhaust its retry budget";
    }

    INSTANTIATE_TEST_SUITE_P(
        DirectAndOffscreen, SwapchainRecoveryTest, testing::Values(false, true));
}
