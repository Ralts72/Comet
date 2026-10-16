#include "gameplay/root_motion.h"
#include "animation/animation_data.h"
#include "physics/physics_access.h"
#include "scene/scene.h"
#include "scene/runtime_services.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace Comet::Gameplay {
    namespace {
        Result<void, Error> move_transform(
            Scene& scene, Entity entity, Math::Vec3 displacement, float angle, Math::Vec3 axis) {
            if(displacement == Math::Vec3(0) && angle == 0)
                return Result<void, Error>::success();
            const auto parent_inverse = glm::affineInverse(scene.get_parent_matrix(entity));
            auto transform = entity.get_component<TransformComponent>();
            if(angle == 0) {
                transform.translation += Math::Vec3(parent_inverse * Math::Vec4(displacement, 0));
            } else {
                auto world = scene.get_world_matrix(entity);
                const auto rotation = glm::rotate(Math::Mat4(1), angle, axis);
                for(int column = 0; column < 3; ++column)
                    world[column] = rotation * world[column];
                world[3] += Math::Vec4(displacement, 0);
                Math::Quat orientation;
                Math::Vec3 skew;
                Math::Vec4 perspective;
                if(!glm::decompose(parent_inverse * world, transform.scale, orientation,
                       transform.translation, skew, perspective)
                    || Math::length(skew) > 1e-4f)
                    return Result<void, Error>::failure(
                        {"Root motion target transform contains shear"});
                transform.rotation = glm::degrees(glm::eulerAngles(orientation));
            }
            if(!entity.try_set_transform(transform))
                return Result<void, Error>::failure({"Cannot apply root motion transform"});
            return Result<void, Error>::success();
        }

    }
    Result<void, Error> apply_root_motion(Scene& scene, Entity model,
        const RootMotionComponent& settings, const AnimationMotion& motion, double delta,
        const RuntimeServices& services) {
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
        if(!axis || (character && glm::dot(*axis, Math::Vec3(0, 1, 0)) < .999f))
            return Result<void, Error>::failure(
                {"Character root motion requires an upright model"});
        if(delta <= 0)
            return Result<void, Error>::success();
        const auto displacement = Math::Vec3(model_world * Math::Vec4(motion.translation, 0));
        if(character) {
            if(!services.physics
                || !services.physics->request_character_velocity(
                    target, {.velocity = displacement / float(delta),
                                .horizontal = settings.horizontal,
                                .vertical = settings.vertical}))
                return Result<void, Error>::failure({"Cannot submit character root motion"});
            return move_transform(scene, model, {}, motion.yaw, *axis);
        }
        return move_transform(scene, target, displacement, motion.yaw, *axis);
    }
}
