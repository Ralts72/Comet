#include "scene/systems/camera_controller.h"
#include "gameplay/camera_controller.h"
#include "scene/scene.h"

namespace Comet {
    namespace {
        Entity primary_camera(Scene& scene) {
            Entity camera;
            // 与 SceneResolver 一致；选中主相机后不回退到其他控制器。
            scene.each<const CameraComponent, const TransformComponent>(
                [&](Entity entity, const CameraComponent& candidate, const TransformComponent&) {
                    if(candidate.primary && (!camera || entity.get_id() < camera.get_id()))
                        camera = entity;
                });
            return camera;
        }
    }

    bool CameraControllerSystem::wants_cursor_capture(Scene& scene, const InputState& input) const {
        return Gameplay::CameraController::wants_cursor_capture(
            scene, primary_camera(scene), input);
    }

    Result<void, Error> CameraControllerSystem::update(Scene& scene, const Context& context) {
        return Gameplay::CameraController::update(scene, primary_camera(scene), context.input,
            context.delta_time, context.services.physics);
    }
}
