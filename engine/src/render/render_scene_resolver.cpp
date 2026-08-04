#include "render_scene_resolver.h"

#include "asset/registry.h"
#include "common/logger.h"
#include "material.h"
#include "mesh.h"
#include "texture.h"

#include <utility>

namespace Comet {
    RenderSceneResolver::RenderSceneResolver(const AssetRegistry& asset_registry)
        : m_asset_registry(asset_registry) {}

    RenderSubmission RenderSceneResolver::resolve(const RenderScene& render_scene) {
        RenderSubmission submission;
        submission.render_items.reserve(render_scene.render_items.size());

        for(const RenderItem& render_item : render_scene.render_items) {
            if(auto resolved_item = resolve_item(render_item)) {
                submission.render_items.push_back(std::move(*resolved_item));
            }
        }

        return submission;
    }

    std::optional<ResolvedRenderItem> RenderSceneResolver::resolve_item(
        const RenderItem& render_item) {
        const auto mesh = m_asset_registry.resolve<Mesh>(render_item.mesh_handle);
        if(!mesh) {
            if(m_missing_mesh_handles.insert(render_item.mesh_handle).second) {
                LOG_ERROR("Render item references missing mesh handle {}",
                    render_item.mesh_handle.value());
            }
            return std::nullopt;
        }
        m_missing_mesh_handles.erase(render_item.mesh_handle);

        const auto material = m_asset_registry.resolve<Material>(render_item.material_handle);
        if(!material) {
            if(m_missing_material_handles.insert(render_item.material_handle).second) {
                LOG_ERROR("Render item references missing material handle {}",
                    render_item.material_handle.value());
            }
            return std::nullopt;
        }
        m_missing_material_handles.erase(render_item.material_handle);

        std::array<std::shared_ptr<Texture>, 2> textures = {
            material->get_texture_property("u_Texture0"),
            material->get_texture_property("u_Texture1")
        };
        if(!textures[0] || !textures[1]) {
            if(m_invalid_material_handles.insert(render_item.material_handle).second) {
                LOG_ERROR(
                    "Material handle {} requires Texture properties u_Texture0 and u_Texture1",
                    render_item.material_handle.value());
            }
            return std::nullopt;
        }
        m_invalid_material_handles.erase(render_item.material_handle);

        return ResolvedRenderItem{
            .entity_id = render_item.entity_id,
            .model_matrix = render_item.model_matrix,
            .mesh = mesh,
            .material = {
                .material_handle = render_item.material_handle,
                .textures = std::move(textures)
            }
        };
    }
}
