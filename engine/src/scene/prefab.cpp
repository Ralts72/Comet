#include "scene/prefab.h"
#include "scene/scene_serializer.h"
#include "common/file_io.h"
#include "common/json.h"
#include "common/scope_exit.h"
#include <unordered_set>

namespace Comet {
    namespace {
        Entity single_root(Scene& scene) {
            Entity root;
            for(const auto entity : scene.get_entities()) {
                if(scene.get_parent(entity))
                    continue;
                if(root)
                    return {};
                root = entity;
            }
            return root;
        }

        bool restore_mapped(Scene& scene, const ComponentRegistry& registry,
            EntityTrees::Snapshot entities, const EntityTrees::ReferenceRemap& mapping) {
            for(auto& entity : entities) {
                const auto found = mapping.find(entity.uuid);
                if(found == mapping.end())
                    return false;
                entity.uuid = found->second;
                if(entity.parent) {
                    const auto parent = mapping.find(entity.parent);
                    if(parent == mapping.end())
                        return false;
                    entity.parent = parent->second;
                }
            }
            return EntityTrees::restore(scene, registry, entities, mapping);
        }

        bool same_component(const ComponentDescriptor& descriptor, Entity a, Entity b) {
            if(descriptor.has_component(a) != descriptor.has_component(b))
                return false;
            if(!descriptor.has_component(a))
                return true;
            for(const auto& property : descriptor.properties) {
                if(property.serializable && !property.transient
                    && property.copy_value(descriptor.get_component(a))
                           != property.copy_value(descriptor.get_component(b)))
                    return false;
            }
            return true;
        }

        bool same_entity(const ComponentRegistry& registry, Entity a, Entity b) {
            if(a.get_component<NameComponent>().name != b.get_component<NameComponent>().name)
                return false;
            for(const auto& component : registry.components())
                if(component.serializable && !same_component(component, a, b))
                    return false;
            return true;
        }

        PropertyValue merge_property(const PropertyValue& before, const PropertyValue& current,
            const PropertyValue& incoming) {
            const auto* old_parameters = std::get_if<ParameterMap>(&before);
            const auto* current_parameters = std::get_if<ParameterMap>(&current);
            const auto* new_parameters = std::get_if<ParameterMap>(&incoming);
            if(!old_parameters || !current_parameters || !new_parameters)
                return current == before ? incoming : current;
            auto merged = *current_parameters;
            for(const auto& [name, value] : *old_parameters) {
                const auto local = merged.find(name);
                if(local == merged.end() || local->second != value)
                    continue;
                const auto replacement = new_parameters->find(name);
                if(replacement == new_parameters->end())
                    merged.erase(local);
                else
                    local->second = replacement->second;
            }
            for(const auto& [name, value] : *new_parameters)
                if(!old_parameters->contains(name) && !merged.contains(name))
                    merged.emplace(name, value);
            return merged;
        }

        bool merge_component(
            const ComponentDescriptor& component, Entity before, Entity current, Entity incoming) {
            const bool was_present = before && component.has_component(before);
            const bool is_present = component.has_component(current);
            const bool source_present = component.has_component(incoming);
            if(!was_present) {
                return is_present || !source_present
                       || component.restore_component(
                           current, component.capture_component(incoming));
            }
            if(!is_present)
                return true; // 本地删除组件。
            if(!source_present)
                return !same_component(component, before, current)
                       || component.remove_component(current);
            for(const auto& property : component.properties) {
                if(!property.serializable || property.transient)
                    continue;
                const auto original = property.copy_value(component.get_component(before));
                const auto local = property.copy_value(component.get_component(current));
                const auto next = property.copy_value(component.get_component(incoming));
                if(!original || !local || !next)
                    return false;
                const auto merged = merge_property(*original, *local, *next);
                if(merged != *local
                    && !component.assign_property(
                        current, property.id, merged, PropertyDescriptor::WriteMode::Restore))
                    return false;
            }
            return true;
        }

        bool references_any(Entity entity, const ComponentRegistry& registry,
            const std::unordered_set<EntityUuid>& targets) {
            for(const auto& component : registry.components()) {
                if(!component.has_component(entity))
                    continue;
                for(const auto& property : component.properties) {
                    if(!property.serializable || property.transient)
                        continue;
                    const auto value = property.copy_value(component.get_component(entity));
                    if(!value)
                        continue;
                    bool found = false;
                    visit_entity_references(
                        *value, [&](EntityUuid uuid) { found |= targets.contains(uuid); });
                    if(found)
                        return true;
                }
            }
            return false;
        }
    }

    Result<Prefab> Prefab::capture(Scene& scene, const ComponentRegistry& registry, Entity root) {
        auto entities = EntityTrees::capture(scene, registry, root);
        if(!entities)
            return Result<Prefab>::failure("Cannot capture prefab subtree");
        entities->front().parent = {};
        // 保存为独立资产时展开已有实例，不隐式创建嵌套 Prefab。
        for(auto& entity : *entities)
            entity.prefab.reset();
        Scene isolated;
        if(!EntityTrees::restore(isolated, registry, *entities))
            return Result<Prefab>::failure("Cannot restore prefab subtree");
        // 存储的是内容，不将脚本实例或其他临时状态复制到后续实例。
        auto content = SceneSerializer(registry).clone(isolated);
        if(!content)
            return Result<Prefab>::failure(content.error());
        auto snapshot = EntityTrees::capture(
            *content.value(), registry, content.value()->find_entity(root.get_uuid()));
        if(!snapshot
            || !EntityTrees::has_only_internal_references(*content.value(), registry, *snapshot))
            return Result<Prefab>::failure("Prefab contains entity references outside its subtree");
        return Result<Prefab>::success(Prefab(std::move(*snapshot)));
    }

    Result<Prefab> Prefab::from_model(const ModelData& model, AssetHandle source,
        const ComponentRegistry& registry, const std::string& name) {
        if(!source)
            return Result<Prefab>::failure("Model source handle must not be zero");
        Scene scene;
        auto root = scene.instantiate_model_content(model, name, {}, std::nullopt, source);
        if(!root)
            return Result<Prefab>::failure(root.error());
        return capture(scene, registry, root.value());
    }

    Result<Prefab> Prefab::deserialize(
        std::string_view contents, const ComponentRegistry& registry, std::string_view source) {
        simdjson::dom::parser parser;
        const Json::Context context("prefab", source);
        auto parsed = context.parse(parser, contents);
        if(!parsed)
            return Result<Prefab>::failure(parsed.error());
        if(auto valid = context.validate_keys(parsed.value(), {"version", "entities"}, "<root>");
            !valid)
            return Result<Prefab>::failure(valid.error());
        auto scene = SceneSerializer(registry).deserialize(contents, source);
        if(!scene)
            return Result<Prefab>::failure(scene.error());
        for(const auto entity : scene.value()->get_entities())
            if(entity.has_component<PrefabInstanceComponent>())
                return Result<Prefab>::failure("Prefab source must not contain instance metadata");
        auto root = single_root(*scene.value());
        if(!root)
            return Result<Prefab>::failure("Prefab must contain exactly one root entity");
        auto entities = EntityTrees::capture(*scene.value(), registry, root);
        if(!entities
            || !EntityTrees::has_only_internal_references(*scene.value(), registry, *entities))
            return Result<Prefab>::failure("Prefab contains entity references outside its subtree");
        return Result<Prefab>::success(Prefab(std::move(*entities)));
    }

    Result<Prefab> Prefab::load(
        const std::filesystem::path& path, const ComponentRegistry& registry) {
        auto contents = read_text_file(path);
        if(!contents)
            return Result<Prefab>::failure(contents.error());
        return deserialize(contents.value(), registry, path.string());
    }

    Result<std::string> Prefab::serialize(const ComponentRegistry& registry) const {
        Scene scene;
        if(!EntityTrees::restore(scene, registry, m_entities))
            return Result<std::string>::failure("Cannot restore prefab content for serialization");
        return SceneSerializer(registry).serialize_entities(scene);
    }

    Result<void> Prefab::save(
        const std::filesystem::path& path, const ComponentRegistry& registry) const {
        auto text = serialize(registry);
        if(!text)
            return Result<void>::failure(text.error());
        return write_text_file_atomic(path, text.value());
    }

    Result<Entity> Prefab::instantiate(Scene& scene, const ComponentRegistry& registry,
        std::optional<Math::Vec3> root_position, EntityUuid parent, AssetHandle source,
        AssetType source_type, EntityUuid reserved_root) const {
        if(source_type != AssetType::Prefab && source_type != AssetType::Model)
            return Result<Entity>::failure("Invalid prefab source type");
        if(root_position && !Math::is_finite(*root_position))
            return Result<Entity>::failure("Invalid prefab placement");
        auto entities = m_entities;
        auto remap = EntityTrees::make_instance_ids(entities, scene, parent, reserved_root);
        if(!remap || !EntityTrees::restore(scene, registry, entities, *remap))
            return Result<Entity>::failure("Cannot instantiate prefab subtree");
        auto root = scene.find_entity(entities.front().uuid);
        ScopeExit rollback([&] { scene.destroy_entity(root); });
        if(root_position && !root.try_edit_transform([&](auto& transform) {
               transform.translation = *root_position;
           }))
            return Result<Entity>::failure("Prefab root needs a transform for placement");
        if(source)
            root.add_component<PrefabInstanceComponent>(PrefabInstanceComponent{
                source, std::make_shared<const Prefab>(*this), std::move(*remap), source_type});
        rollback.release();
        return Result<Entity>::success(root);
    }
    Result<EntityTrees::Snapshot> Prefab::update_candidate(Scene& scene,
        const ComponentRegistry& registry, Entity instance_root, UpdateMode mode) const {
        using Update = Result<EntityTrees::Snapshot>;
        if(!scene.is_valid(instance_root)
            || !instance_root.has_component<PrefabInstanceComponent>())
            return Update::failure("Entity is not a prefab instance root");
        const auto& link = instance_root.get_component<PrefabInstanceComponent>();
        if(!link.asset || !link.base || root_uuid() != link.base->root_uuid()
            || link.entities.size() != link.base->entity_count())
            return Update::failure("Prefab source root identity has changed");
        auto current = EntityTrees::capture(scene, registry, instance_root);
        if(!current)
            return Update::failure("Cannot capture prefab instance");
        const auto external_parent = current->front().parent;
        current->front().parent = {};
        current->front().prefab.reset();
        Scene candidate, baseline, incoming;
        if(!EntityTrees::restore(candidate, registry, *current)
            || !restore_mapped(baseline, registry, link.base->m_entities, link.entities))
            return Update::failure("Cannot restore prefab baseline");
        const auto root_mapping = link.entities.find(root_uuid());
        if(root_mapping == link.entities.end() || root_mapping->second != instance_root.get_uuid())
            return Update::failure("Prefab instance root identity mismatch");
        std::unordered_set<EntityUuid> occupied, source_ids, removed;
        for(const auto& [source, target] : link.entities) {
            if(!target || !occupied.insert(target).second)
                return Update::failure("Invalid prefab instance identity");
            if(scene.find_entity(target) && !candidate.find_entity(target))
                return Update::failure(
                    "Prefab member was moved outside its instance; move it back or detach the instance");
        }
        EntityTrees::ReferenceRemap mapping;
        for(const auto& source : m_entities) {
            source_ids.insert(source.uuid);
            EntityUuid id;
            if(const auto found = link.entities.find(source.uuid); found != link.entities.end())
                id = found->second;
            else {
                do {
                    id = EntityUuid::generate();
                } while(scene.find_entity(id) || occupied.contains(id));
                occupied.insert(id);
            }
            mapping.emplace(source.uuid, id);
        }
        if(!restore_mapped(incoming, registry, m_entities, mapping))
            return Update::failure("Cannot restore new prefab content");
        if(mode == UpdateMode::RevertOverrides) {
            for(const auto& entity : *current)
                if(!incoming.find_entity(entity.uuid))
                    removed.insert(entity.uuid);
            for(const auto entity : scene.get_entities())
                if(!candidate.find_entity(entity.get_uuid())
                    && references_any(entity, registry, removed))
                    return Update::failure(
                        "Cannot revert: local entities are referenced by the level");
            auto root = incoming.find_entity(instance_root.get_uuid());
            if(instance_root.has_component<TransformComponent>()) {
                const auto placement = instance_root.get_component<TransformComponent>();
                if(root.has_component<TransformComponent>())
                    root.set_transform(placement);
                else
                    root.add_component<TransformComponent>(placement);
            }
            root.add_component<PrefabInstanceComponent>(PrefabInstanceComponent{link.asset,
                std::make_shared<const Prefab>(*this), std::move(mapping), link.source_type});
            auto restored = EntityTrees::capture(incoming, registry, root);
            if(!restored)
                return Update::failure("Cannot capture reverted prefab");
            restored->front().parent = external_parent;
            return Update::success(std::move(*restored));
        }
        for(const auto& [source, target] : link.entities)
            if(!source_ids.contains(source) && candidate.find_entity(target))
                removed.insert(target);

        for(const auto& source : m_entities) {
            const auto id = mapping.at(source.uuid);
            auto local = candidate.find_entity(id);
            const auto original = baseline.find_entity(id);
            const auto next = incoming.find_entity(id);
            if(!local && original)
                continue; // 本地删除节点，不在源更新时重新生成。
            if(!local) {
                if(source.parent && !candidate.find_entity(mapping.at(source.parent)))
                    continue; // 新节点位于本地删除的父节点下。
                local = candidate.create_entity_with_uuid(id, source.name);
                local.remove_component<TransformComponent>();
            }
            if(!original
                || local.get_component<NameComponent>().name
                       == original.get_component<NameComponent>().name)
                local.get_component<NameComponent>().name =
                    next.get_component<NameComponent>().name;
            for(const auto& component : registry.components()) {
                if(!component.serializable
                    || (id == instance_root.get_uuid() && component.id == "transform"))
                    continue;
                if(!merge_component(component, original, local, next))
                    return Update::failure("Cannot merge prefab component: " + component.id);
            }
        }
        for(const auto& source : m_entities) {
            const auto id = mapping.at(source.uuid);
            auto local = candidate.find_entity(id);
            if(!local || id == instance_root.get_uuid())
                continue;
            const auto original = baseline.find_entity(id);
            const auto previous_parent = original ? baseline.get_parent(original) : Entity{};
            const auto local_parent = candidate.get_parent(local);
            if(original && local_parent.get_uuid() != previous_parent.get_uuid())
                continue; // 保留本地重挂接。
            const auto next_parent = candidate.find_entity(mapping.at(source.parent));
            if(!next_parent || !candidate.set_parent(local, next_parent))
                return Update::failure("Prefab hierarchy conflicts with local changes");
        }
        for(const auto id : removed) {
            const auto local = candidate.find_entity(id);
            const auto original = baseline.find_entity(id);
            if(!same_entity(registry, original, local)
                || candidate.get_parent(local).get_uuid()
                       != baseline.get_parent(original).get_uuid())
                return Update::failure(
                    "Source removes a locally edited entity; detach or revert the instance first");
            for(const auto child : candidate.get_children(local))
                if(!removed.contains(child.get_uuid()))
                    return Update::failure(
                        "Source removes the parent of local content; detach or reparent that content first");
        }
        for(const auto entity : candidate.get_entities())
            if(!removed.contains(entity.get_uuid()) && references_any(entity, registry, removed))
                return Update::failure(
                    "Source removal conflicts with an instance entity reference");
        for(const auto entity : scene.get_entities())
            if(!candidate.find_entity(entity.get_uuid())
                && references_any(entity, registry, removed))
                return Update::failure("Source removal conflicts with a reference from the level");
        for(const auto id : removed)
            if(const auto entity = candidate.find_entity(id))
                candidate.destroy_entity(entity);
        auto root = candidate.find_entity(instance_root.get_uuid());
        root.add_component<PrefabInstanceComponent>(PrefabInstanceComponent{link.asset,
            std::make_shared<const Prefab>(*this), std::move(mapping), link.source_type});
        auto result = EntityTrees::capture(candidate, registry, root);
        if(!result || result->size() != candidate.entity_count())
            return Update::failure("Prefab update would leave detached entities");
        if(auto valid = SceneSerializer(registry).clone(candidate); !valid)
            return Update::failure(valid.error());
        result->front().parent = external_parent;
        return Update::success(std::move(*result));
    }

    Result<PrefabSourceUpdate> Prefab::prepare_source_update(
        Scene& scene, const ComponentRegistry& registry, Entity instance_root) const {
        using Update = Result<PrefabSourceUpdate>;
        auto merged = update_candidate(scene, registry, instance_root);
        if(!merged)
            return Update::failure(merged.error());
        auto instance = std::move(merged).value();
        const auto link = instance.front().prefab;
        if(link->source_type != AssetType::Prefab)
            return Update::failure("Imported model sources must be edited in the modeling tool");
        EntityTrees::ReferenceRemap to_source, to_instance;
        std::unordered_set<EntityUuid> occupied;
        for(const auto& [source, target] : link->entities) {
            to_source.emplace(target, source);
            occupied.insert(source);
        }
        for(const auto& entity : instance) {
            auto source = to_source.find(entity.uuid);
            if(source == to_source.end()) {
                EntityUuid id;
                do {
                    id = EntityUuid::generate();
                } while(occupied.contains(id));
                occupied.insert(id);
                source = to_source.emplace(entity.uuid, id).first;
            }
            to_instance.emplace(source->second, entity.uuid);
        }
        auto content = instance;
        content.front().parent = {};
        for(auto& entity : content)
            entity.prefab.reset();
        // 场景中的根变换用于放置，写回源内容时保留原来的根变换。
        auto& root_components = content.front().components;
        std::erase_if(
            root_components, [](const auto& component) { return component.id == "transform"; });
        for(const auto& component : m_entities.front().components)
            if(component.id == "transform")
                root_components.push_back(component);
        Scene source_scene;
        if(!restore_mapped(source_scene, registry, std::move(content), to_source))
            return Update::failure("Cannot map instance content back to its prefab source");
        auto source = capture(source_scene, registry, source_scene.find_entity(root_uuid()));
        if(!source)
            return Update::failure(source.error());
        for(auto& entity : instance)
            entity.prefab.reset();
        instance.front().prefab = std::make_shared<const PrefabInstanceComponent>(
            PrefabInstanceComponent{link->asset, std::make_shared<const Prefab>(source.value()),
                std::move(to_instance), link->source_type});
        return Update::success(PrefabSourceUpdate{std::move(source).value(), std::move(instance)});
    }

}
