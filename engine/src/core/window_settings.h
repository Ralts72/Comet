#pragma once

#include <string>

namespace Comet {
    enum class WindowMode { Windowed, Borderless, Fullscreen };
    struct WindowSettings {
        int width = 960;
        int height = 720;
        std::string title = "Comet";
        WindowMode mode = WindowMode::Windowed;
        bool resizable = true;
        bool maximized = false;
    };
}
