#include "render_context.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "graphics/convert.h"

namespace Comet {
    RenderContext::RenderContext(const Window& window, const Config::Vulkan& vulkan_config, const Config::Render& render_config) {
        PROFILE_SCOPE("RenderContext::Constructor");
        LOG_INFO("init graphics system");

        auto present_mode = Graphics::present_mode_to_vk(vulkan_config.present_mode);
        if(render_config.enable_vsync) {
            present_mode = vk::PresentModeKHR::eFifo;
        }

        const SwapchainRequest swapchain_request{
            .image_count = vulkan_config.swapchain_image_count,
            .surface_format = {
                Graphics::format_to_vk(vulkan_config.surface_format),
                Graphics::image_color_space_to_vk(vulkan_config.color_space)
            },
            .present_mode = present_mode,
            .usage = vk::ImageUsageFlagBits::eColorAttachment
        };
        const DeviceCapabilityRequest capability_request{
            .swapchain = swapchain_request,
            .depth_format = Graphics::format_to_vk(vulkan_config.depth_format),
            .sample_count = Graphics::sample_count_to_vk(vulkan_config.msaa_samples),
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
