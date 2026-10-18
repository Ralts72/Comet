#include "render/scene/scene_extractor.h"
#include "common/snapshot.h"

#include "scene/components.h"
#include "scene/scene.h"
#include <algorithm>
#include <cmath>

namespace Comet {
    namespace {
        void extract_particle_colliders(Scene& scene, RenderScene& output, bool interpolating) {
            output.particle_colliders.clear();
            output.particle_meshes.clear();
            if(!output.particles
                || !std::ranges::any_of(output.particles->gpu_emitters, [](const auto& emitter) {
                       return emitter.settings.gpu_dynamics && emitter.settings.gpu_world_collision;
                   }))
                return;
            std::vector<std::pair<EntityId, ParticleWorldCollider>> candidates;
            scene.each<const ColliderComponent, const RigidBodyComponent>([&](Entity entity,
                                                                              const auto& collider,
                                                                              const auto& body) {
                if(collider.shape == ColliderShape::TriangleMesh && !collider.is_trigger
                    && collider.mesh) {
                    if(entity.has_component<ParticleMeshCollisionComponent>()
                        && entity.get_component<ParticleMeshCollisionComponent>().enabled)
                        return;
                    const auto matrix = interpolating && body.motion != BodyMotion::Static
                                            ? scene.get_render_transform(entity).world_matrix
                                            : scene.get_world_matrix(entity);
                    if(Math::positive_trs_scale(matrix))
                        output.particle_meshes.push_back(
                            {entity.get_id(), collider.mesh, matrix, collider.collision_layer});
                    return;
                }
                if(collider.is_trigger
                    || (collider.shape != ColliderShape::Box
                        && collider.shape != ColliderShape::Sphere
                        && collider.shape != ColliderShape::Capsule))
                    return;
                const auto matrix = interpolating ? scene.get_render_transform(entity).world_matrix
                                                  : scene.get_world_matrix(entity);
                if(!Math::is_finite(matrix) || std::abs(Math::determinant(matrix)) < 1e-8f)
                    return;
                ParticleWorldCollider result;

                if(collider.shape == ColliderShape::Sphere) {
                    const float scale = std::max({Math::length(Math::Vec3(matrix[0])),
                        Math::length(Math::Vec3(matrix[1])), Math::length(Math::Vec3(matrix[2]))});
                    result.world_to_local = Math::translate(Math::Mat4(1), -Math::Vec3(matrix[3]));
                    result.extent_shape = Math::Vec4(Math::Vec3(collider.radius * scale), 1);
                } else if(collider.shape == ColliderShape::Capsule) {
                    const auto scale = Math::uniform_scale(matrix);
                    if(!scale || collider.height <= 2 * collider.radius)
                        return;
                    auto pose = matrix;
                    for(int axis = 0; axis < 3; ++axis)
                        pose[axis] /= *scale;
                    result.world_to_local = Math::inverse(pose);
                    result.extent_shape = {collider.radius * *scale,
                        (collider.height * .5f - collider.radius) * *scale,
                        collider.radius * *scale, 2};
                } else {
                    result.world_to_local = Math::inverse(matrix);
                    result.extent_shape = Math::Vec4(collider.half_extents, 0);
                }
                if(!Math::is_finite(result.extent_shape) || result.extent_shape.x <= 0
                    || result.extent_shape.z <= 0 || result.extent_shape.y < 0
                    || (result.extent_shape.w != 2 && result.extent_shape.y == 0))
                    return;
                result.layers[0] = collider.collision_layer;
                candidates.emplace_back(entity.get_id(), result);
            });
            scene.each<const ParticleMeshCollisionComponent>([&](Entity entity,
                                                                 const auto& component) {
                if(!component.enabled)
                    return;
                auto mesh = component.mesh;
                if(!mesh && entity.has_component<MeshRendererComponent>())
                    mesh = entity.get_component<MeshRendererComponent>().mesh;
                if(!mesh)
                    return;
                const auto matrix = interpolating ? scene.get_render_transform(entity).world_matrix
                                                  : scene.get_world_matrix(entity);
                if(!Math::positive_trs_scale(matrix))
                    return;
                auto animation =
                    component.deformed ? scene.get_render_animation_playback(entity) : nullptr;
                output.particle_meshes.push_back(
                    {entity.get_id(), mesh, matrix, component.layers, std::move(animation),
                        component.deformed ? scene.get_vertex_bend(entity) : Math::Vec4(0)});
            });
            std::ranges::sort(output.particle_meshes, {}, &ParticleMeshCollider::entity_id);
            std::ranges::sort(candidates, {}, &std::pair<EntityId, ParticleWorldCollider>::first);
            for(size_t i = 0;
                i < std::min(candidates.size(), size_t(ParticleWorldCollider::MAX_COLLIDERS)); ++i)
                output.particle_colliders.push_back(candidates[i].second);
        }
        void extract_views(Scene& scene, RenderScene& output, bool interpolating) {
            output.particles = scene.get_particles();
            extract_particle_colliders(scene, output, interpolating);
            output.cameras.clear();
            output.lights.clear();
            output.reflection_probes.clear();
            output.irradiance_volumes.clear();
            output.decals.clear();
            output.environment = scene.get_environment();
            output.post_process = scene.get_post_process();
            output.cameras.reserve(scene.component_count<CameraComponent>());
            scene.each<const CameraComponent, const TransformComponent, WorldTransformComponent,
                IdComponent>(
                [&](Entity entity, const CameraComponent& camera, const TransformComponent&,
                    const WorldTransformComponent& authoritative, const IdComponent& id) {
                    const auto& world =
                        interpolating ? scene.get_render_transform(entity) : authoritative;
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
            output.reflection_probes.reserve(scene.component_count<ReflectionProbeComponent>());
            scene.each<const ReflectionProbeComponent, WorldTransformComponent, IdComponent>(
                [&](Entity entity, const ReflectionProbeComponent& probe,
                    const WorldTransformComponent& authoritative, const IdComponent& id) {
                    if(!probe.enabled || !probe.environment || probe.intensity <= 0)
                        return;
                    const auto& world =
                        interpolating ? scene.get_render_transform(entity) : authoritative;
                    const auto bounds = transform_box(
                        {-probe.half_extents, probe.half_extents}, world.world_matrix);
                    if(!bounds)
                        return;
                    output.reflection_probes.push_back({id.id, probe.environment,
                        Math::Vec3(world.world_matrix[3]), *bounds, probe.blend_distance,
                        probe.intensity, probe.rotation, probe.box_projection});
                });
            std::ranges::sort(output.reflection_probes, {}, &RenderReflectionProbe::entity_id);
            output.irradiance_volumes.reserve(scene.component_count<IrradianceVolumeComponent>());
            scene.each<const IrradianceVolumeComponent, IdComponent>(
                [&](Entity entity, const IrradianceVolumeComponent& volume, const IdComponent& id) {
                    if(volume.enabled && (volume.realtime || volume.irradiance)
                        && volume.intensity > 0)
                        output.irradiance_volumes.push_back(
                            {id.id, volume.irradiance, volume.intensity, volume.normal_bias,
                                volume.realtime, Math::Vec3(scene.get_world_matrix(entity)[3]),
                                volume.half_extents, volume.spacing, volume.max_distance,
                                volume.update_budget, volume.update_rays, volume.history_weight,
                                volume.bounce_strength, volume.blend_distance});
                });
            std::ranges::sort(output.irradiance_volumes, {}, &RenderIrradianceVolume::entity_id);
            scene.each<const DecalComponent, WorldTransformComponent, IdComponent>(
                [&](Entity entity, const DecalComponent& decal,
                    const WorldTransformComponent& authoritative, const IdComponent& id) {
                    if(!decal.enabled || decal.color.a <= 0 || !Math::is_finite(decal.half_extents)
                        || Math::any_less_equal(decal.half_extents, Math::Vec3(0)))
                        return;
                    const auto& world =
                        interpolating ? scene.get_render_transform(entity) : authoritative;
                    const auto transform =
                        world.world_matrix * Math::scale(Math::Mat4(1), decal.half_extents * 2.f);
                    const auto bounds =
                        transform_box({Math::Vec3(-.5f), Math::Vec3(.5f)}, transform);
                    if(!bounds || std::abs(Math::determinant(transform)) < 1e-8f)
                        return;
                    output.decals.push_back({id.id, decal.texture, Math::inverse(transform),
                        *bounds, decal.color, decal.roughness, decal.normal_threshold,
                        decal.edge_fade, decal.draw_order});
                });
            std::ranges::sort(output.decals, [](const auto& a, const auto& b) {
                return std::pair(a.draw_order, a.entity_id) < std::pair(b.draw_order, b.entity_id);
            });
            output.lights.reserve(scene.component_count<LightComponent>());
            scene.each<const LightComponent, WorldTransformComponent, IdComponent>(
                [&](Entity entity, const LightComponent& light,
                    const WorldTransformComponent& authoritative, const IdComponent& id) {
                    if(!light.enabled)
                        return;
                    const auto& world =
                        interpolating ? scene.get_render_transform(entity) : authoritative;
                    output.lights.push_back({.entity_id = id.id,
                        .type = light.type,
                        .position = Math::Vec3(world.world_matrix[3]),
                        .direction = -Math::Vec3(world.pose_world_matrix[2]),
                        .color = light.color,
                        .intensity = light.intensity,
                        .range = light.range,
                        .inner_angle = light.inner_angle,
                        .outer_angle = light.outer_angle,
                        .casts_shadow = light.casts_shadow,
                        .shadow_distance = light.shadow_distance});
                });
        }

        void extract_item(Scene& scene, Entity entity, const MeshRendererComponent& mesh,
            const WorldTransformComponent& world, EntityId id, bool same_scene, RenderItem& item) {
            if(!same_scene || item.entity_id != id || item.transform_revision != world.revision)
                item.model_matrix = world.world_matrix;
            item.animation = scene.get_render_animation_playback(entity);
            item.vertex_bend = scene.get_vertex_bend(entity);
            item.entity_id = id;
            item.transform_revision = world.revision;
            item.lod.reset();
            if(entity.has_component<MeshLodComponent>())
                item.lod = entity.get_component<MeshLodComponent>();
            item.contributes_to_gi = mesh.contributes_to_gi && item.vertex_bend.z <= 0;
            item.outline = entity.has_component<OutlineComponent>()
                               ? entity.get_component<OutlineComponent>()
                               : OutlineSettings{.enabled = false};
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
        output.item_changes = {};
        extract_all(scene, output, nullptr);
    }

    void SceneExtractor::extract_all(Scene& scene, RenderScene& output, SceneExtractor* slots) {
        scene.update_world_transforms();
        const bool same_scene = output.scene_lifetime == scene.get_lifetime();
        output.scene_lifetime = scene.get_lifetime();
        const bool interpolating = scene.m_transform_interpolation.active();
        extract_views(scene, output, interpolating);
        // 复用已有快照槽位；查询过滤掉缺少 Transform 的实体后再收缩。
        output.render_items.resize(scene.component_count<MeshRendererComponent>());
        if(slots) {
            slots->m_entity_indices_by_slot.resize(output.render_items.size());
            slots->m_slots_by_entity.reserve(scene.entity_count());
        }
        std::size_t item_count = 0;
        scene.each<const MeshRendererComponent, const TransformComponent, WorldTransformComponent,
            IdComponent>([&](Entity entity, const MeshRendererComponent& mesh,
                             const TransformComponent&,
                             const WorldTransformComponent& authoritative, const IdComponent& id) {
            const auto& world = interpolating ? scene.get_render_transform(entity) : authoritative;
            extract_item(
                scene, entity, mesh, world, id.id, same_scene, output.render_items[item_count]);
            if(slots) {
                const auto index = entt::to_entity(entity.m_handle);
                if(index >= slots->m_slots_by_entity.size())
                    slots->m_slots_by_entity.resize(index + 1);
                slots->m_slots_by_entity[index] = item_count;
                slots->m_entity_indices_by_slot[item_count] = index;
            }
            ++item_count;
        });
        output.render_items.resize(item_count);
        if(slots) {
            slots->m_entity_indices_by_slot.resize(item_count);
            slots->m_matches_storage_order = true;
        }
    }

    void SceneExtractor::reset() noexcept {
        m_output.reset();
        m_scene_version = {};
        m_matches_storage_order = false;
        m_slots_by_entity.clear();
        m_entity_indices_by_slot.clear();
    }

    void SceneExtractor::update_items(Scene& scene, RenderScene& output) {
        auto& items = output.render_items;
        auto& changes = output.item_changes;
        const bool interpolating = scene.m_transform_interpolation.active();
        for(const auto& change : scene.render_changes().entities) {
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
                    m_entity_indices_by_slot.push_back(index);
                    items.emplace_back();
                    changes.structure.push_back(
                        {RenderItemChanges::StructuralEdit::Type::Append, slot});
                    m_matches_storage_order = false;
                }
                extract_item(scene, entity, entity.get_component<MeshRendererComponent>(),
                    interpolating ? scene.get_render_transform(entity)
                                  : entity.get_component<WorldTransformComponent>(),
                    change.id, true, items[slot]);
                changes.slots.push_back(slot);
            } else if(slot < items.size()) {
                changes.structure.push_back(
                    {RenderItemChanges::StructuralEdit::Type::Remove, slot});
                if(slot + 1 != items.size()) {
                    items[slot] = std::move(items.back());
                    const auto moved = m_entity_indices_by_slot.back();
                    m_entity_indices_by_slot[slot] = moved;
                    m_slots_by_entity[moved] = slot;
                    changes.slots.push_back(slot);
                }
                items.pop_back();
                m_entity_indices_by_slot.pop_back();
                m_matches_storage_order = false;
            }
        }
        if(!changes.structure.empty()) {
            auto& changed = changes.slots;
            std::erase_if(changed, [&](const auto slot) { return slot >= items.size(); });
            std::ranges::sort(changed);
            changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
        }
    }

    std::shared_ptr<const RenderScene> SceneExtractor::update(Scene& scene) {
        const VersionStamp version{scene.get_lifetime(), scene.get_render_revision()};
        if(m_scene_version == version)
            return m_output;
        if(m_scene_version.identity != version.identity)
            m_output.reset();
        auto& output = writable_snapshot(m_output);
        const auto batch = scene.render_changes();
        const bool full = m_scene_version.identity != version.identity
                          || m_scene_version.revision != batch.base_revision || batch.full_update;
        output.item_changes.begin_update(full);
        if(full) {
            const bool rebuild_slots =
                !m_matches_storage_order || m_scene_version.identity != version.identity
                || m_scene_version.revision != batch.base_revision || batch.structure_changed;
            SceneExtractor* slots = nullptr;
            if(rebuild_slots)
                slots = this;
            extract_all(scene, output, slots);
        } else {
            scene.update_world_transforms();
            extract_views(scene, output, scene.m_transform_interpolation.active());
            update_items(scene, output);
            if(batch.structure_changed)
                m_matches_storage_order = false;
        }
        m_scene_version = {version.identity, batch.revision};
        scene.acknowledge_render_changes(m_scene_version.revision,
            std::clamp<std::size_t>(scene.component_count<MeshRendererComponent>() / 4, 8, 64));
        return m_output;
    }
}
