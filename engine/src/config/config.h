#pragma once

#include "common/export.h"
#include "core/math_utils.h"
#include "graphics/enums.h"
#include "render/post_process.h"

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
            // 本机启动上下文，不从共享 YAML 读取；空路径禁用磁盘缓存。
            std::filesystem::path pipeline_cache_directory;
        };

        struct Render {
            static constexpr Format SCENE_COLOR_FORMAT = Format::R16G16B16A16_SFLOAT;
            OutputMode output_mode = OutputMode::Sdr;
            // 扩展线性输出峰值相对于 SDR 白色的倍数，不代表显示器实测能力。
            float hdr_headroom = 4.0f;
            PostProcessSettings post_process;
            std::uint32_t max_frames_in_flight = 2;
            Math::Vec4 clear_color{0.2f, 0.4f, 0.1f, 1.0f};
            bool enable_vsync = false;
            float max_anisotropy = 1.0f;
        };

        Diagnostics diagnostics;
        Window window;
        Vulkan vulkan;
        Render render;
    };
}
