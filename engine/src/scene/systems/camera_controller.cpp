#include "scene/systems/camera_controller.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Comet {
    namespace {
        struct ControlledCamera {
            Entity entity;
            Math::Mat4 parent_pose{1};
            Math::Mat4 world_to_parent{1};
        };

        std::optional<ControlledCamera> controlled_camera(Scene& scene) {
            Entity camera;
            // 与 SceneResolver 一致：多个主相机时使用最小 EntityId，不回退到其他控制器。
            scene.each<const CameraComponent, const TransformComponent>(
                [&](Entity entity, const CameraComponent& candidate, const TransformComponent&) {
                    if(candidate.primary && (!camera || entity.get_id() < camera.get_id()))
                        camera = entity;
                });
            if(!camera || !camera.has_component<CameraControllerComponent>())
                return std::nullopt;
            const auto& controller = camera.get_component<CameraControllerComponent>();
            if(!controller.enabled || !std::isfinite(controller.move_speed)
                || controller.move_speed < 0 || !std::isfinite(controller.look_sensitivity)
                || controller.look_sensitivity < 0)
                return std::nullopt;
            ControlledCamera result{camera};
            if(auto parent = scene.get_parent(camera)) {
                result.world_to_parent = Math::inverse(scene.get_world_matrix(parent));
                result.parent_pose =
                    parent.get_component<WorldTransformComponent>().pose_world_matrix;
                for(int column = 0; column < 4; ++column)
                    if(!Math::is_finite(result.world_to_parent[column])
                        || !Math::is_finite(result.parent_pose[column]))
                        return std::nullopt;
            }
            return result;
        }
    }

    bool CameraControllerSystem::wants_cursor_capture(Scene& scene, const InputState& input) const {
        const auto* look = input.action("camera.look");
        return input.focused() && input.physical().pointer_enabled && look
               && look->type == InputState::Action::Type::Button && look->down
               && controlled_camera(scene).has_value();
    }

    Result<void, Error> CameraControllerSystem::update(Scene& scene, const Context& context) {
        const auto& input = context.input;
        const auto delta_time = static_cast<float>(context.delta_time);
        if(!input.focused() || !std::isfinite(delta_time) || delta_time < 0)
            return Result<void, Error>::success();
        const auto target = controlled_camera(scene);
        if(!target)
            return Result<void, Error>::success();
        const auto camera = target->entity;
        const auto& controller = camera.get_component<CameraControllerComponent>();
        auto transform = camera.get_component<TransformComponent>();
        using Type = InputState::Action::Type;
        const std::pair<std::string_view, Type> expected[]{{"camera.move_x", Type::Axis},
            {"camera.move_y", Type::Axis}, {"camera.move_z", Type::Axis},
            {"camera.look", Type::Button}, {"camera.look_x", Type::Delta},
            {"camera.look_y", Type::Delta}, {"camera.zoom", Type::Delta},
            {"camera.boost", Type::Button}};
        for(const auto& [name, type] : expected)
            if(const auto* value = input.action(name); value && value->type != type)
                return Result<void, Error>::failure(
                    {"Camera action has wrong type: " + std::string(name)});
        const auto value = [&](std::string_view name) {
            if(const auto* action = input.action(name))
                return action->value;
            return 0.0f;
        };
        const auto* look = input.action("camera.look");
        // 本帧位移可能包含按下前的移动；开始拖动时跳过，下一帧再转向。
        if(look && look->down && !look->pressed) {
            transform.rotation.x =
                std::clamp(Math::wrap_degrees(transform.rotation.x)
                               - value("camera.look_y") * controller.look_sensitivity,
                    -89.0f, 89.0f);
            transform.rotation.y = Math::wrap_degrees(
                transform.rotation.y - value("camera.look_x") * controller.look_sensitivity);
        }

        const Math::Vec3 movement{
            value("camera.move_x"), value("camera.move_y"), value("camera.move_z")};

        const auto rotation = target->parent_pose
                              * Math::compose_trs(Math::Vec3(0), transform.rotation, Math::Vec3(1));
        const auto right = Math::Vec3(rotation[0]);
        const auto forward = -Math::Vec3(rotation[2]);
        auto direction = right * movement.x - forward * movement.z + Math::Vec3(0, movement.y, 0);
        if(Math::length(direction) > 1)
            direction = Math::normalize(direction);
        float speed = controller.move_speed;
        if(value("camera.boost") != 0)
            speed *= 2;
        const auto world_delta = direction * speed * delta_time
                                 + forward * value("camera.zoom") * controller.move_speed / 15.0f;
        transform.translation += Math::Vec3(target->world_to_parent * Math::Vec4(world_delta, 0));
        if(Math::is_finite(transform.translation) && Math::is_finite(transform.rotation)
            && !camera.try_set_transform(transform))
            return Result<void, Error>::failure({"Cannot update camera transform"});
        return Result<void, Error>::success();
    }
}
