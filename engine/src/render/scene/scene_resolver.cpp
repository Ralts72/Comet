#include "render/scene/scene_resolver.h"

#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"

#include <utility>

namespace Comet {
    SceneResolver::SceneResolver(const AssetRegistry& asset_registry)
        : m_asset_registry(asset_registry) {}

    RenderSubmission SceneResolver::resolve(
        const RenderScene& render_scene, const RenderView& view) {
        RenderSubmission submission;
        submission.view_project_matrix = resolve_camera(render_scene, view);
        submission.render_items.reserve(render_scene.render_items.size());

        for(const RenderItem& render_item : render_scene.render_items) {
            if(auto resolved_item = resolve_item(render_item)) {
                submission.render_items.push_back(std::move(*resolved_item));
            }
        }

        return submission;
    }

    std::optional<ViewProjectMatrix> SceneResolver::resolve_camera(
        const RenderScene& render_scene, const RenderView& view) {
        const RenderCamera* camera = nullptr;
        std::size_t primary_camera_count = 0;
        if(view.camera_selection == RenderView::CameraSelection::Override) {
            if(view.camera_override) {
                camera = &*view.camera_override;
                m_missing_camera_override = false;
            } else if(!m_missing_camera_override) {
                LOG_WARN("Render view requested a camera override but none was provided; "
                         "scene drawing is skipped");
                m_missing_camera_override = true;
            }
            m_missing_primary_camera = false;
            m_multiple_primary_cameras = false;
        } else {
            m_missing_camera_override = false;
            for(const RenderCamera& scene_camera : render_scene.cameras) {
                if(!scene_camera.primary)
                    continue;

                ++primary_camera_count;
                if(!camera || scene_camera.entity_id < camera->entity_id) {
                    camera = &scene_camera;
                }
            }

            if(!camera) {
                if(!m_missing_primary_camera) {
                    LOG_WARN(
                        "Render scene has no primary camera; scene drawing is skipped");
                    m_missing_primary_camera = true;
                }
            } else {
                m_missing_primary_camera = false;
            }
        }

        if(!camera) {
            m_camera_diagnostic.reset();
            m_invalid_render_size = false;
            return std::nullopt;
        }

        if(view.camera_selection == RenderView::CameraSelection::ScenePrimary
            && primary_camera_count > 1) {
            if(!m_multiple_primary_cameras) {
                LOG_WARN("Render scene has {} primary cameras; using entity {}",
                    primary_camera_count, camera->entity_id);
                m_multiple_primary_cameras = true;
            }
        } else {
            m_multiple_primary_cameras = false;
        }

        if(view.render_size.x == 0 || view.render_size.y == 0) {
            if(!m_invalid_render_size) {
                LOG_WARN("Cannot build camera projection for render size {}x{}",
                    view.render_size.x, view.render_size.y);
                m_invalid_render_size = true;
            }
            m_camera_diagnostic.reset();
            return std::nullopt;
        }
        m_invalid_render_size = false;

        const float aspect = static_cast<float>(view.render_size.x)
                             / static_cast<float>(view.render_size.y);
        if(const auto issue = camera->projection_issue(aspect)) {
            const CameraDiagnostic diagnostic{camera->entity_id, *issue};
            if(m_camera_diagnostic != diagnostic) {
                const char* reason = "unknown";
                switch(*issue) {
                    case RenderCamera::ProjectionIssue::InvalidFov:
                        reason = "invalid FOV";
                        break;
                    case RenderCamera::ProjectionIssue::InvalidOrthographicHeight:
                        reason = "invalid orthographic height";
                        break;
                    case RenderCamera::ProjectionIssue::InvalidClipPlanes:
                        reason = "invalid clip planes";
                        break;
                    case RenderCamera::ProjectionIssue::InvalidAspect:
                        reason = "invalid aspect ratio";
                        break;
                    case RenderCamera::ProjectionIssue::InvalidView:
                        reason = "nonfinite view matrix";
                        break;
                }
                LOG_ERROR(
                    "Render camera entity {} has invalid projection/view parameters "
                    "({}, FOV={}, height={}, near={}, far={})",
                    camera->entity_id, reason, camera->fov_degrees,
                    camera->orthographic_height, camera->near_clip, camera->far_clip);
            }
            m_camera_diagnostic = diagnostic;
            return std::nullopt;
        }
        m_camera_diagnostic.reset();
        return ViewProjectMatrix{
            .view = camera->view_matrix,
            .projection = *camera->projection_matrix(aspect),
        };
    }

    std::optional<ResolvedRenderItem> SceneResolver::resolve_item(
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

        const auto material =
            m_asset_registry.resolve<Material>(render_item.material_handle);
        if(!material) {
            if(m_missing_material_handles.insert(render_item.material_handle).second) {
                LOG_ERROR("Render item references missing material handle {}",
                    render_item.material_handle.value());
            }
            return std::nullopt;
        }
        m_missing_material_handles.erase(render_item.material_handle);

        if(material->get_template_name() != "unlit_texture_blend") {
            if(m_invalid_material_handles.insert(render_item.material_handle).second) {
                LOG_ERROR("Material handle {} uses unsupported template '{}'",
                    render_item.material_handle.value(), material->get_template_name());
            }
            return std::nullopt;
        }

        std::array<std::shared_ptr<Texture>, 2> textures = {
            material->get_texture_property("u_Texture0"),
            material->get_texture_property("u_Texture1")};
        if(!textures[0] || !textures[1]) {
            if(m_invalid_material_handles.insert(render_item.material_handle).second) {
                LOG_ERROR(
                    "Material handle {} requires Texture properties u_Texture0 and u_Texture1",
                    render_item.material_handle.value());
            }
            return std::nullopt;
        }
        m_invalid_material_handles.erase(render_item.material_handle);

        return ResolvedRenderItem{.entity_id = render_item.entity_id,
            .model_matrix = render_item.model_matrix,
            .mesh = mesh,
            .material = {.material_handle = render_item.material_handle,
                .textures = std::move(textures)}};
    }
}
