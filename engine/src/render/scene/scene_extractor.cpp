#include "render/scene/scene_extractor.h"

#include "scene/components.h"
#include "scene/scene.h"

namespace Comet {
    RenderScene SceneExtractor::extract(Scene& scene) {
        scene.update_world_transforms();

        RenderScene render_scene;
        render_scene.environment = scene.get_environment();
        render_scene.post_process = scene.get_post_process();

        render_scene.cameras.reserve(scene.component_count<CameraComponent>());
        scene.each<const CameraComponent, const TransformComponent, WorldTransformComponent>(
            [&](Entity entity, const CameraComponent& camera, const TransformComponent&,
                const WorldTransformComponent& world_transform) {
                render_scene.cameras.push_back({.entity_id = entity.get_id(),
                    .primary = camera.primary,
                    .view_matrix = Math::inverse(world_transform.pose_world_matrix),
                    .fov_degrees = camera.fov,
                    .near_clip = camera.near_clip,
                    .far_clip = camera.far_clip});
            });

        render_scene.render_items.reserve(scene.component_count<MeshRendererComponent>());
        scene.each<const MeshRendererComponent, const TransformComponent, WorldTransformComponent>(
            [&](Entity entity, const MeshRendererComponent& mesh, const TransformComponent&,
                const WorldTransformComponent& world_transform) {
                render_scene.render_items.push_back({.entity_id = entity.get_id(),
                    .model_matrix = world_transform.world_matrix,
                    .mesh_handle = mesh.mesh,
                    .material_handle = mesh.material});
            });

        render_scene.lights.reserve(scene.component_count<LightComponent>());
        scene.each<const LightComponent, WorldTransformComponent>(
            [&](Entity entity, const LightComponent& light, const WorldTransformComponent& world) {
                if(!light.enabled)
                    return;
                render_scene.lights.push_back({.entity_id = entity.get_id(),
                    .type = light.type,
                    .position = Math::Vec3(world.world_matrix[3]),
                    .direction = -Math::Vec3(world.pose_world_matrix[2]),
                    .color = light.color,
                    .intensity = light.intensity,
                    .range = light.range,
                    .inner_angle = light.inner_angle,
                    .outer_angle = light.outer_angle,
                    .casts_shadow = light.casts_shadow});
            });
        return render_scene;
    }
}
