#pragma once

#include "common/export.h"

#include <array>
#include <cstdint>
#include <string>

namespace Comet {
    class COMET_API Config {
    public:
        struct Log {
            bool enable_file_logging = true;
            std::string level = "trace";
        };

        struct Window {
            int width = 1280;
            int height = 720;
            std::string title = "Comet Engine";
            bool fullscreen = false;
            bool resizable = true;
        };

        struct Vulkan {
            int surface_format = 50;
            int color_space = 0;
            int depth_format = 126;
            int present_mode = 0;
            std::uint32_t swapchain_image_count = 3;
            int msaa_samples = 4;
            bool enable_validation = true;
        };

        struct Render {
            std::uint32_t max_frames_in_flight = 2;
            std::array<float, 4> clear_color = {0.2f, 0.4f, 0.1f, 1.0f};
            bool enable_vsync = false;
        };

        Log log;
        Window window;
        Vulkan vulkan;
        Render render;
    };
}
