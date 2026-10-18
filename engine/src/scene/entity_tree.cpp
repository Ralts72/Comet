#include "scene/entity_tree.h"
#include "scene/prefab.h"
#include "common/scope_exit.h"
#include <unordered_set>

namespace Comet::EntityTrees {
    static bool remap_reference(EntityUuid& uuid, const ReferenceRemap& remap) {
        const auto found = remap.find(uuid);
        if(found == remap.end())
            return false;
        uuid = found->second;
        return true;
    }

    static bool remap_entity_references(
        Entity entity, const ComponentRegistry& registry, const ReferenceRemap& remap) {
        if(remap.empty())
            return true;
        for(const auto& component : registry.components()) {
            const auto* data = component.get_component(entity);
            if(!data)
                continue;
            for(const auto& property : component.properties) {
                if(!property.has_entity_references())
                    continue;
                auto value = property.copy_value(data);
                if(!value)
                    return false;
                bool changed = false;
                visit_entity_references(
                    *value, [&](EntityUuid& uuid) { changed |= remap_reference(uuid, remap); });
                if(changed
                    && !component.assign_property(
                        entity, property.id, *value, PropertyDescriptor::WriteMode::Restore))
                    return false;
            }
        }
        return true;
    }

    std::optional<std::vector<EntitySnapshot>> capture(
        Scene& scene, const ComponentRegistry& registry, Entity root) {
        if(!scene.is_valid(root))
            return std::nullopt;
        std::vector<EntitySnapshot> result;
        std::vector<Entity> pending{root};
        for(std::size_t i = 0; i < pending.size(); ++i) {
            const auto entity = pending[i];
            if(!registry.covers_entity(entity))
                return std::nullopt;
            const auto parent = scene.get_parent(entity);
            EntitySnapshot snapshot{.uuid = entity.get_uuid(),
                .parent = parent ? parent.get_uuid() : EntityUuid{},
                .name = entity.get_component<NameComponent>().name};
            if(entity.has_component<PrefabInstanceComponent>())
                snapshot.prefab = std::make_shared<const PrefabInstanceComponent>(
                    entity.get_component<PrefabInstanceComponent>());
            for(const auto& component : registry.components()) {
                if(component.id == "name" || !component.has_component(entity))
                    continue;
                if(!component.restore_component_callback)
                    return std::nullopt;
                auto value = component.capture_component(entity);
                if(!value.has_value())
                    return std::nullopt;
                snapshot.components.push_back({component.id, std::move(value)});
            }
            result.push_back(std::move(snapshot));
            const auto children = scene.get_children(entity);
            pending.insert(pending.end(), children.begin(), children.end());
        }
        return result;
    }

    std::optional<std::vector<EntityUuid>> roots(
        Scene& scene, std::span<const EntityUuid> entities) {
        if(entities.empty())
            return std::nullopt;
        std::unordered_set<EntityUuid> selected(entities.begin(), entities.end());
        for(const auto uuid : selected)
            if(!scene.find_entity(uuid))
                return std::nullopt;
        std::vector<EntityUuid> result;
        std::unordered_set<EntityUuid> visited;
        for(const auto uuid : entities) {
            if(!visited.insert(uuid).second)
                continue;
            auto entity = scene.find_entity(uuid);
            auto ancestor = scene.get_parent(entity);
            while(ancestor && !selected.contains(ancestor.get_uuid()))
                ancestor = scene.get_parent(ancestor);
            if(ancestor)
                continue;
            result.push_back(uuid);
        }
        return result;
    }

    std::optional<Snapshot> capture(
        Scene& scene, const ComponentRegistry& registry, std::span<const EntityUuid> entities) {
        const auto selected_roots = roots(scene, entities);
        if(!selected_roots)
            return std::nullopt;
        Snapshot result;
        for(const auto uuid : *selected_roots) {
            auto tree = capture(scene, registry, scene.find_entity(uuid));
            if(!tree)
                return std::nullopt;
            result.insert(result.end(), std::make_move_iterator(tree->begin()),
                std::make_move_iterator(tree->end()));
        }
        return result;
    }

    std::vector<EntityUuid> roots(const Snapshot& snapshots) {
        std::unordered_set<EntityUuid> members;
        for(const auto& snapshot : snapshots)
            members.insert(snapshot.uuid);
        std::vector<EntityUuid> result;
        for(const auto& snapshot : snapshots)
            if(!members.contains(snapshot.parent))
                result.push_back(snapshot.uuid);
        return result;
    }

    bool restore(Scene& scene, const ComponentRegistry& registry,
        const std::vector<EntitySnapshot>& snapshots, const ReferenceRemap& remap) {
        std::unordered_set<EntityUuid> uuids;
        for(const auto& snapshot : snapshots) {
            if(!snapshot.uuid || scene.find_entity(snapshot.uuid)
                || !uuids.insert(snapshot.uuid).second)
                return false;
            for(const auto& value : snapshot.components) {
                const auto* component = registry.find_component(value.id);
                if(!component || !component->restore_component_callback)
                    return false;
            }
        }
        for(const auto& snapshot : snapshots) {
            if(snapshot.parent && !uuids.contains(snapshot.parent)
                && !scene.find_entity(snapshot.parent))
                return false;
        }

        std::vector<Entity> created;
        created.reserve(snapshots.size());
        ScopeExit rollback([&]() {
            for(auto it = created.rbegin(); it != created.rend(); ++it)
                scene.destroy_entity(*it);
        });
        for(const auto& snapshot : snapshots) {
            auto entity = scene.create_entity_with_uuid(snapshot.uuid, snapshot.name);
            if(!entity) {
                return false;
            }
            created.push_back(entity);
            if(snapshot.prefab) {
                auto instance = *snapshot.prefab;
                for(auto& [source, target] : instance.entities)
                    remap_reference(target, remap);
                entity.add_component<PrefabInstanceComponent>(std::move(instance));
            }
            // 空名称和缺少 Transform 的源实体也必须原样恢复。
            entity.get_component<NameComponent>().name = snapshot.name;
            entity.remove_component<TransformComponent>();
            for(const auto& value : snapshot.components) {
                if(!registry.find_component(value.id)->restore_component(entity, value.value)) {
                    return false;
                }
            }
        }
        for(const auto& snapshot : snapshots) {
            if(!remap_entity_references(scene.find_entity(snapshot.uuid), registry, remap))
                return false;
            if(snapshot.parent
                && !scene.set_parent(
                    scene.find_entity(snapshot.uuid), scene.find_entity(snapshot.parent))) {
                return false;
            }
        }
        rollback.release();
        return true;
    }

    std::optional<ReferenceRemap> make_instance_ids(Snapshot& snapshots, Scene& scene,
        std::optional<EntityUuid> parent, EntityUuid reserved_root) {
        if(snapshots.empty() || (parent && *parent && !scene.find_entity(*parent)))
            return std::nullopt;
        ReferenceRemap remap;
        std::unordered_set<EntityUuid> source_uuids;
        std::unordered_set<EntityUuid> reserved;
        remap.reserve(snapshots.size());
        source_uuids.reserve(snapshots.size());
        reserved.reserve(snapshots.size());
        for(const auto& snapshot : snapshots)
            if(!snapshot.uuid || !source_uuids.insert(snapshot.uuid).second)
                return std::nullopt;
        if(reserved_root) {
            if(scene.find_entity(reserved_root) || source_uuids.contains(reserved_root)
                || roots(snapshots).size() != 1)
                return std::nullopt;
            remap.emplace(snapshots.front().uuid, reserved_root);
            reserved.insert(reserved_root);
        }
        for(const auto& snapshot : snapshots) {
            if(remap.contains(snapshot.uuid))
                continue;
            EntityUuid uuid;
            do {
                uuid = EntityUuid::generate();
            } while(
                scene.find_entity(uuid) || source_uuids.contains(uuid) || reserved.contains(uuid));
            reserved.insert(uuid);
            remap.emplace(snapshot.uuid, uuid);
        }
        for(const auto& snapshot : snapshots) {
            if(!snapshot.prefab)
                continue;
            for(const auto& [source, target] : snapshot.prefab->entities) {
                if(remap.contains(target))
                    continue;
                // 不能把另一个子树的活实体带入复制后的实例关系。
                if(scene.find_entity(target))
                    return std::nullopt;
                EntityUuid replacement;
                do {
                    replacement = EntityUuid::generate();
                } while(scene.find_entity(replacement) || source_uuids.contains(replacement)
                        || reserved.contains(replacement));
                reserved.insert(replacement);
                remap.emplace(target, replacement);
            }
        }
        for(auto& snapshot : snapshots) {
            snapshot.uuid = remap.at(snapshot.uuid);
            if(const auto found = remap.find(snapshot.parent); found != remap.end())
                snapshot.parent = found->second;
            else if(parent)
                snapshot.parent = *parent;
        }
        return remap;
    }

    bool has_only_internal_references(
        Scene& scene, const ComponentRegistry& registry, const Snapshot& snapshots) {
        std::unordered_set<EntityUuid> ids;
        for(const auto& snapshot : snapshots)
            ids.insert(snapshot.uuid);
        const auto valid = [&](EntityUuid uuid) { return !uuid || ids.contains(uuid); };
        for(const auto& snapshot : snapshots) {
            const auto entity = scene.find_entity(snapshot.uuid);
            if(!entity)
                return false;
            for(const auto& component : registry.components()) {
                const auto* data = component.get_component(entity);
                if(!data)
                    continue;
                for(const auto& property : component.properties) {
                    if(!property.has_entity_references())
                        continue;
                    auto value = property.copy_value(data);
                    if(!value)
                        return false;
                    bool internal = true;
                    visit_entity_references(
                        *value, [&](EntityUuid uuid) { internal &= valid(uuid); });
                    if(!internal)
                        return false;
                }
            }
        }
        return true;
    }
}
