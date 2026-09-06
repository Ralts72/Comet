#include "render/scene/scene_extractor.h"

#include "scene/components.h"
#include "scene/scene.h"

namespace Comet {
    RenderScene SceneExtractor::extract(Scene& scene) {
        scene.update_world_transforms();

        RenderScene render_scene;

        const auto camera_view = scene.m_registry.view<IdComponent, TransformComponent,
            WorldTransformComponent, CameraComponent>();
        render_scene.cameras.reserve(camera_view.size_hint());
        for(const entt::entity handle : camera_view) {
            const auto& [id] = camera_view.get<IdComponent>(handle);
            const auto& world_transform =
                camera_view.get<WorldTransformComponent>(handle);
            const auto& [primary, fov, near_clip, far_clip] =
                camera_view.get<CameraComponent>(handle);

            render_scene.cameras.push_back({.entity_id = id,
                .primary = primary,
                .view_matrix = Math::inverse(world_transform.pose_world_matrix),
                .fov_degrees = fov,
                .near_clip = near_clip,
                .far_clip = far_clip});
        }

        const auto view = scene.m_registry.view<IdComponent, TransformComponent,
            WorldTransformComponent, MeshRendererComponent>();
        render_scene.render_items.reserve(view.size_hint());

        for(const entt::entity handle : view) {
            const auto& [id] = view.get<IdComponent>(handle);
            const auto& world_transform = view.get<WorldTransformComponent>(handle);
            const auto& [mesh, material] = view.get<MeshRendererComponent>(handle);

            render_scene.render_items.push_back({.entity_id = id,
                .model_matrix = world_transform.world_matrix,
                .mesh_handle = mesh,
                .material_handle = material});
        }

        const auto light_view =
            scene.m_registry.view<IdComponent, WorldTransformComponent, LightComponent>();
        render_scene.lights.reserve(light_view.size_hint());
        for(const entt::entity handle : light_view) {
            const auto& light = light_view.get<LightComponent>(handle);
            if(!light.enabled)
                continue;
            const auto& world = light_view.get<WorldTransformComponent>(handle);
            render_scene.lights.push_back(
                {.entity_id = light_view.get<IdComponent>(handle).id,
                    .type = light.type,
                    .position = Math::Vec3(world.world_matrix[3]),
                    .direction = -Math::Vec3(world.pose_world_matrix[2]),
                    .color = light.color,
                    .intensity = light.intensity,
                    .range = light.range,
                    .inner_angle = light.inner_angle,
                    .outer_angle = light.outer_angle});
        }
        return render_scene;
    }
}
