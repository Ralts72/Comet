#include "gameplay/character_controller.h"
#include "input/input_state.h"
#include "physics/physics_access.h"
#include "scene/scene.h"

#include <cmath>
#include <string>
#include <utility>

namespace Comet::Gameplay {
    namespace {
        Math::Mat3 pose_rotation(Scene& scene, const Entity entity) {
            (void)scene.get_world_matrix(entity);
            return Math::Mat3(entity.get_component<WorldTransformComponent>().pose_world_matrix);
        }

        Math::Vec2 relative_movement(
            Scene& scene, const Entity reference, const Math::Vec2 direction) {
            const auto rotation = pose_rotation(scene, reference);
            Math::Vec2 backward{rotation[2].x, rotation[2].z};
            // 垂直俯视时由相机右轴确定平面方向，避免归一化近零向量。
            if(glm::dot(backward, backward) < 0.000001f)
                backward = {-rotation[0].z, rotation[0].x};
            backward = glm::normalize(backward);
            const Math::Vec2 right{backward.y, -backward.x};
            return right * direction.x + backward * direction.y;
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
            const auto* horizontal = input.action(move_x);
            const auto* vertical = input.action(move_z);
            const auto* jumping = input.action(jump);
            for(const auto& [name, action] :
                {std::pair{move_x, horizontal}, {move_z, vertical}, {jump, jumping}}) {
                if(!action)
                    return Result<bool>::failure(
                        "Unknown character input action: " + std::string(name));
            }
            if(horizontal->type != InputState::Action::Type::Axis
                || vertical->type != InputState::Action::Type::Axis)
                return Result<bool>::failure("Character movement requires two axis actions");
            if(jumping->type != InputState::Action::Type::Button)
                return Result<bool>::failure("Character jump requires a button action");
            direction = {horizontal->value, vertical->value};
            jump_requested = jumping->pressed && state->grounded;
            for(const auto& [name, target] :
                {std::pair{run, &running}, std::pair{crouch, &crouching}}) {
                if(name.empty())
                    continue;
                const auto* action = input.action(name);
                if(!action || action->type != InputState::Action::Type::Button)
                    return Result<bool>::failure(
                        "Character run/crouch requires a button action: " + std::string(name));
                *target = action->down;
            }
        }
        if(!Math::is_finite(direction))
            return Result<bool>::failure("Character movement must be finite");
        if(direction != Math::Vec2(0)) {
            if(reference)
                direction = relative_movement(scene, reference, direction);
            if(visual && !visual.try_set_transform(facing_transform(scene, visual, direction)))
                return Result<bool>::failure("Cannot update character facing transform");
        }
        if(!physics.request_character_move(entity, {.direction = direction,
                                                       .jump = jump_requested,
                                                       .run = running,
                                                       .crouch = crouching}))
            return Result<bool>::failure("Cannot submit character movement to the active runtime");
        return Result<bool>::success(jump_requested);
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
