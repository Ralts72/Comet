#include "viewport/collider_overlay.h"
#include "render/debug/line_draw_list.h"
#include "scene/scene.h"

namespace CometEditor {
    namespace {
        Comet::Math::Vec4 collider_color(const Comet::Entity entity,
            const Comet::ColliderComponent& collider, const Comet::TransformComponent& transform) {
            using namespace Comet;
            const bool positive_scale = glm::all(glm::greaterThan(transform.scale, Math::Vec3(0)));
            const bool uniform_scale =
                transform.scale.x == transform.scale.y && transform.scale.x == transform.scale.z;
            if(entity.get_component<RelationshipComponent>().parent != INVALID_ENTITY_ID
                || !positive_scale || (collider.shape == ColliderShape::Sphere && !uniform_scale))
                return {1, 0.2f, 0.2f, 1};
            if(!entity.has_component<RigidBodyComponent>())
                return {0.6f, 0.6f, 0.6f, 1};
            if(collider.is_trigger)
                return {0.1f, 0.9f, 0.9f, 1};
            switch(entity.get_component<RigidBodyComponent>().motion) {
                case BodyMotion::Static:
                    return {0.25f, 0.55f, 1, 1};
                case BodyMotion::Dynamic:
                    return {0.2f, 1, 0.3f, 1};
                case BodyMotion::Kinematic:
                    return {0.8f, 0.4f, 1, 1};
            }
            return {1, 0.2f, 0.2f, 1};
        }
    }

    void append_collider_outlines(Comet::Scene& scene, Comet::LineDrawList& lines) {
        scene.each<const Comet::TransformComponent, const Comet::ColliderComponent>(
            [&](const Comet::Entity entity, const Comet::TransformComponent& transform,
                const Comet::ColliderComponent& collider) {
                const auto color = collider_color(entity, collider, transform);
                const auto& matrix = scene.get_render_transform(entity).world_matrix;
                switch(collider.shape) {
                    case Comet::ColliderShape::Box:
                        if(Comet::Math::is_finite(collider.half_extents)
                            && glm::all(
                                glm::greaterThan(collider.half_extents, Comet::Math::Vec3(0))))
                            static_cast<void>(lines.add_box({.minimum = -collider.half_extents,
                                                                .maximum = collider.half_extents},
                                matrix, color));
                        break;
                    case Comet::ColliderShape::Sphere:
                        static_cast<void>(lines.add_sphere(collider.radius, matrix, color));
                        break;
                }
            });
    }
}
