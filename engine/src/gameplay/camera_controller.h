#pragma once

#include "common/error.h"
#include "common/export.h"
#include "common/result.h"

namespace Comet {
    class Entity;
    class InputState;
    class PhysicsAccess;
    class Scene;
}

namespace Comet::Gameplay {
    // 配置归 CameraControllerComponent；借用场景和输入，不持有运行态。
    class COMET_API CameraController final {
    public:
        [[nodiscard]] static Result<void, Error> update(Scene& scene, Entity camera,
            const InputState& input, double delta_time, PhysicsAccess* physics = nullptr);
        [[nodiscard]] static bool wants_cursor_capture(
            Scene& scene, Entity camera, const InputState& input);
    };
}
