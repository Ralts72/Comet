#pragma once
#include "common/export.h"
#include "core/window/window.h"
#include "render/renderer.h"

namespace Comet {
    class COMET_API Engine {
    public:
        Engine();
        ~Engine();

        void on_update();

        [[nodiscard]] Window* get_window() const { return m_window.get(); };
        [[nodiscard]] bool get_close_status() const { return m_should_close; };

    private:
        bool m_should_close = false;
        std::unique_ptr<Window> m_window;
        std::unique_ptr<Renderer> m_renderer;
    };
}
