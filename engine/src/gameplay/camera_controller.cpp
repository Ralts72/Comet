#include "gameplay/camera_controller.h"
#include "input/input_state.h"
#include "scene/scene.h"
#include "physics/physics_access.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Comet::Gameplay {
    namespace {
        struct ControlledCamera {
            Entity parent;
            Math::Mat4 parent_pose{1};
            Math::Mat4 world_to_parent{1};
        };

        std::optional<ControlledCamera> controlled_camera(Scene& scene, const Entity camera) {
            if(!scene.is_valid(camera) || !camera.has_component<CameraComponent>()
                || !camera.has_component<CameraControllerComponent>()
                || !camera.has_component<TransformComponent>())
                return std::nullopt;
            const auto& controller = camera.get_component<CameraControllerComponent>();
            if(!controller.enabled || !std::isfinite(controller.move_speed)
                || controller.move_speed < 0 || !std::isfinite(controller.look_sensitivity)
                || controller.look_sensitivity < 0 || !std::isfinite(controller.look_speed)
                || controller.look_speed < 0)
                return std::nullopt;
            ControlledCamera result;
            if(auto parent = scene.get_parent(camera)) {
                result.parent = parent;
                result.world_to_parent = Math::inverse(scene.get_parent_matrix(camera));
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
        struct CameraInput {
            Math::Vec3 movement{};
            Math::Vec2 look_delta{};
            Math::Vec2 look_rate{};
            InputState::Action look;
            float zoom = 0;
            float zoom_axis = 0;
            bool boost = false;
        };

        Result<CameraInput> read_camera_input(const InputState& input) {
            using Type = InputState::Action::Type;
            CameraInput result;
            const struct Value {
                std::string_view name;
                Type type;
                float* target;
            } values[]{{"camera.move_x", Type::Axis, &result.movement.x},
                {"camera.move_y", Type::Axis, &result.movement.y},
                {"camera.move_z", Type::Axis, &result.movement.z},
                {"camera.look_x", Type::Delta, &result.look_delta.x},
                {"camera.look_y", Type::Delta, &result.look_delta.y},
                {"camera.look_rate_x", Type::Axis, &result.look_rate.x},
                {"camera.look_rate_y", Type::Axis, &result.look_rate.y},
                {"camera.zoom", Type::Delta, &result.zoom},
                {"camera.zoom_axis", Type::Axis, &result.zoom_axis}};
            for(const auto& [name, type, target] : values) {
                const auto action = input.read_action(name, type, false);
                if(!action)
                    return Result<CameraInput>::failure(action.error());
                *target = action.value().value;
            }
            const auto look = input.read_action("camera.look", Type::Button, false);
            if(!look)
                return Result<CameraInput>::failure(look.error());
            const auto boost = input.read_action("camera.boost", Type::Button, false);
            if(!boost)
                return Result<CameraInput>::failure(boost.error());
            result.look = look.value();
            result.boost = boost.value().value != 0;
            if(!input.focused())
                return Result<CameraInput>::success({});
            return Result<CameraInput>::success(result);
        }
    }

    bool CameraController::wants_cursor_capture(
        Scene& scene, const Entity camera, const InputState& input) {
        const auto look = input.read_action("camera.look", InputState::Action::Type::Button, false);
        return input.focused() && input.physical().pointer_enabled
               && controlled_camera(scene, camera).has_value()
               && (camera.get_component<CameraControllerComponent>().capture_mouse
                   || (look && look.value().down));
    }

    Result<void, Error> CameraController::update(Scene& scene, Entity camera,
        const InputState& input, const double elapsed, PhysicsAccess* physics) {
        const auto delta_time = static_cast<float>(elapsed);
        if(!std::isfinite(delta_time) || delta_time < 0)
            return Result<void, Error>::success();
        const auto target = controlled_camera(scene, camera);
        if(!target)
            return Result<void, Error>::success();
        auto& controller = camera.get_component<CameraControllerComponent>();
        if(!input.focused() && controller.mode == CameraControllerComponent::Mode::Free)
            return Result<void, Error>::success();
        auto transform = camera.get_component<TransformComponent>();
        const auto read = read_camera_input(input);
        if(!read)
            return Result<void, Error>::failure({read.error()});
        const auto& controls = read.value();
        auto turn = controls.look_rate * controller.look_speed * delta_time;
        // 本帧位移可能包含按下前的移动；开始拖动时跳过，下一帧再转向。
        const bool mouse_look =
            input.physical().pointer_enabled
            && (controller.capture_mouse || (controls.look.down && !controls.look.pressed));
        if(mouse_look)
            turn += controls.look_delta * controller.look_sensitivity;
        if(mouse_look || turn.x != 0 || turn.y != 0) {
            transform.rotation.x =
                std::clamp(Math::wrap_degrees(transform.rotation.x) - turn.y, -89.0f, 89.0f);
            transform.rotation.y = Math::wrap_degrees(transform.rotation.y - turn.x);
        }

        const auto rotation = target->parent_pose
                              * Math::compose_trs(Math::Vec3(0), transform.rotation, Math::Vec3(1));
        const auto basis = Math::view_basis(-Math::Vec3(rotation[2]), Math::Vec3(rotation[1]));
        if(!basis)
            return Result<void, Error>::failure({"Invalid camera orientation"});
        const auto forward = -(*basis)[2];
        if(controller.mode == CameraControllerComponent::Mode::Orbit) {
            const auto distance =
                std::clamp(controller.orbit_distance - controls.zoom * controller.orbit_zoom_step
                               - controls.zoom_axis * controller.move_speed * delta_time,
                    controller.orbit_min_distance, controller.orbit_max_distance);
            float height_ratio = 1;
            if(physics) {
                if(const auto character = physics->get_character_state(target->parent))
                    height_ratio = character->height_ratio;
            }
            const auto pivot = Math::Vec3(target->parent_pose[3])
                               + Math::Vec3(0, controller.orbit_height * height_ratio, 0);
            auto offset = -forward * distance;
            if(physics) {
                const auto fraction = physics->sweep_sphere(
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

        const auto movement = controls.movement;

        const auto right = (*basis)[0];
        auto direction = right * movement.x - forward * movement.z + Math::Vec3(0, movement.y, 0);
        direction += forward * controls.zoom_axis;
        direction = Math::clamp_unit_length(direction);
        float speed = controller.move_speed;
        if(controls.boost)
            speed *= 2;
        const auto world_delta = direction * speed * delta_time
                                 + forward * controls.zoom * controller.move_speed / 15.0f;
        transform.translation += Math::Vec3(target->world_to_parent * Math::Vec4(world_delta, 0));
        if(Math::is_finite(transform.translation) && Math::is_finite(transform.rotation)
            && !camera.try_set_transform(transform))
            return Result<void, Error>::failure({"Cannot update camera transform"});
        return Result<void, Error>::success();
    }
}
