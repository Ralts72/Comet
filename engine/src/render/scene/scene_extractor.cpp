#include "render/scene/scene_extractor.h"

#include "scene/components.h"
#include "scene/scene.h"

namespace Comet {
    RenderScene SceneExtractor::extract(Scene& scene) {
        RenderScene render_scene;
        extract(scene, render_scene);
        return render_scene;
    }

    void SceneExtractor::extract(Scene& scene, RenderScene& render_scene) {
        scene.update_world_transforms();
        render_scene.cameras.clear();
        render_scene.lights.clear();
        render_scene.environment = scene.get_environment();
        render_scene.post_process = scene.get_post_process();

        render_scene.cameras.reserve(scene.component_count<CameraComponent>());
        scene.each<const CameraComponent, const TransformComponent, WorldTransformComponent,
            IdComponent>(
            [&](Entity, const CameraComponent& camera, const TransformComponent&,
                const WorldTransformComponent& world_transform, const IdComponent& id) {
                auto projection = RenderCamera::Projection::Perspective;
                if(camera.projection == CameraComponent::Projection::Orthographic)
                    projection = RenderCamera::Projection::Orthographic;
                render_scene.cameras.push_back({.entity_id = id.id,
                    .primary = camera.primary,
                    .view_matrix = Math::inverse(world_transform.pose_world_matrix),
                    .projection = projection,
                    .fov_degrees = camera.fov,
                    .orthographic_height = camera.orthographic_height,
                    .near_clip = camera.near_clip,
                    .far_clip = camera.far_clip});
            });

        // 复用已有快照槽位；查询过滤掉缺少 Transform 的实体后再收缩。
        render_scene.render_items.resize(scene.component_count<MeshRendererComponent>());
        std::size_t item_count = 0;
        scene.each<const MeshRendererComponent, const TransformComponent, WorldTransformComponent,
            IdComponent>(
            [&](Entity entity, const MeshRendererComponent& mesh, const TransformComponent&,
                const WorldTransformComponent& world_transform, const IdComponent& id) {
                auto& item = render_scene.render_items[item_count++];
                item.entity_id = id.id;
                item.model_matrix = world_transform.world_matrix;
                item.mesh_handle = mesh.mesh;
                item.material_handle = mesh.material;
                const auto overrides = scene.get_material_overrides(entity);
                if(item.material_overrides != overrides)
                    item.material_overrides = overrides;
            });
        render_scene.render_items.resize(item_count);

        render_scene.lights.reserve(scene.component_count<LightComponent>());
        scene.each<const LightComponent, WorldTransformComponent, IdComponent>(
            [&](Entity, const LightComponent& light, const WorldTransformComponent& world,
                const IdComponent& id) {
                if(!light.enabled)
                    return;
                render_scene.lights.push_back({.entity_id = id.id,
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
    }
}
