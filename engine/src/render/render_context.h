#pragma once
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "common/export.h"
#include "config/config.h"

namespace Comet {
    class COMET_API RenderContext {
    public:
        RenderContext(const Window& window, const Config::Vulkan& vulkan_config,
            const Config::Render& render_config);
        ~RenderContext();

        [[nodiscard]] Device& get_device() { return *m_device; }
        [[nodiscard]] const Device& get_device() const { return *m_device; }
        [[nodiscard]] Swapchain& get_swapchain() { return *m_swapchain; }
        [[nodiscard]] const Swapchain& get_swapchain() const { return *m_swapchain; }
        [[nodiscard]] Context& get_context() { return *m_context; }
        [[nodiscard]] const Context& get_context() const { return *m_context; }

        void wait_idle() const;

    private:
        std::unique_ptr<Context> m_context;
        std::unique_ptr<Device> m_device;
        std::unique_ptr<Swapchain> m_swapchain;
    };
}
