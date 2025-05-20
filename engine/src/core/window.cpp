#include "window.h"
#include "../common/log.h"

namespace Comet {
    Window::Window(const std::string& title, int width, int height): m_title(title), m_width(width), m_height(height) {
        m_window = SDL_CreateWindow(m_title.c_str(), m_width, m_height, SDL_WINDOW_VULKAN);
        if(!m_window) {
            LOG_ERROR("Failed to create window!");
        }
    }

    Window::~Window() {}
}
