#include "scene/systems/camera_controller.h"
#include "scene/scene.h"
#include "physics/physics_access.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Comet {
    namespace {
        struct ControlledCamera {
            Entity entity;
            Entity parent;
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
                || controller.look_sensitivity < 0 || !std::isfinite(controller.look_speed)
                || controller.look_speed < 0)
                return std::nullopt;
            ControlledCamera result{camera};
            if(auto parent = scene.get_parent(camera)) {
                result.parent = parent;
                result.world_to_parent = Math::inverse(scene.get_world_matrix(parent));
                result.parent_pose =
                    parent.get_component<WorldTransformComponent>().pose_world_matrix;
                for(int column = 0; column < 4; ++column)
                    if(!Math::is_finite(result.world_to_parent[column])
                        || !Math::is_finite(result.parent_pose[column]))
                        return std::nullopt;
            }
            if(controller.mode == CameraControllerComponent::Mode::Orbit) {
                if(!result.parent || !std::isfinite(controller.orbit_distance)
                    || !std::isfinite(controller.orbit_height) || controller.orbit_height < 0
                    || !std::isfinite(controller.orbit_min_distance)
                    || controller.orbit_min_distance <= 0
                    || !std::isfinite(controller.orbit_max_distance)
                    || controller.orbit_max_distance < controller.orbit_min_distance
                    || !std::isfinite(controller.orbit_zoom_step) || controller.orbit_zoom_step < 0
                    || !std::isfinite(controller.collision_radius)
                    || controller.collision_radius <= 0)
                    return std::nullopt;
            } else if(controller.mode != CameraControllerComponent::Mode::Free)
                return std::nullopt;
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
        if(!std::isfinite(delta_time) || delta_time < 0)
            return Result<void, Error>::success();
        const auto target = controlled_camera(scene);
        if(!target)
            return Result<void, Error>::success();
        auto camera = target->entity;
        auto& controller = camera.get_component<CameraControllerComponent>();
        if(!input.focused() && controller.mode == CameraControllerComponent::Mode::Free)
            return Result<void, Error>::success();
        auto transform = camera.get_component<TransformComponent>();
        using Type = InputState::Action::Type;
        const std::pair<std::string_view, Type> expected[]{{"camera.move_x", Type::Axis},
            {"camera.move_y", Type::Axis}, {"camera.move_z", Type::Axis},
            {"camera.look", Type::Button}, {"camera.look_x", Type::Delta},
            {"camera.look_y", Type::Delta}, {"camera.look_rate_x", Type::Axis},
            {"camera.look_rate_y", Type::Axis}, {"camera.zoom", Type::Delta},
            {"camera.boost", Type::Button}, {"camera.zoom_axis", Type::Axis}};
        for(const auto& [name, type] : expected)
            if(const auto* value = input.action(name); value && value->type != type)
                return Result<void, Error>::failure(
                    {"Camera action has wrong type: " + std::string(name)});
        const auto value = [&](std::string_view name) {
            if(!input.focused())
                return 0.0f;
            if(const auto* action = input.action(name))
                return action->value;
            return 0.0f;
        };
        const auto* look = input.action("camera.look");
        auto turn = Math::Vec2(value("camera.look_rate_x"), value("camera.look_rate_y"))
                    * controller.look_speed * delta_time;
        // 本帧位移可能包含按下前的移动；开始拖动时跳过，下一帧再转向。
        const bool mouse_look = input.focused() && look && look->down && !look->pressed;
        if(mouse_look)
            turn += Math::Vec2(value("camera.look_x"), value("camera.look_y"))
                    * controller.look_sensitivity;
        if(mouse_look || turn.x != 0 || turn.y != 0) {
            transform.rotation.x =
                std::clamp(Math::wrap_degrees(transform.rotation.x) - turn.y, -89.0f, 89.0f);
            transform.rotation.y = Math::wrap_degrees(transform.rotation.y - turn.x);
        }

        const auto rotation = target->parent_pose
                              * Math::compose_trs(Math::Vec3(0), transform.rotation, Math::Vec3(1));
        const auto forward = -Math::Vec3(rotation[2]);
        if(controller.mode == CameraControllerComponent::Mode::Orbit) {
            const auto distance = std::clamp(
                controller.orbit_distance - value("camera.zoom") * controller.orbit_zoom_step
                    - value("camera.zoom_axis") * controller.move_speed * delta_time,
                controller.orbit_min_distance, controller.orbit_max_distance);
            float height_ratio = 1;
            if(context.services.physics) {
                if(const auto character =
                        context.services.physics->get_character_state(target->parent))
                    height_ratio = character->height_ratio;
            }
            const auto pivot = Math::Vec3(target->parent_pose[3])
                               + Math::Vec3(0, controller.orbit_height * height_ratio, 0);
            auto offset = -forward * distance;
            if(context.services.physics) {
                const auto fraction = context.services.physics->sweep_sphere(
                    target->parent, pivot, offset, controller.collision_radius);
                if(!fraction)
                    return Result<void, Error>::failure({"Orbit camera collision query failed"});
                if(*fraction < 1)
                    offset *= std::max(0.0f, *fraction - 0.01f / distance);
            }
            transform.translation =
                Math::Vec3(target->world_to_parent * Math::Vec4(pivot + offset, 1));
            if(!camera.try_set_transform(transform))
                return Result<void, Error>::failure({"Cannot update orbit camera transform"});
            controller.orbit_distance = distance;
            return Result<void, Error>::success();
        }

        const Math::Vec3 movement{
            value("camera.move_x"), value("camera.move_y"), value("camera.move_z")};

        const auto right = Math::Vec3(rotation[0]);
        auto direction = right * movement.x - forward * movement.z + Math::Vec3(0, movement.y, 0);
        direction += forward * value("camera.zoom_axis");
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
