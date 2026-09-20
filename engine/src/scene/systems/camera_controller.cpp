#include "scene/systems/camera_controller.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>

namespace Comet {
    Result<void, Error> CameraControllerSystem::update(Scene& scene, const Context& context) {
        const auto& input = context.input;
        const auto delta_time = static_cast<float>(context.delta_time);
        if(!input.focused || !std::isfinite(delta_time) || delta_time < 0)
            return Result<void, Error>::success();
        Entity camera;
        // 与 SceneResolver 一致：多个主相机时使用最小 EntityId，不回退到其他控制器。
        scene.each<const CameraComponent, const TransformComponent>(
            [&](Entity entity, const CameraComponent& candidate, const TransformComponent&) {
                if(candidate.primary && (!camera || entity.get_id() < camera.get_id()))
                    camera = entity;
            });
        if(!camera || !camera.has_component<CameraControllerComponent>())
            return Result<void, Error>::success();
        const auto& controller = camera.get_component<CameraControllerComponent>();
        if(!controller.enabled || !std::isfinite(controller.move_speed) || controller.move_speed < 0
            || !std::isfinite(controller.look_sensitivity) || controller.look_sensitivity < 0)
            return Result<void, Error>::success();

        auto& current = camera.get_component<TransformComponent>();
        auto transform = current;
        Math::Mat4 parent_pose(1);
        Math::Mat4 world_to_parent(1);
        if(auto parent = scene.get_parent(camera)) {
            world_to_parent = Math::inverse(scene.get_world_matrix(parent));
            parent_pose = parent.get_component<WorldTransformComponent>().pose_world_matrix;
            for(int column = 0; column < 4; ++column)
                if(!Math::is_finite(world_to_parent[column])
                    || !Math::is_finite(parent_pose[column]))
                    return Result<void, Error>::success();
        }
        using Key = Input::Key;

        const auto& look = input.mouse(Input::MouseButton::Right);
        // 本帧位移可能包含按下前的移动；开始拖动时跳过，下一帧再转向。
        if(look.down && !look.pressed) {
            transform.rotation.x =
                std::clamp(Math::wrap_degrees(transform.rotation.x)
                               - input.cursor_delta.y * controller.look_sensitivity,
                    -89.0f, 89.0f);
            transform.rotation.y = Math::wrap_degrees(
                transform.rotation.y - input.cursor_delta.x * controller.look_sensitivity);
        }

        Math::Vec3 movement{float(input.key(Key::D).down) - float(input.key(Key::A).down),
            float(input.key(Key::E).down) - float(input.key(Key::Q).down),
            float(input.key(Key::S).down) - float(input.key(Key::W).down)};
        const auto stick = [](float value) {
            const float magnitude = std::abs(value);
            if(magnitude <= 0.15f)
                return 0.0f;
            return std::copysign((magnitude - 0.15f) / 0.85f, value);
        };
        for(const auto& pad : input.gamepads) {
            if(!pad.connected)
                continue;
            using Axis = Input::GamepadAxis;
            movement.x += stick(pad.axis(Axis::LeftX));
            movement.z += stick(pad.axis(Axis::LeftY));
            movement.y += pad.axis(Axis::RightTrigger) - pad.axis(Axis::LeftTrigger);
            break;
        }

        const auto rotation =
            parent_pose * Math::compose_trs(Math::Vec3(0), transform.rotation, Math::Vec3(1));
        const auto right = Math::Vec3(rotation[0]);
        const auto forward = -Math::Vec3(rotation[2]);
        auto direction = right * movement.x - forward * movement.z + Math::Vec3(0, movement.y, 0);
        if(Math::length(direction) > 1)
            direction = Math::normalize(direction);
        float speed = controller.move_speed;
        if(input.key(Key::LeftShift).down)
            speed *= 2;
        const auto world_delta = direction * speed * delta_time
                                 + forward * input.scroll.y * controller.move_speed / 15.0f;
        transform.translation += Math::Vec3(world_to_parent * Math::Vec4(world_delta, 0));
        if(Math::is_finite(transform.translation) && Math::is_finite(transform.rotation))
            current = transform;
        return Result<void, Error>::success();
    }
}
