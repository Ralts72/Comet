#include "render_context.h"
#include "common/logger.h"
#include "common/profiler.h"

namespace Comet {
    RenderContext::RenderContext(const Window& window, const Config::Vulkan& vulkan_config, const Config::Render& render_config) {
        PROFILE_SCOPE("RenderContext::Constructor");
        LOG_INFO("init graphics system");

        auto present_mode = static_cast<vk::PresentModeKHR>(vulkan_config.present_mode);
        if(render_config.enable_vsync) {
            present_mode = vk::PresentModeKHR::eFifo;
        }

        const SwapchainRequest swapchain_request{
            .image_count = vulkan_config.swapchain_image_count,
            .surface_format = {
                static_cast<vk::Format>(vulkan_config.surface_format),
                static_cast<vk::ColorSpaceKHR>(vulkan_config.color_space)
            },
            .present_mode = present_mode,
            .usage = vk::ImageUsageFlagBits::eColorAttachment
        };
        const DeviceCapabilityRequest capability_request{
            .swapchain = swapchain_request,
            .depth_format = static_cast<vk::Format>(vulkan_config.depth_format),
            .sample_count = static_cast<vk::SampleCountFlagBits>(vulkan_config.msaa_samples),
            .max_sampler_anisotropy = render_config.max_anisotropy
        };
        m_context = std::make_unique<Context>(window, vulkan_config, capability_request);

        LOG_INFO("create device");
        m_device = std::make_unique<Device>(*m_context);

        LOG_INFO("create swapchain");
        m_swapchain = std::make_unique<Swapchain>(
            window, *m_context, *m_device, swapchain_request);
    }

    void RenderContext::wait_idle() const {
        if (m_device) {
            m_device->wait_idle();
        } else {
            LOG_ERROR("m_device is nullptr, can't wait idle");
        }
    }

    RenderContext::~RenderContext() {
        PROFILE_SCOPE("RenderContext::Destructor");
        LOG_INFO("destroy render context");
        wait_idle();

        m_swapchain.reset();
        m_device.reset();
        m_context.reset();
    }
}
