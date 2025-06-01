#pragma once
#include "../pch.h"

namespace Comet {
    class Window {
    public:
        Window(const std::string& title, int width, int height);

        ~Window() = default;

        [[nodiscard]] SDL_Window* getWindow() const { return m_window; }

        [[nodiscard]] Vec2 getSize() const;

        bool isMinimize() const noexcept;

    private:
        SDL_Window* m_window;
        std::string m_title;
        int m_width;
        int m_height;
    };
}
