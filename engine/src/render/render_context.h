#pragma once
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "common/export.h"

namespace Comet {
    class COMET_API RenderContext {
    public:
        explicit RenderContext(const Window& window);
        ~RenderContext();

        [[nodiscard]] Device* get_device() const { return m_device.get(); }
        [[nodiscard]] Swapchain* get_swapchain() const { return m_swapchain.get(); }
        [[nodiscard]] Context *get_context() const { return m_context.get(); }

        void wait_idle() const;

    private:
        std::unique_ptr<Context> m_context;
        std::unique_ptr<Device> m_device;
        std::unique_ptr<Swapchain> m_swapchain;
    };
}