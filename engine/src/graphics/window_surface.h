#pragma once

#include "graphics/result.h"
#include "graphics/vk_common.h"

#include <span>

namespace Comet {
    class Window;

    namespace Detail {
        // 借用窗口后端的扩展名；Window 存活期间有效。
        Result<std::span<const char* const>, GraphicsError> window_instance_extensions();
        Result<vk::UniqueSurfaceKHR, GraphicsError> create_window_surface(
            vk::Instance instance, const Window& window);
    }
}
