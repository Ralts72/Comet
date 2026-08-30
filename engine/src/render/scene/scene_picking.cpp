#include "render/scene/scene_picking.h"

#include "render/resource/mesh.h"

#include <cmath>
#include <vector>

namespace Comet {
    namespace {
        bool is_finite(const Math::Vec4 value) {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z)
                && std::isfinite(value.w);
        }

        std::optional<Ray> transform_ray_to_local(
            const Ray& world_ray,
            const Math::Mat4& model_matrix) {
            const Math::Mat4 inverse_model = Math::inverse(model_matrix);
            const Math::Vec4 local_origin = inverse_model
                * Math::Vec4(world_ray.origin, 1.0f);
            const Math::Vec4 local_direction = inverse_model
                * Math::Vec4(world_ray.direction, 0.0f);
            if(!is_finite(local_origin)
               || !is_finite(local_direction)
               || std::abs(local_origin.w) <= 0.000001f) {
                return std::nullopt;
            }

            const Ray ray{
                .origin = Math::Vec3(local_origin) / local_origin.w,
                .direction = Math::Vec3(local_direction)
            };
            return ray.is_valid()
                ? std::optional<Ray>(ray)
                : std::nullopt;
        }
    }

    std::optional<Ray> make_world_ray(
        const ViewProjectMatrix& view_project,
        const Math::Vec2u pixel,
        const Math::Vec2u render_resolution) {
        if(render_resolution.x == 0 || render_resolution.y == 0
           || pixel.x >= render_resolution.x
           || pixel.y >= render_resolution.y) {
            return std::nullopt;
        }

        const float normalized_x =
            (static_cast<float>(pixel.x) + 0.5f)
            / static_cast<float>(render_resolution.x);
        const float normalized_y =
            (static_cast<float>(pixel.y) + 0.5f)
            / static_cast<float>(render_resolution.y);
        const float ndc_x = normalized_x * 2.0f - 1.0f;
        const float ndc_y = normalized_y * 2.0f - 1.0f;
        const Math::Mat4 inverse_view_projection = Math::inverse(
            view_project.projection * view_project.view);
        Math::Vec4 near_point = inverse_view_projection
            * Math::Vec4(ndc_x, ndc_y, 0.0f, 1.0f);
        Math::Vec4 far_point = inverse_view_projection
            * Math::Vec4(ndc_x, ndc_y, 1.0f, 1.0f);
        if(!is_finite(near_point)
           || !is_finite(far_point)
           || std::abs(near_point.w) <= 0.000001f
           || std::abs(far_point.w) <= 0.000001f) {
            return std::nullopt;
        }
        near_point /= near_point.w;
        far_point /= far_point.w;

        const Math::Vec3 direction = Math::Vec3(far_point - near_point);
        const float direction_length = Math::length(direction);
        if(!std::isfinite(direction_length)
           || direction_length <= 0.000001f) {
            return std::nullopt;
        }
        return Ray{
            .origin = Math::Vec3(near_point),
            .direction = Math::normalize(direction)
        };
    }

    std::optional<ScenePickHit> pick_scene_candidates(
        const Ray& world_ray,
        const std::span<const ScenePickCandidate> candidates) {
        if(!world_ray.is_valid()) {
            return std::nullopt;
        }

        std::optional<ScenePickHit> closest;
        for(const ScenePickCandidate& candidate: candidates) {
            if(candidate.entity_id == INVALID_ENTITY_ID
               || !candidate.local_bounds.is_valid()) {
                continue;
            }
            const std::optional<Ray> local_ray = transform_ray_to_local(
                world_ray, candidate.model_matrix);
            if(!local_ray) {
                continue;
            }
            const std::optional<float> distance = intersect_ray_box(
                *local_ray, candidate.local_bounds);
            if(!distance) {
                continue;
            }
            constexpr float DISTANCE_EPSILON = 0.000001f;
            if(!closest
               || *distance < closest->distance - DISTANCE_EPSILON
               || (std::abs(*distance - closest->distance)
                       <= DISTANCE_EPSILON
                   && candidate.entity_id < closest->entity_id)) {
                closest = ScenePickHit{
                    .entity_id = candidate.entity_id,
                    .distance = *distance
                };
            }
        }
        return closest;
    }

    std::optional<ScenePickHit> pick_render_submission(
        const RenderSubmission& submission,
        const Math::Vec2u pixel,
        const Math::Vec2u render_resolution) {
        if(!submission.view_project_matrix) {
            return std::nullopt;
        }
        const std::optional<Ray> world_ray = make_world_ray(
            *submission.view_project_matrix, pixel, render_resolution);
        if(!world_ray) {
            return std::nullopt;
        }

        std::vector<ScenePickCandidate> candidates;
        candidates.reserve(submission.render_items.size());
        for(const ResolvedRenderItem& item: submission.render_items) {
            if(!item.mesh) {
                continue;
            }
            candidates.push_back({
                .entity_id = item.entity_id,
                .model_matrix = item.model_matrix,
                .local_bounds = item.mesh->get_local_bounds()
            });
        }
        return pick_scene_candidates(*world_ray, candidates);
    }
}
