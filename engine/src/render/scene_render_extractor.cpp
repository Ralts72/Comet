#include "render/scene_render_extractor.h"

#include "scene/components.h"
#include "scene/scene.h"

namespace Comet {
    RenderScene SceneRenderExtractor::extract(Scene& scene) {
        scene.update_world_transforms();

        RenderScene render_scene;

        const auto camera_view =
                scene.m_registry.view<IdComponent, TransformComponent,
                                      WorldTransformComponent, CameraComponent>();
        render_scene.cameras.reserve(camera_view.size_hint());
        for(const entt::entity handle: camera_view) {
            const auto& [id] = camera_view.get<IdComponent>(handle);
            const auto& world_transform =
                    camera_view.get<WorldTransformComponent>(handle);
            const auto& [primary, fov, near_clip, far_clip] =
                    camera_view.get<CameraComponent>(handle);

            render_scene.cameras.push_back({
                .entity_id = id,
                .primary = primary,
                .view_matrix = Math::inverse(
                    world_transform.camera_world_matrix),
                .fov_degrees = fov,
                .near_clip = near_clip,
                .far_clip = far_clip
            });
        }

        const auto view =
                scene.m_registry.view<IdComponent, TransformComponent,
                                      WorldTransformComponent, MeshRendererComponent>();
        render_scene.render_items.reserve(view.size_hint());

        for(const entt::entity handle: view) {
            const auto& [id] = view.get<IdComponent>(handle);
            const auto& world_transform =
                    view.get<WorldTransformComponent>(handle);
            const auto& [mesh, material] = view.get<MeshRendererComponent>(handle);

            render_scene.render_items.push_back({
                .entity_id = id,
                .model_matrix = world_transform.world_matrix,
                .mesh_handle = mesh,
                .material_handle = material
            });
        }

        return render_scene;
    }
}
