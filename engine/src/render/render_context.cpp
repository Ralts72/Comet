#include "render_context.h"
#include "core/window.h"
#include "config/config.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <utility>

namespace Comet {
    Result<std::unique_ptr<RenderContext>, GraphicsError> RenderContext::create(
        const Window& window, const Config::Vulkan& vulkan_config,
        const Config::Render& render_config) {
        PROFILE_SCOPE("RenderContext::Constructor");
        LOG_INFO("init graphics system");

        PresentMode present_mode = vulkan_config.present_mode;
        if(render_config.enable_vsync) {
            present_mode = PresentMode::Fifo;
        }
        const SwapchainRequest swapchain_request{.output_mode = render_config.output_mode,
            .image_count = vulkan_config.swapchain_image_count,
            .surface_format = vulkan_config.surface_format,
            .color_space = vulkan_config.color_space,
            .present_mode = present_mode,
            .usage = Flags<ImageUsage>(ImageUsage::ColorAttachment)};
        const DeviceCapabilityRequest capability_request{.swapchain = swapchain_request,
            .scene_color_format = Config::Render::SCENE_COLOR_FORMAT,
            .depth_format = vulkan_config.depth_format,
            .sample_count = vulkan_config.msaa_samples,
            .max_sampler_anisotropy = render_config.max_anisotropy};
        auto context = std::make_unique<Context>(window, vulkan_config, capability_request);

        LOG_INFO("create device");
        auto device = std::make_unique<Device>(*context);
        if(auto restored =
                device->get_pipeline_cache().restore(vulkan_config.pipeline_cache_directory);
            !restored)
            return Result<std::unique_ptr<RenderContext>, GraphicsError>::failure(restored.error());

        LOG_INFO("create swapchain");
        auto swapchain = Swapchain::create(window, *context, *device, swapchain_request);
        if(!swapchain)
            return Result<std::unique_ptr<RenderContext>, GraphicsError>::failure(
                swapchain.error());
        return Result<std::unique_ptr<RenderContext>, GraphicsError>::success(
            std::unique_ptr<RenderContext>(new RenderContext(
                std::move(context), std::move(device), std::move(swapchain).value())));
    }

    RenderContext::RenderContext(std::unique_ptr<Context> context, std::unique_ptr<Device> device,
        std::unique_ptr<Swapchain> swapchain)
        : m_context(std::move(context)), m_device(std::move(device)),
          m_swapchain(std::move(swapchain)) {}

    void RenderContext::wait_idle() const {
        m_device->wait_idle();
    }

    RenderContext::~RenderContext() {
        PROFILE_SCOPE("RenderContext::Destructor");
        LOG_INFO("destroy render context");
        m_device->wait_idle_for_shutdown();

        m_swapchain.reset();
        m_device.reset();
        m_context.reset();
    }
}
