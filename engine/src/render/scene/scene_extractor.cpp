#include "render/scene/scene_extractor.h"

#include "scene/components.h"
#include "scene/scene.h"
#include <algorithm>

namespace Comet {
    namespace {
        void extract_views(Scene& scene, RenderScene& output) {
            output.cameras.clear();
            output.lights.clear();
            output.environment = scene.get_environment();
            output.post_process = scene.get_post_process();
            output.cameras.reserve(scene.component_count<CameraComponent>());
            scene.each<const CameraComponent, const TransformComponent, WorldTransformComponent,
                IdComponent>([&](Entity, const CameraComponent& camera, const TransformComponent&,
                                 const WorldTransformComponent& world, const IdComponent& id) {
                auto projection = RenderCamera::Projection::Perspective;
                if(camera.projection == CameraComponent::Projection::Orthographic)
                    projection = RenderCamera::Projection::Orthographic;
                output.cameras.push_back({.entity_id = id.id,
                    .primary = camera.primary,
                    .view_matrix = Math::inverse(world.pose_world_matrix),
                    .projection = projection,
                    .fov_degrees = camera.fov,
                    .orthographic_height = camera.orthographic_height,
                    .near_clip = camera.near_clip,
                    .far_clip = camera.far_clip});
            });
            output.lights.reserve(scene.component_count<LightComponent>());
            scene.each<const LightComponent, WorldTransformComponent, IdComponent>(
                [&](Entity, const LightComponent& light, const WorldTransformComponent& world,
                    const IdComponent& id) {
                    if(!light.enabled)
                        return;
                    output.lights.push_back({.entity_id = id.id,
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

        void extract_item(Scene& scene, Entity entity, const MeshRendererComponent& mesh,
            const WorldTransformComponent& world, EntityId id, bool same_scene, RenderItem& item) {
            if(!same_scene || item.entity_id != id || item.transform_revision != world.revision)
                item.model_matrix = world.world_matrix;
            item.entity_id = id;
            item.transform_revision = world.revision;
            item.mesh_handle = mesh.mesh;
            item.material_handle = mesh.material;
            const auto overrides = scene.get_material_overrides(entity);
            if(item.material_overrides != overrides)
                item.material_overrides = overrides;
        }
    }

    RenderScene SceneExtractor::extract(Scene& scene) {
        RenderScene output;
        extract(scene, output);
        return output;
    }

    void SceneExtractor::extract(Scene& scene, RenderScene& output) {
        extract_all(scene, output, nullptr);
    }

    void SceneExtractor::extract_all(Scene& scene, RenderScene& output, SceneExtractor* slots) {
        scene.update_world_transforms();
        const bool same_scene = output.scene_lifetime == scene.get_lifetime();
        output.scene_lifetime = scene.get_lifetime();
        extract_views(scene, output);
        // 复用已有快照槽位；查询过滤掉缺少 Transform 的实体后再收缩。
        output.render_items.resize(scene.component_count<MeshRendererComponent>());
        if(slots) {
            slots->m_entities_by_slot.resize(output.render_items.size());
            slots->m_slots_by_entity.reserve(scene.entity_count());
        }
        std::size_t item_count = 0;
        scene.each<const MeshRendererComponent, const TransformComponent, WorldTransformComponent,
            IdComponent>(
            [&](Entity entity, const MeshRendererComponent& mesh, const TransformComponent&,
                const WorldTransformComponent& world, const IdComponent& id) {
                extract_item(
                    scene, entity, mesh, world, id.id, same_scene, output.render_items[item_count]);
                if(slots) {
                    const auto index = entt::to_entity(entity.m_handle);
                    if(index >= slots->m_slots_by_entity.size())
                        slots->m_slots_by_entity.resize(index + 1);
                    slots->m_slots_by_entity[index] = item_count;
                    slots->m_entities_by_slot[item_count] = entity.m_handle;
                }
                ++item_count;
            });
        output.render_items.resize(item_count);
        if(slots) {
            slots->m_entities_by_slot.resize(item_count);
            slots->m_matches_storage_order = true;
        }
    }

    void SceneExtractor::reset() noexcept {
        m_scene_lifetime = 0;
        m_revision = 0;
        m_matches_storage_order = false;
        m_slots_by_entity.clear();
        m_entities_by_slot.clear();
    }

    void SceneExtractor::update_items(Scene& scene, RenderScene& output) {
        auto& items = output.render_items;
        for(const auto& change : scene.m_render_changes) {
            const auto index = entt::to_entity(change.handle);
            auto slot = items.size();
            if(index < m_slots_by_entity.size()) {
                const auto candidate = m_slots_by_entity[index];
                // EntityId 同时排除已经回收并复用的 EnTT 槽位。
                if(candidate < items.size() && items[candidate].entity_id == change.id)
                    slot = candidate;
            }
            const Entity entity(change.handle, &scene);
            const bool renderable = entity && entity.get_id() == change.id
                                    && entity.has_component<MeshRendererComponent>()
                                    && entity.has_component<TransformComponent>();
            if(renderable) {
                if(slot == items.size()) {
                    if(index >= m_slots_by_entity.size())
                        m_slots_by_entity.resize(index + 1);
                    m_slots_by_entity[index] = slot;
                    m_entities_by_slot.push_back(change.handle);
                    items.emplace_back();
                    m_matches_storage_order = false;
                }
                extract_item(scene, entity, entity.get_component<MeshRendererComponent>(),
                    entity.get_component<WorldTransformComponent>(), change.id, true, items[slot]);
            } else if(slot < items.size()) {
                if(slot + 1 != items.size()) {
                    items[slot] = std::move(items.back());
                    const auto moved = m_entities_by_slot.back();
                    m_entities_by_slot[slot] = moved;
                    m_slots_by_entity[entt::to_entity(moved)] = slot;
                }
                items.pop_back();
                m_entities_by_slot.pop_back();
                m_matches_storage_order = false;
            }
        }
    }

    void SceneExtractor::update(Scene& scene, RenderScene& output) {
        if(m_scene_lifetime == scene.get_lifetime() && m_revision == scene.get_render_revision())
            return;
        const bool full = m_scene_lifetime != scene.get_lifetime()
                          || m_revision != scene.m_render_changes_base
                          || scene.m_render_full_update;
        if(full) {
            const bool rebuild_slots =
                !m_matches_storage_order || m_scene_lifetime != scene.get_lifetime()
                || m_revision != scene.m_render_changes_base || scene.m_render_structure_changed;
            SceneExtractor* slots = nullptr;
            if(rebuild_slots)
                slots = this;
            extract_all(scene, output, slots);
        } else {
            scene.update_world_transforms();
            extract_views(scene, output);
            update_items(scene, output);
            if(scene.m_render_structure_changed)
                m_matches_storage_order = false;
        }
        m_scene_lifetime = scene.get_lifetime();
        m_revision = scene.get_render_revision();
        scene.m_render_changes_base = m_revision;
        // 限制全量运动时的记录成本；小场景仍按对象数缩小局部批次。
        scene.m_render_change_limit =
            std::clamp<std::size_t>(scene.component_count<MeshRendererComponent>() / 4, 8, 64);
        scene.m_render_full_update = false;
        scene.m_render_structure_changed = false;
        scene.m_render_changes.clear();
        scene.m_dirty_render_entities.clear();
    }
}
