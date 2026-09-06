#pragma once

#include "common/export.h"
#include "core/geometry.h"
#include "render/scene/render_submission.h"

#include <optional>
#include <span>

namespace Comet {
    struct ScenePickCandidate {
        EntityId entity_id = INVALID_ENTITY_ID;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        BoundingBox local_bounds;
    };

    struct ScenePickHit {
        EntityId entity_id = INVALID_ENTITY_ID;
        // 射线参数；在 make_world_ray() 中表示距近裁剪面的世界距离。
        float distance = 0.0f;
    };

    // 像素坐标以显示纹理的左上角为原点。
    [[nodiscard]] COMET_API std::optional<Ray> make_world_ray(
        const ViewProjectMatrix& view_project, Math::Vec2u pixel,
        Math::Vec2u render_resolution);

    [[nodiscard]] COMET_API std::optional<ScenePickHit> pick_scene_candidates(
        const Ray& world_ray, std::span<const ScenePickCandidate> candidates);

    [[nodiscard]] COMET_API std::optional<ScenePickHit> pick_render_submission(
        const RenderSubmission& submission, Math::Vec2u pixel,
        Math::Vec2u render_resolution);
}
