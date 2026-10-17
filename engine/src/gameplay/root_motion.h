#pragma once

#include "common/export.h"
#include "common/error.h"
#include "common/result.h"

namespace Comet {
    class Entity;
    class Scene;
    struct RootMotionComponent;
    struct AnimationMotionStep;
    struct RuntimeServices;
}

namespace Comet::Gameplay {
    // 角色提交物理速度；普通／运动学对象写变换，禁止覆盖动态刚体求解结果。
    [[nodiscard]] COMET_API Result<void, Error> apply_root_motion(Scene& scene, Entity model,
        const RootMotionComponent& settings, const AnimationMotionStep& step, double delta,
        const RuntimeServices& services);
}
