#include "render_context.h"
#include "common/logger.h"
#include "common/profiler.h"

namespace Comet {
    RenderContext::RenderContext(const Window& window) {
        PROFILE_SCOPE("RenderContext::Constructor");
        LOG_INFO("init graphics system");

        m_context = std::make_unique<Context>(window);

        LOG_INFO("create device");
        m_device = std::make_unique<Device>(m_context.get(), 1, 1);

        LOG_INFO("create swapchain");
        m_swapchain = std::make_unique<Swapchain>(m_context.get(), m_device.get());
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
