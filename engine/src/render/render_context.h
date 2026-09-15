#pragma once
#include "common/export.h"
#include "config/config.h"
#include "graphics/result.h"

#include <memory>

namespace Comet {
    class Context;
    class Device;
    class Swapchain;
    class Window;
    class COMET_API RenderContext {
    public:
        static Result<std::unique_ptr<RenderContext>, GraphicsError> create(const Window& window,
            const Config::Vulkan& vulkan_config, const Config::Render& render_config);
        ~RenderContext();

        [[nodiscard]] Device& get_device() { return *m_device; }
        [[nodiscard]] const Device& get_device() const { return *m_device; }
        [[nodiscard]] Swapchain& get_swapchain() { return *m_swapchain; }
        [[nodiscard]] const Swapchain& get_swapchain() const { return *m_swapchain; }
        [[nodiscard]] Context& get_context() { return *m_context; }
        [[nodiscard]] const Context& get_context() const { return *m_context; }

        void wait_idle() const;

    private:
        RenderContext(std::unique_ptr<Context> context, std::unique_ptr<Device> device,
            std::unique_ptr<Swapchain> swapchain);
        std::unique_ptr<Context> m_context;
        std::unique_ptr<Device> m_device;
        std::unique_ptr<Swapchain> m_swapchain;
    };
}
