#include "gameplay/root_motion.h"
#include "gameplay/navigation_system.h"
#include "animation/animation_data.h"
#include "physics/physics_access.h"
#include "scene/scene.h"
#include "scene/runtime_services.h"
#include <cmath>
#include <algorithm>

namespace Comet::Gameplay {
    namespace {
        Result<void, Error> warp_motion(Scene& scene, Entity model, Entity moving,
            const MotionWarpComponent& warp, const RootMotionComponent& root,
            const AnimationMotionStep& step, Math::Vec3& displacement, float& yaw) {
            using Applied = Result<void, Error>;
            if(!std::isfinite(warp.start) || !std::isfinite(warp.end) || warp.start < 0
                || warp.end > 1 || warp.start >= warp.end || !std::isfinite(warp.weight)
                || warp.weight < 0 || warp.weight > 1 || !std::isfinite(warp.max_distance)
                || warp.max_distance < 0 || !std::isfinite(warp.max_angle) || warp.max_angle < 0
                || warp.max_angle > 180)
                return Applied::failure({"Invalid motion warp window or limits"});
            if(step.sample.loop || warp.weight == 0
                || (!warp.state.empty() && warp.state != step.state
                    && warp.state != step.sample.clip))
                return Applied::success();
            const auto duration = animation_duration(step.data, step.sample);
            if(!duration)
                return Applied::failure({duration.error()});
            const double period = step.sample.normalized_time ? 1 : duration.value();
            const double start = warp.start * period, end = warp.end * period;
            const double from = std::max(step.from, start);
            const double to = std::min(step.to, end);
            if(from >= end || to <= from)
                return Applied::success();
            auto destination = scene.find_entity(warp.target);
            if(!destination || destination == moving || destination == model)
                return Applied::failure({"Motion warp requires a separate target entity"});
            auto sample = step.sample;
            sample.time = step.from;
            auto remaining = animation_root_motion(
                step.data, sample, end, {root.node, root.horizontal, root.vertical, root.rotation});
            if(!remaining)
                return Applied::failure({remaining.error()});
            const auto model_world = scene.get_world_matrix(model);
            const auto target_world = scene.get_world_matrix(destination);
            auto correction =
                Math::Vec3(target_world[3] - scene.get_world_matrix(moving)[3])
                - Math::Vec3(model_world * Math::Vec4(remaining.value().translation, 0));
            if(!root.horizontal)
                correction.x = correction.z = 0;
            if(!root.vertical)
                correction.y = 0;
            // 超出动作允许的对位范围时保持原轨迹，不瞬移远距离目标。
            if(Math::length(correction) > warp.max_distance)
                return Applied::success();
            const float fraction = float((to - from) / (end - from)) * warp.weight;
            displacement += correction * fraction;
            if(root.rotation) {
                auto forward = Math::Vec3(model_world[2]);
                auto desired = Math::Vec3(target_world[2]);
                forward.y = desired.y = 0;
                if(Math::length(forward) > 1e-5f && Math::length(desired) > 1e-5f) {
                    const float angle =
                        std::atan2(Math::cross(forward, desired).y, Math::dot(forward, desired));
                    const float correction_yaw =
                        std::remainder(angle - remaining.value().yaw, 2 * Math::PI);
                    if(std::abs(correction_yaw) <= Math::radians(warp.max_angle))
                        yaw += correction_yaw * fraction;
                }
            }
            return Applied::success();
        }

        Result<void, Error> move_transform(
            Scene& scene, Entity entity, Math::Vec3 displacement, float angle, Math::Vec3 axis) {
            if(displacement == Math::Vec3(0) && angle == 0)
                return Result<void, Error>::success();
            const auto parent_inverse = Math::affine_inverse(scene.get_parent_matrix(entity));
            auto transform = entity.get_component<TransformComponent>();
            if(angle == 0) {
                transform.translation += Math::Vec3(parent_inverse * Math::Vec4(displacement, 0));
            } else {
                auto world = scene.get_world_matrix(entity);
                const auto rotation = Math::rotate(Math::Mat4(1), angle, axis);
                for(int column = 0; column < 3; ++column)
                    world[column] = rotation * world[column];
                world[3] += Math::Vec4(displacement, 0);
                const auto parts = Math::decompose(parent_inverse * world);
                if(!parts || Math::length(parts->skew) > 1e-4f)
                    return Result<void, Error>::failure(
                        {"Root motion target transform contains shear"});
                transform.scale = parts->scale;
                transform.translation = parts->translation;
                transform.rotation = Math::degrees(Math::euler_angles(parts->rotation));
            }
            if(!entity.try_set_transform(transform))
                return Result<void, Error>::failure({"Cannot apply root motion transform"});
            return Result<void, Error>::success();
        }

    }
    Result<void, Error> apply_root_motion(Scene& scene, Entity model,
        const RootMotionComponent& settings, const AnimationMotionStep& step, double delta,
        const RuntimeServices& services) {
        const auto& motion = step.motion;
        auto target = settings.target ? scene.find_entity(settings.target) : model;
        if(!target)
            return Result<void, Error>::failure({"Root motion target is missing"});
        auto ancestor = model;
        while(ancestor && ancestor != target)
            ancestor = scene.get_parent(ancestor);
        if(!ancestor)
            return Result<void, Error>::failure(
                {"Root motion target must be the model or an ancestor"});
        const bool character = target.has_component<CharacterControllerComponent>();
        if(character && !target.get_component<CharacterControllerComponent>().enabled)
            return Result<void, Error>::success();
        if(target.has_component<RigidBodyComponent>()
            && target.get_component<RigidBodyComponent>().motion != BodyMotion::Kinematic)
            return Result<void, Error>::failure(
                {"Root motion cannot drive a static or dynamic rigid body"});
        const auto model_world = scene.get_world_matrix(model);
        const auto axis =
            Math::normalized_direction(Math::Vec3(model_world * Math::Vec4(motion.up, 0)));
        if(!axis || (character && Math::dot(*axis, Math::Vec3(0, 1, 0)) < .999f))
            return Result<void, Error>::failure(
                {"Character root motion requires an upright model"});
        if(delta <= 0)
            return Result<void, Error>::success();
        if(character && services.navigation
            && services.navigation->is_traversing_link(scene, target))
            return Result<void, Error>::success();
        auto displacement = Math::Vec3(model_world * Math::Vec4(motion.translation, 0));
        float yaw = motion.yaw;
        if(model.has_component<MotionWarpComponent>()
            && model.get_component<MotionWarpComponent>().enabled) {
            if(auto warped = warp_motion(scene, model, target,
                   model.get_component<MotionWarpComponent>(), settings, step, displacement, yaw);
                !warped)
                return warped;
        }
        if(character) {
            if(!services.physics
                || !services.physics->request_character_velocity(
                    target, {.velocity = displacement / float(delta),
                                .horizontal = settings.horizontal,
                                .vertical = settings.vertical}))
                return Result<void, Error>::failure({"Cannot submit character root motion"});
            return move_transform(scene, model, {}, yaw, *axis);
        }
        return move_transform(scene, target, displacement, yaw, *axis);
    }
}
