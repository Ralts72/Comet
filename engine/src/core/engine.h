#pragma once
#include "common/export.h"
#include "common/singleton.h"
#include "core/window/window.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "graphics/render_pass.h"

namespace Comet {
    class COMET_API Engine: public Singleton<Engine, true> {
    public:
        Engine();

        ~Engine();

        void on_update();

        [[nodiscard]] Window* get_window() const { return m_window.get(); };
        [[nodiscard]] bool get_close_status() const { return m_should_close; };

    private:
        bool m_should_close = false;
        std::unique_ptr<Window> m_window;
        std::unique_ptr<Context> m_context;
        std::shared_ptr<Device> m_device;
        std::shared_ptr<Swapchain> m_swapchain;
        std::shared_ptr<RenderPass> m_render_pass;
    };
}
