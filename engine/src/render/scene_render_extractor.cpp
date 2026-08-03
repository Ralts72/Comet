#include "render/scene_render_extractor.h"

#include "scene/components.h"
#include "scene/scene.h"

namespace Comet {
    RenderScene SceneRenderExtractor::extract(const Scene& scene) {
        RenderScene render_scene;
        const auto view =
            scene.m_registry.view<IdComponent, TransformComponent, MeshRendererComponent>();
        render_scene.render_items.reserve(view.size_hint());

        for(const entt::entity handle: view) {
            const auto& id = view.get<IdComponent>(handle);
            const auto& transform = view.get<TransformComponent>(handle);
            const auto& mesh_renderer = view.get<MeshRendererComponent>(handle);

            render_scene.render_items.push_back({
                .entity_id = id.id,
                .model_matrix = transform.local_matrix(),
                .mesh_handle = mesh_renderer.mesh,
                .material_handle = mesh_renderer.material
            });
        }

        return render_scene;
    }
}
