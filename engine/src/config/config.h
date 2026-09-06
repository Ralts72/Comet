#pragma once

#include "common/export.h"
#include "graphics/enums.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Comet {
    class COMET_API Config {
    public:
        struct Log {
            bool enable_file_logging = true;
            std::string level = "trace";
        };

        struct Diagnostics {
            Log log;
            bool enable_profiler = false;
        };

        struct Window {
            int width = 1280;
            int height = 720;
            std::string title = "Comet Engine";
            bool fullscreen = false;
            bool resizable = true;
        };

        struct Vulkan {
            Format surface_format = Format::B8G8R8A8_SRGB;
            ImageColorSpace color_space = ImageColorSpace::SrgbNonlinearKHR;
            Format depth_format = Format::D32_SFLOAT;
            PresentMode present_mode = PresentMode::Immediate;
            std::uint32_t swapchain_image_count = 3;
            SampleCount msaa_samples = SampleCount::Count4;
            bool enable_validation = false;
            // 启动层提供本机路径；空值只使用内存缓存，不写入共享 YAML。
            std::filesystem::path pipeline_cache_directory;
        };

        struct Render {
            std::uint32_t max_frames_in_flight = 2;
            std::array<float, 4> clear_color = {0.2f, 0.4f, 0.1f, 1.0f};
            bool enable_vsync = false;
            float max_anisotropy = 1.0f;
            float exposure = 1.0f;
            float bloom_strength = 0.0f;
            float bloom_threshold = 1.0f;
        };

        Diagnostics diagnostics;
        Window window;
        Vulkan vulkan;
        Render render;
    };
}
