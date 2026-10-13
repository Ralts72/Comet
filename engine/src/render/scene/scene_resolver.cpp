#include "render/scene/scene_resolver.h"
#include "common/snapshot.h"

#include "asset/registry.h"
#include "asset/artifact/shader_program_artifact.h"
#include "diagnostics/logger.h"
#include "render/material/material.h"
#include "render/resource/mesh.h"
#include "render/resource/environment.h"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <utility>

namespace Comet {
    namespace {
        void prune_material_sources(RenderWorld& world) {
            std::unordered_set<AssetHandle> used;
            used.reserve(world.resolution.material_sources.size());
            for(const auto& item : world.render_items)
                used.insert(item.material.material_handle);
            std::erase_if(world.resolution.material_sources,
                [&](const auto& entry) { return !used.contains(entry.first); });
        }

        void update_item_values(
            const RenderItem& item, const bool same_scene, ResolvedRenderItem& resolved) {
            if(!same_scene || item.entity_id == INVALID_ENTITY_ID || item.transform_revision == 0
                || resolved.entity_id != item.entity_id
                || resolved.transform_revision != item.transform_revision)
                resolved.model_matrix = item.model_matrix;
            resolved.animation = item.animation;
            resolved.entity_id = item.entity_id;
            resolved.transform_revision = item.transform_revision;
            if(resolved.material.overrides != item.material_overrides)
                resolved.material.overrides = item.material_overrides;
        }
    }

    SceneResolver::SceneResolver(const AssetRegistry& asset_registry)
        : m_asset_registry(asset_registry) {}

    RenderWorld SceneResolver::resolve(const RenderScene& render_scene) {
        RenderWorld world;
        resolve(render_scene, world);
        return world;
    }

    RenderWorld& SceneResolver::writable_output() {
        return writable_snapshot(m_output);
    }

    std::shared_ptr<const RenderWorld> SceneResolver::update(const RenderScene& render_scene) {
        resolve(render_scene, writable_output());
        return m_output;
    }

    void SceneResolver::refresh_assets() {
        if(m_output)
            refresh_assets(writable_output());
    }

    void SceneResolver::reset() {
        if(m_output && m_output.use_count() == 1)
            m_output->clear();
        else
            m_output.reset();
    }

    MaterialBinding SceneResolver::resolve_material(
        const AssetHandle handle, RenderWorld& world) const {
        MaterialBinding binding{.material_handle = handle};
        const auto source =
            m_asset_registry.resolve<const Material>(handle, &binding.asset_revision);
        if(!source)
            return binding;
        world.resolution.material_sources.insert_or_assign(
            handle, RenderWorld::ResolutionCache::MaterialSource{source, source->get_revision()});
        binding.resource = source->snapshot();
        if(const auto program = source->get_shader_program())
            binding.program_source = m_asset_registry.resolve<const ShaderProgramArtifact>(program);
        return binding;
    }

    void SceneResolver::refresh_assets(RenderWorld& world) const {
        const auto revision = m_asset_registry.get_revision();
        const bool published = world.resolution.asset_revision != revision;
        auto& materials = world.resolution.material_sources;
        bool edited = false;
        // 每个材质身份只检查一次；参数没变时不扫描物体或复制参数。
        for(auto entry = materials.begin(); entry != materials.end();) {
            const auto source = entry->second.source.lock();
            if(!source || (published && !m_asset_registry.contains<Material>(entry->first))) {
                entry = materials.erase(entry);
                edited = true;
                continue;
            }
            if(entry->second.revision != source->get_revision()) {
                entry->second.revision = source->get_revision();
                edited = true;
            }
            ++entry;
        }
        if(!published && !edited)
            return;
        auto& pending = world.resolution.pending_asset_slots;
        const bool had_pending = !pending.empty();
        const auto partial_limit = std::clamp<std::size_t>(world.render_items.size() / 4, 8, 64);
        const auto collection_limit = partial_limit + pending.size();
        bool collect_changes = world.resolution.input_revision != 0;
        bool changed_items = false;
        AssetHandle mesh_handle;
        uint64_t mesh_revision = 0;
        AssetHandle material_handle;
        uint64_t material_revision = 0;
        AssetHandle program_handle;
        std::shared_ptr<const ShaderProgramArtifact> program_source;
        // 只缓存本次检查的版本值；碰撞时重新查询，低命中率时停止使用缓存。
        std::array<std::pair<AssetHandle, uint64_t>, 128> revisions{};
        const auto miss_limit = revisions.size() * 2;
        auto misses_remaining = miss_limit;
        bool reuse_revisions = true;
        const auto get_revision = [&](const AssetHandle handle) {
            if(!reuse_revisions)
                return m_asset_registry.get_revision(handle);
            auto& cached = revisions[std::hash<AssetHandle>{}(handle) % revisions.size()];
            if(cached.first == handle) {
                misses_remaining = std::min(misses_remaining + 1, miss_limit);
                return cached.second;
            }
            if(--misses_remaining == 0)
                reuse_revisions = false;
            cached = {handle, m_asset_registry.get_revision(handle)};
            return cached.second;
        };
        const auto* first = world.render_items.data();
        for(auto& item : world.render_items) {
            bool changed = false;
            if(item.mesh) {
                if(mesh_handle != item.mesh_handle) {
                    mesh_handle = item.mesh_handle;
                    mesh_revision = get_revision(mesh_handle);
                }
                if(mesh_revision == 0 || item.mesh_revision != mesh_revision) {
                    item.mesh.reset();
                    item.mesh_revision = 0;
                    changed = true;
                }
            }
            if(item.material.resource) {
                if(material_handle != item.material.material_handle) {
                    material_handle = item.material.material_handle;
                    material_revision = get_revision(material_handle);
                }
                const auto source = materials.find(item.material.material_handle);
                const bool parameters_changed =
                    edited && source != materials.end()
                    && item.material.resource->get_revision() != source->second.revision;
                if(material_revision == 0 || item.material.asset_revision != material_revision
                    || parameters_changed) {
                    item.material.resource.reset();
                    item.material.asset_revision = 0;
                    item.material.program_source.reset();
                    changed = true;
                } else if(published && item.material.resource->get_shader_program()) {
                    const auto handle = item.material.resource->get_shader_program();
                    if(program_handle != handle) {
                        program_handle = handle;
                        program_source =
                            m_asset_registry.resolve<const ShaderProgramArtifact>(handle);
                    }
                    item.material.program_source = program_source;
                }
            }
            if(changed && collect_changes) {
                pending.push_back(static_cast<std::size_t>(&item - first));
                changed_items = true;
                if(pending.size() > collection_limit) {
                    collect_changes = false;
                    pending.clear();
                }
            }
        }
        if(world.environment_resource) {
            const auto environment_revision =
                m_asset_registry.get_revision(world.environment.asset);
            if(environment_revision == 0 || world.environment_revision != environment_revision) {
                world.environment_resource.reset();
                world.environment_revision = 0;
            }
        }
        if(had_pending && changed_items) {
            std::ranges::sort(pending);
            pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
        }
        if(pending.size() > partial_limit) {
            collect_changes = false;
            pending.clear();
        }
        if(!collect_changes)
            world.resolution.input_revision = 0;
        world.resolution.asset_revision = revision;
    }

    void SceneResolver::resolve_environment(const RenderScene& render_scene, RenderWorld& world) {
        const auto handle = render_scene.environment.asset;
        AssetHandle invalid_environment;
        // 未发布的环境资源可能仍在加载；加载失败由资产层报告。
        if(handle && (render_scene.environment.background || render_scene.environment.lighting)) {
            if(!world.environment_resource || world.environment.asset != handle) {
                world.environment_resource =
                    m_asset_registry.resolve<Environment>(handle, &world.environment_revision);
            }
            if(!world.environment_resource && m_asset_registry.contains(handle)) {
                invalid_environment = handle;
                if(m_invalid_environment != handle)
                    LOG_ERROR("Scene references incompatible environment asset handle {} "
                              "(expected environment resource)",
                        handle.value());
            }
        } else {
            world.environment_resource.reset();
            world.environment_revision = 0;
        }
        world.environment = render_scene.environment;
        m_invalid_environment = invalid_environment;
    }

    void SceneResolver::resolve(const RenderScene& render_scene, RenderWorld& world) {
        refresh_assets(world);
        world.cameras = render_scene.cameras;
        world.lights = render_scene.lights;
        world.post_process = render_scene.post_process;
        resolve_environment(render_scene, world);
        const auto& items = render_scene.render_items;
        auto& slots = world.render_items;
        slots.reserve(items.size());
        const bool same_scene =
            render_scene.scene_lifetime != 0 && world.scene_lifetime == render_scene.scene_lifetime;
        bool materials_changed = !same_scene || slots.size() != items.size();
        // 完整与局部解析共用相邻 Handle 查询，避免共享资源逐物体重复查找。
        AssetHandle mesh_handle;
        AssetHandle material_handle;
        std::shared_ptr<Mesh> mesh;
        uint64_t mesh_revision = 0;
        MaterialBinding material;
        enum class MissingResource { None, Mesh, Material };
        const auto resolve_item = [&](const RenderItem& item, ResolvedRenderItem& resolved) {
            if(!resolved.mesh || resolved.mesh_handle != item.mesh_handle) {
                if(mesh_handle != item.mesh_handle) {
                    mesh_handle = item.mesh_handle;
                    mesh = m_asset_registry.resolve<Mesh>(mesh_handle, &mesh_revision);
                }
                resolved.mesh = mesh;
                resolved.mesh_handle = item.mesh_handle;
                resolved.mesh_revision = mesh_revision;
            }
            if(!resolved.mesh)
                return MissingResource::Mesh;
            if(!resolved.material.resource
                || resolved.material.material_handle != item.material_handle) {
                materials_changed = true;
                if(material_handle != item.material_handle) {
                    material_handle = item.material_handle;
                    material = resolve_material(material_handle, world);
                }
                resolved.material = material;
            }
            if(!resolved.material.resource)
                return MissingResource::Material;
            return MissingResource::None;
        };
        const auto& changes = render_scene.item_changes;
        auto& pending = world.resolution.pending_asset_slots;
        const bool tracked =
            same_scene && changes.revision != 0 && world.resolution.input_revision != 0;
        const bool unchanged = tracked && world.resolution.input_revision == changes.revision
                               && slots.size() == items.size();
        if(unchanged && pending.empty())
            return;
        const auto partial_limit = std::clamp<std::size_t>(items.size() / 4, 8, 64);
        if(tracked && pending.size() <= partial_limit
            && (unchanged
                || changes.can_apply(
                    world.resolution.input_revision, slots.size(), items.size()))) {
            if(!unchanged) {
                materials_changed |= !changes.structure.empty();
                const bool combine_assets = !pending.empty();
                auto count = slots.size();
                for(const auto& edit : changes.structure) {
                    if(edit.type == RenderItemChanges::StructuralEdit::Type::Append) {
                        ++count;
                    } else {
                        std::erase(pending, edit.slot);
                        for(auto& index : pending)
                            if(index + 1 == count)
                                index = edit.slot;
                        --count;
                    }
                }
                changes.apply_structure(slots);
                pending.insert(pending.end(), changes.slots.begin(), changes.slots.end());
                if(combine_assets) {
                    std::ranges::sort(pending);
                    pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
                }
            }
            bool complete = true;
            for(const auto index : pending) {
                if(resolve_item(items[index], slots[index]) != MissingResource::None) {
                    complete = false;
                    break;
                }
                update_item_values(items[index], same_scene, slots[index]);
            }
            if(complete) {
                if(!pending.empty() || (!unchanged && !changes.structure.empty())) {
                    world.item_changes.begin_update(false);
                    world.item_changes.slots.swap(pending);
                    if(!unchanged)
                        world.item_changes.structure = changes.structure;
                }
                world.resolution.input_revision = changes.revision;
                if(materials_changed)
                    prune_material_sources(world);
                return;
            }
        }
        world.item_changes.begin_update(true);
        pending.clear();
        for(auto& [handle, used] : m_missing_mesh_handles)
            used = false;
        for(auto& [handle, used] : m_missing_material_handles)
            used = false;

        if(same_scene)
            RenderItemChanges::align_single_edit(items, slots);
        std::size_t item_count = 0;
        for(const RenderItem& item : items) {
            if(item_count == slots.size())
                slots.emplace_back();
            auto& resolved = slots[item_count];
            const auto missing = resolve_item(item, resolved);
            if(missing != MissingResource::None) {
                const bool mesh_missing = missing == MissingResource::Mesh;
                const auto handle = mesh_missing ? item.mesh_handle : item.material_handle;
                auto& diagnostics =
                    mesh_missing ? m_missing_mesh_handles : m_missing_material_handles;
                const auto [entry, inserted] = diagnostics.try_emplace(handle, true);
                entry->second = true;
                if(inserted)
                    LOG_ERROR("Render item references missing {} handle {}",
                        mesh_missing ? "mesh" : "material", handle.value());
                continue;
            }
            update_item_values(item, same_scene, resolved);
            ++item_count;
        }
        materials_changed |= slots.size() != item_count;
        slots.resize(item_count);
        if(materials_changed)
            prune_material_sources(world);
        world.scene_lifetime = render_scene.scene_lifetime;
        world.resolution.input_revision = changes.revision;
        std::erase_if(m_missing_mesh_handles, [](const auto& entry) { return !entry.second; });
        std::erase_if(m_missing_material_handles, [](const auto& entry) { return !entry.second; });
    }

    std::optional<ViewProjectMatrix> RenderCameraResolver::resolve(
        const RenderWorld& world, const RenderView& view) {
        if(!view.visible)
            return std::nullopt;
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
            for(const RenderCamera& scene_camera : world.cameras) {
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
