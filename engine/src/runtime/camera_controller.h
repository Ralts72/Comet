#pragma once

#include "core/input.h"

namespace Comet {
    class Scene;

    // 只更新当前主相机上启用的控制组件；调用方负责运行模式和输入归属。
    COMET_API void update_camera_controller(
        Scene& scene, const Input::Frame& input, float delta_time);
}
