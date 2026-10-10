#include "render/scene/scene_resolver.h"

#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "render/material/material.h"
#include "render/resource/mesh.h"
#include "render/resource/environment.h"

#include <algorithm>
#include <utility>

namespace Comet {
    namespace {
        void update_item_values(
            const RenderItem& item, const bool same_scene, ResolvedRenderItem& resolved) {
            if(!same_scene || item.entity_id == INVALID_ENTITY_ID || item.transform_revision == 0
                || resolved.entity_id != item.entity_id
                || resolved.transform_revision != item.transform_revision)
                resolved.model_matrix = item.model_matrix;
            resolved.entity_id = item.entity_id;
            resolved.transform_revision = item.transform_revision;
            if(resolved.material.overrides != item.material_overrides)
                resolved.material.overrides = item.material_overrides;
        }
    }

    SceneResolver::SceneResolver(const AssetRegistry& asset_registry)
        : m_asset_registry(asset_registry) {}

    RenderSubmission SceneResolver::resolve(
        const RenderScene& render_scene, const RenderView& view) {
        RenderSubmission submission;
        resolve(render_scene, view, submission);
        return submission;
    }

    void SceneResolver::refresh_assets(RenderSubmission& submission) const {
        const auto revision = m_asset_registry.get_revision();
        if(submission.asset_revision == revision)
            return;
        bool changed_items = false;
        AssetHandle mesh_handle;
        uint64_t mesh_revision = 0;
        AssetHandle material_handle;
        uint64_t material_revision = 0;
        for(auto& item : submission.render_items) {
            if(item.mesh) {
                if(mesh_handle != item.mesh_handle) {
                    mesh_handle = item.mesh_handle;
                    mesh_revision = m_asset_registry.get_revision(mesh_handle);
                }
                if(mesh_revision == 0 || item.mesh_revision != mesh_revision) {
                    item.mesh.reset();
                    item.mesh_revision = 0;
                    changed_items = true;
                }
            }
            if(item.material.resource) {
                if(material_handle != item.material.material_handle) {
                    material_handle = item.material.material_handle;
                    material_revision = m_asset_registry.get_revision(material_handle);
                }
                if(material_revision == 0 || item.material.asset_revision != material_revision) {
                    item.material.resource.reset();
                    item.material.asset_revision = 0;
                    changed_items = true;
                }
            }
        }
        if(submission.environment_resource) {
            const auto environment_revision =
                m_asset_registry.get_revision(submission.environment.asset);
            if(environment_revision == 0
                || submission.environment_revision != environment_revision) {
                submission.environment_resource.reset();
                submission.environment_revision = 0;
            }
        }
        if(changed_items) {
            submission.scene_revision = 0;
            submission.item_changes = {};
        }
        submission.asset_revision = revision;
    }

    void SceneResolver::resolve(
        const RenderScene& render_scene, const RenderView& view, RenderSubmission& submission) {
        refresh_assets(submission);
        submission.view_project_matrix = resolve_camera(render_scene, view);
        submission.lights = render_scene.lights;
        submission.post_process = render_scene.post_process;
        const auto handle = render_scene.environment.asset;
        AssetHandle invalid_environment;
        // 未发布的环境资源可能仍在加载；加载失败由资产层报告。
        if(handle && (render_scene.environment.background || render_scene.environment.lighting)) {
            if(!submission.environment_resource || submission.environment.asset != handle) {
                submission.environment_resource =
                    m_asset_registry.resolve<Environment>(handle, &submission.environment_revision);
            }
            if(!submission.environment_resource && m_asset_registry.contains(handle)) {
                invalid_environment = handle;
                if(m_invalid_environment != handle)
                    LOG_ERROR("Scene references incompatible environment asset handle {} "
                              "(expected environment resource)",
                        handle.value());
            }
        } else {
            submission.environment_resource.reset();
            submission.environment_revision = 0;
        }
        submission.environment = render_scene.environment;
        m_invalid_environment = invalid_environment;
        const auto& items = render_scene.render_items;
        auto& slots = submission.render_items;
        slots.reserve(items.size());
        const bool same_scene = render_scene.scene_lifetime != 0
                                && submission.scene_lifetime == render_scene.scene_lifetime;
        const auto resolve_changed_item = [&](const RenderItem& item,
                                              ResolvedRenderItem& resolved) {
            if(resolved.mesh_handle != item.mesh_handle) {
                resolved.mesh =
                    m_asset_registry.resolve<Mesh>(item.mesh_handle, &resolved.mesh_revision);
                resolved.mesh_handle = item.mesh_handle;
            }
            if(!resolved.mesh)
                return false;
            if(resolved.material.material_handle != item.material_handle) {
                resolved.material.resource = m_asset_registry.resolve<const Material>(
                    item.material_handle, &resolved.material.asset_revision);
                resolved.material.material_handle = item.material_handle;
            }
            if(!resolved.material.resource)
                return false;
            update_item_values(item, same_scene, resolved);
            return true;
        };
        const auto& changes = render_scene.item_changes;
        const bool tracked = same_scene && changes.revision != 0 && submission.scene_revision != 0
                             && slots.size() == items.size();
        if(tracked && submission.scene_revision == changes.revision)
            return;
        if(tracked && !changes.full_update && submission.scene_revision == changes.base_revision) {
            bool complete = true;
            for(const auto index : changes.slots) {
                if(!resolve_changed_item(items[index], slots[index])) {
                    complete = false;
                    break;
                }
            }
            if(complete) {
                if(!changes.slots.empty()) {
                    submission.item_changes.begin_update(false);
                    submission.item_changes.slots = changes.slots;
                }
                submission.scene_revision = changes.revision;
                return;
            }
        }
        submission.item_changes.begin_update(true);
        for(auto& [handle, used] : m_missing_mesh_handles)
            used = false;
        for(auto& [handle, used] : m_missing_material_handles)
            used = false;

        const auto previous_size = slots.size();
        // 单个增删只移动一次后缀；缺失资源导致的压缩仍由下方逐项解析处理。
        if(same_scene && (items.size() == previous_size + 1 || previous_size == items.size() + 1)) {
            std::size_t index = 0;
            while(index < std::min(items.size(), previous_size)
                  && items[index].entity_id == slots[index].entity_id)
                ++index;
            if(items.size() > previous_size
                && (index == previous_size
                    || items[index + 1].entity_id == slots[index].entity_id)) {
                slots.emplace_back();
                std::move_backward(slots.begin() + index, slots.end() - 1, slots.end());
                slots[index] = {};
            } else if(previous_size > items.size()
                      && (index == items.size()
                          || items[index].entity_id == slots[index + 1].entity_id)) {
                std::move(slots.begin() + index + 1, slots.end(), slots.begin() + index);
                slots.pop_back();
            }
        }
        // 分别复用 Mesh 与材质；新增或重排的输入仍共用帧内查询。
        AssetHandle mesh_handle;
        AssetHandle material_handle;
        std::shared_ptr<Mesh> mesh;
        uint64_t mesh_revision = 0;
        std::shared_ptr<const Material> material;
        uint64_t material_revision = 0;
        std::size_t item_count = 0;
        for(const RenderItem& item : items) {
            if(item_count == slots.size())
                slots.emplace_back();
            auto& resolved = slots[item_count];
            const bool same_mesh = resolved.mesh && resolved.mesh_handle == item.mesh_handle;
            const bool same_material = resolved.material.resource
                                       && resolved.material.material_handle == item.material_handle;
            if(!same_mesh || !same_material) {
                if(!same_mesh && mesh_handle != item.mesh_handle) {
                    mesh_handle = item.mesh_handle;
                    mesh = m_asset_registry.resolve<Mesh>(item.mesh_handle, &mesh_revision);
                }
                const auto& item_mesh = same_mesh ? resolved.mesh : mesh;
                if(!item_mesh) {
                    const auto [entry, inserted] =
                        m_missing_mesh_handles.try_emplace(item.mesh_handle, true);
                    entry->second = true;
                    if(inserted)
                        LOG_ERROR("Render item references missing mesh handle {}",
                            item.mesh_handle.value());
                    continue;
                }
                m_missing_mesh_handles.erase(item.mesh_handle);

                if(!same_material && material_handle != item.material_handle) {
                    material_handle = item.material_handle;
                    material = m_asset_registry.resolve<const Material>(
                        item.material_handle, &material_revision);
                }
                const auto& item_material = same_material ? resolved.material.resource : material;
                if(!item_material) {
                    const auto [entry, inserted] =
                        m_missing_material_handles.try_emplace(item.material_handle, true);
                    entry->second = true;
                    if(inserted)
                        LOG_ERROR("Render item references missing material handle {}",
                            item.material_handle.value());
                    continue;
                }
                m_missing_material_handles.erase(item.material_handle);
                if(!same_mesh) {
                    resolved.mesh_handle = item.mesh_handle;
                    resolved.mesh_revision = mesh_revision;
                    resolved.mesh = mesh;
                }
                if(!same_material) {
                    resolved.material.material_handle = item.material_handle;
                    resolved.material.asset_revision = material_revision;
                    resolved.material.resource = material;
                }
            }
            update_item_values(item, same_scene, resolved);
            ++item_count;
        }
        slots.resize(item_count);
        submission.scene_lifetime = render_scene.scene_lifetime;
        submission.scene_revision = changes.revision;
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
