#include "window.h"

namespace Comet {
    Window::Window(const std::string& title, const int width, const int height): m_title(title), m_width(width), m_height(height) {
        m_window = SDL_CreateWindow(m_title.c_str(), m_width, m_height, SDL_WINDOW_VULKAN);
        if(!m_window) {
            LOG_ERROR("Failed to create window!");
            std::cout << SDL_GetError() << std::endl;
        }
    }
}
