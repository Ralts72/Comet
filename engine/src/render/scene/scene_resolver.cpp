#include "render/scene/scene_resolver.h"

#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "render/material/material.h"
#include "render/resource/mesh.h"
#include "render/resource/environment.h"

#include <utility>

namespace Comet {
    SceneResolver::SceneResolver(const AssetRegistry& asset_registry)
        : m_asset_registry(asset_registry) {}

    RenderSubmission SceneResolver::resolve(
        const RenderScene& render_scene, const RenderView& view) {
        RenderSubmission submission;
        resolve(render_scene, view, submission);
        return submission;
    }

    void SceneResolver::resolve(
        const RenderScene& render_scene, const RenderView& view, RenderSubmission& submission) {
        submission.environment_resource.reset();
        submission.view_project_matrix = resolve_camera(render_scene, view);
        submission.lights = render_scene.lights;
        submission.environment = render_scene.environment;
        submission.post_process = render_scene.post_process;
        const auto handle = render_scene.environment.asset;
        AssetHandle invalid_environment;
        // 未发布的环境资源可能仍在加载；加载失败由资产层报告。
        if(handle && (render_scene.environment.background || render_scene.environment.lighting)) {
            auto environment = m_asset_registry.resolve<Environment>(handle);
            if(environment) {
                submission.environment_resource = std::move(environment);
            } else if(m_asset_registry.contains(handle)) {
                invalid_environment = handle;
                if(m_invalid_environment != handle)
                    LOG_ERROR("Scene references incompatible environment asset handle {} "
                              "(expected environment resource)",
                        handle.value());
            }
        }
        m_invalid_environment = invalid_environment;
        submission.render_items.reserve(render_scene.render_items.size());
        for(auto& [handle, used] : m_missing_mesh_handles)
            used = false;
        for(auto& [handle, used] : m_missing_material_handles)
            used = false;

        const auto asset_revision = m_asset_registry.get_revision();
        const bool same_assets = submission.asset_revision == asset_revision;
        const bool same_scene = render_scene.scene_lifetime != 0
                                && submission.scene_lifetime == render_scene.scene_lifetime;
        // 发布版本未变时复用槽位资源；新增或重排的输入仍共用帧内查询。
        AssetHandle mesh_handle;
        AssetHandle material_handle;
        std::shared_ptr<Mesh> mesh;
        uint64_t mesh_revision = 0;
        std::shared_ptr<const Material> material;
        std::size_t item_count = 0;
        for(const RenderItem& item : render_scene.render_items) {
            if(item_count == submission.render_items.size())
                submission.render_items.emplace_back();
            auto& resolved = submission.render_items[item_count];
            const bool same_resources =
                same_assets && resolved.mesh && resolved.material.resource
                && resolved.mesh_handle == item.mesh_handle
                && resolved.material.material_handle == item.material_handle;
            if(!same_resources) {
                if(mesh_handle != item.mesh_handle) {
                    mesh_handle = item.mesh_handle;
                    mesh = m_asset_registry.resolve<Mesh>(item.mesh_handle);
                    mesh_revision = m_asset_registry.get_revision(item.mesh_handle);
                }
                if(!mesh) {
                    const auto [entry, inserted] =
                        m_missing_mesh_handles.try_emplace(item.mesh_handle, true);
                    entry->second = true;
                    if(inserted)
                        LOG_ERROR("Render item references missing mesh handle {}",
                            item.mesh_handle.value());
                    continue;
                }
                m_missing_mesh_handles.erase(item.mesh_handle);

                if(material_handle != item.material_handle) {
                    material_handle = item.material_handle;
                    material = m_asset_registry.resolve<const Material>(item.material_handle);
                }
                if(!material) {
                    const auto [entry, inserted] =
                        m_missing_material_handles.try_emplace(item.material_handle, true);
                    entry->second = true;
                    if(inserted)
                        LOG_ERROR("Render item references missing material handle {}",
                            item.material_handle.value());
                    continue;
                }
                m_missing_material_handles.erase(item.material_handle);
                resolved.mesh_handle = item.mesh_handle;
                resolved.mesh_revision = mesh_revision;
                if(resolved.mesh != mesh)
                    resolved.mesh = mesh;
                resolved.material.material_handle = item.material_handle;
                if(resolved.material.resource != material)
                    resolved.material.resource = material;
            }
            if(!same_scene || item.entity_id == INVALID_ENTITY_ID || item.transform_revision == 0
                || resolved.entity_id != item.entity_id
                || resolved.transform_revision != item.transform_revision)
                resolved.model_matrix = item.model_matrix;
            resolved.entity_id = item.entity_id;
            resolved.transform_revision = item.transform_revision;
            if(resolved.material.overrides != item.material_overrides)
                resolved.material.overrides = item.material_overrides;
            ++item_count;
        }
        submission.render_items.resize(item_count);
        submission.scene_lifetime = render_scene.scene_lifetime;
        submission.asset_revision = asset_revision;
        std::erase_if(m_missing_mesh_handles, [](const auto& entry) { return !entry.second; });
        std::erase_if(m_missing_material_handles, [](const auto& entry) { return !entry.second; });
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
                    LOG_WARN("Render scene has no primary camera; scene drawing is skipped");
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
                LOG_WARN("Cannot build camera projection for render size {}x{}", view.render_size.x,
                    view.render_size.y);
                m_invalid_render_size = true;
            }
            m_camera_diagnostic.reset();
            return std::nullopt;
        }
        m_invalid_render_size = false;

        const float aspect =
            static_cast<float>(view.render_size.x) / static_cast<float>(view.render_size.y);
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
                LOG_ERROR("Render camera entity {} has invalid projection/view parameters "
                          "({}, FOV={}, height={}, near={}, far={})",
                    camera->entity_id, reason, camera->fov_degrees, camera->orthographic_height,
                    camera->near_clip, camera->far_clip);
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

}
