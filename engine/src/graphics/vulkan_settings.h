#pragma once

#include "graphics/enums.h"

#include <cstdint>
#include <filesystem>

namespace Comet {
    struct VulkanSettings {
        Format surface_format = Format::B8G8R8A8_SRGB;
        ImageColorSpace color_space = ImageColorSpace::SrgbNonlinearKHR;
        Format depth_format = Format::D32_SFLOAT;
        PresentMode present_mode = PresentMode::Immediate;
        std::uint32_t swapchain_image_count = 3;
        SampleCount msaa_samples = SampleCount::Count4;
        bool enable_validation = false;
        // 本机启动上下文，不从开发者 Profile 读取；空路径禁用磁盘缓存。
        std::filesystem::path pipeline_cache_directory;
    };
}
