#include "render_scene_resolver.h"

#include "asset/registry.h"
#include "common/logger.h"
#include "material.h"
#include "mesh.h"
#include "texture.h"

#include <cmath>
#include <utility>

namespace Comet {
    RenderSceneResolver::RenderSceneResolver(const AssetRegistry& asset_registry)
        : m_asset_registry(asset_registry) {}

    RenderSubmission RenderSceneResolver::resolve(
        const RenderScene& render_scene, const Math::Vec2u render_size) {
        RenderSubmission submission;
        submission.view_project_matrix = resolve_camera(render_scene, render_size);
        submission.render_items.reserve(render_scene.render_items.size());

        for(const RenderItem& render_item : render_scene.render_items) {
            if(auto resolved_item = resolve_item(render_item)) {
                submission.render_items.push_back(std::move(*resolved_item));
            }
        }

        return submission;
    }

    std::optional<ViewProjectMatrix> RenderSceneResolver::resolve_camera(
        const RenderScene& render_scene, const Math::Vec2u render_size) {
        const RenderCamera* primary_camera = nullptr;
        std::size_t primary_camera_count = 0;
        for(const RenderCamera& camera : render_scene.cameras) {
            if(!camera.primary) continue;

            ++primary_camera_count;
            if(!primary_camera || camera.entity_id < primary_camera->entity_id) {
                primary_camera = &camera;
            }
        }

        if(!primary_camera) {
            if(!m_missing_primary_camera) {
                LOG_WARN("Render scene has no primary camera; scene drawing is skipped");
                m_missing_primary_camera = true;
            }
            m_multiple_primary_cameras = false;
            m_invalid_camera_fov.reset();
            m_invalid_camera_clip_planes.reset();
            m_invalid_render_size = false;
            return std::nullopt;
        }
        m_missing_primary_camera = false;

        if(primary_camera_count > 1) {
            if(!m_multiple_primary_cameras) {
                LOG_WARN("Render scene has {} primary cameras; using entity {}",
                    primary_camera_count, primary_camera->entity_id);
                m_multiple_primary_cameras = true;
            }
        } else {
            m_multiple_primary_cameras = false;
        }

        if(render_size.x == 0 || render_size.y == 0) {
            if(!m_invalid_render_size) {
                LOG_WARN("Cannot build camera projection for render size {}x{}",
                    render_size.x, render_size.y);
                m_invalid_render_size = true;
            }
            m_invalid_camera_fov.reset();
            m_invalid_camera_clip_planes.reset();
            return std::nullopt;
        }
        m_invalid_render_size = false;

        if(!std::isfinite(primary_camera->fov_degrees)
            || primary_camera->fov_degrees <= 0.0f
            || primary_camera->fov_degrees >= 180.0f) {
            if(m_invalid_camera_fov != primary_camera->entity_id) {
                LOG_ERROR("Primary camera entity {} has invalid FOV {} degrees",
                    primary_camera->entity_id, primary_camera->fov_degrees);
            }
            m_invalid_camera_fov = primary_camera->entity_id;
            m_invalid_camera_clip_planes.reset();
            return std::nullopt;
        }
        m_invalid_camera_fov.reset();

        if(!std::isfinite(primary_camera->near_clip)
            || !std::isfinite(primary_camera->far_clip)
            || primary_camera->near_clip <= 0.0f
            || primary_camera->far_clip <= primary_camera->near_clip) {
            if(m_invalid_camera_clip_planes != primary_camera->entity_id) {
                LOG_ERROR(
                    "Primary camera entity {} has invalid clip planes: near={}, far={}",
                    primary_camera->entity_id,
                    primary_camera->near_clip,
                    primary_camera->far_clip);
            }
            m_invalid_camera_clip_planes = primary_camera->entity_id;
            return std::nullopt;
        }
        m_invalid_camera_clip_planes.reset();

        const float aspect =
                static_cast<float>(render_size.x) / static_cast<float>(render_size.y);
        return ViewProjectMatrix{
            .view = primary_camera->view_matrix,
            .projection = Math::perspective(
                primary_camera->fov_degrees,
                aspect,
                primary_camera->near_clip,
                primary_camera->far_clip)
        };
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
