#include "gameplay/character_controller.h"
#include "input/input_state.h"
#include "physics/physics_access.h"
#include "scene/scene.h"

#include <cmath>
#include <utility>

namespace Comet::Gameplay {
    namespace {
        Math::Mat3 pose_rotation(Scene& scene, const Entity entity) {
            (void)scene.get_world_matrix(entity);
            return Math::Mat3(entity.get_component<WorldTransformComponent>().pose_world_matrix);
        }

        Result<Math::Vec2> relative_movement(
            Scene& scene, const Entity reference, const Math::Vec2 direction) {
            const auto rotation = pose_rotation(scene, reference);
            Math::Vec3 forward{-rotation[2].x, 0, -rotation[2].z};
            // 垂直俯视时由相机右轴确定平面方向，避免归一化近零向量。
            if(glm::dot(forward, forward) < 0.000001f)
                forward = {rotation[0].z, 0, -rotation[0].x};
            const auto basis = Math::view_basis(forward, {0, 1, 0});
            if(!basis)
                return Result<Math::Vec2>::failure(
                    "Character movement reference has invalid orientation");
            const auto movement = *basis * Math::Vec3(direction.x, 0, direction.y);
            return Result<Math::Vec2>::success({movement.x, movement.z});
        }

        TransformComponent facing_transform(
            Scene& scene, const Entity entity, const Math::Vec2 direction) {
            const auto yaw = std::atan2(-direction.x, -direction.y) * Math::RAD_TO_DEG;
            Math::Mat3 rotation(Math::compose_trs(Math::Vec3(0), {0, yaw, 0}, Math::Vec3(1)));
            if(const auto parent = scene.get_parent(entity))
                rotation = glm::transpose(pose_rotation(scene, parent)) * rotation;
            auto transform = entity.get_component<TransformComponent>();
            transform.rotation = Math::wrap_degrees(
                glm::degrees(glm::eulerAngles(glm::normalize(glm::quat_cast(rotation)))));
            return transform;
        }
    }

    Result<bool> CharacterController::update(Scene& scene, Entity entity, const InputState& input,
        PhysicsAccess& physics, const bool enabled) const {
        if(!scene.is_valid(entity) || !physics.is_bound_to(scene))
            return Result<bool>::failure("Character control requires the active physics scene");
        const auto state = physics.get_character_state(entity);
        if(!state)
            return Result<bool>::failure("Character control needs an enabled Character Controller");

        const auto reference = scene.find_entity(movement_reference);
        if(movement_reference && !reference)
            return Result<bool>::failure("Character movement reference is missing");
        const auto visual = scene.find_entity(facing);
        if(facing && !visual)
            return Result<bool>::failure("Character facing entity is missing");
        if(visual
            && (visual.has_component<RigidBodyComponent>()
                || visual.has_component<CharacterControllerComponent>()))
            return Result<bool>::failure("Character facing must target an entity without physics");

        Math::Vec2 direction{0};
        bool jump_requested = false;
        bool running = false;
        bool crouching = false;
        if(enabled && input.focused()) {
            using Type = InputState::Action::Type;
            const auto horizontal = input.read_action(move_x, Type::Axis);
            if(!horizontal)
                return Result<bool>::failure(horizontal.error());
            const auto vertical = input.read_action(move_z, Type::Axis);
            if(!vertical)
                return Result<bool>::failure(vertical.error());
            const auto jumping = input.read_action(jump, Type::Button);
            if(!jumping)
                return Result<bool>::failure(jumping.error());
            direction = {horizontal.value().value, vertical.value().value};
            jump_requested = jumping.value().pressed && state->grounded;
            for(const auto& [name, target] :
                {std::pair{run, &running}, std::pair{crouch, &crouching}}) {
                if(name.empty())
                    continue;
                const auto action = input.read_action(name, Type::Button);
                if(!action)
                    return Result<bool>::failure(action.error());
                *target = action.value().down;
            }
        }
        if(direction != Math::Vec2(0)) {
            if(reference) {
                const auto movement = relative_movement(scene, reference, direction);
                if(!movement)
                    return Result<bool>::failure(movement.error());
                direction = movement.value();
            }
            if(auto faced = face_movement(scene, facing, direction); !faced)
                return Result<bool>::failure(faced.error());
        }
        if(!physics.request_character_move(entity, {.direction = direction,
                                                       .jump = jump_requested,
                                                       .run = running,
                                                       .crouch = crouching}))
            return Result<bool>::failure("Cannot submit character movement to the active runtime");
        return Result<bool>::success(jump_requested);
    }

    Result<void> CharacterController::face_movement(
        Scene& scene, EntityUuid uuid, Math::Vec2 direction) {
        if(!uuid || direction == Math::Vec2(0))
            return Result<void>::success();
        const auto visual = scene.find_entity(uuid);
        if(!visual || visual.has_component<RigidBodyComponent>()
            || visual.has_component<CharacterControllerComponent>() || !Math::is_finite(direction))
            return Result<void>::failure(
                "Character facing requires a nonphysical visual entity and finite direction");
        if(!visual.try_set_transform(facing_transform(scene, visual, direction)))
            return Result<void>::failure("Cannot update character facing transform");
        return Result<void>::success();
    }

    Result<CharacterController::State> CharacterController::get_state(
        const Entity entity, const PhysicsAccess& physics) {
        const auto source = physics.get_character_state(entity);
        if(!source)
            return Result<State>::failure("Character state needs an active Character Controller");
        State state{.grounded = source->grounded,
            .crouched = source->crouched,
            .speed = std::hypot(source->relative_velocity.x, source->relative_velocity.z),
            .vertical_speed = source->relative_velocity.y,
            .height_ratio = source->height_ratio};
        if(!state.grounded)
            state.motion = state.vertical_speed > 0.05f ? Motion::Jumping : Motion::Falling;
        else if(state.crouched)
            state.motion = Motion::Crouching;
        else if(state.speed > 0.05f)
            state.motion = source->running ? Motion::Running : Motion::Walking;
        return Result<State>::success(state);
    }

    std::string_view CharacterController::State::name() const {
        switch(motion) {
            case Motion::Idle:
                return "idle";
            case Motion::Walking:
                return "walk";
            case Motion::Running:
                return "run";
            case Motion::Crouching:
                return "crouch";
            case Motion::Jumping:
                return "jump";
            case Motion::Falling:
                return "fall";
        }
        return "unknown";
    }
}
