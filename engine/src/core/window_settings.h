#pragma once

#include <string>

namespace Comet {
    struct WindowSettings {
        int width = 1280;
        int height = 720;
        std::string title = "Comet";
        bool fullscreen = false;
        bool resizable = true;
    };
}
