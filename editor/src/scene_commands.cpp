#include "scene_commands.h"

#include <unordered_set>
#include <unordered_map>

namespace CometEditor::SceneCommands {
    namespace {
        struct EntitySnapshot {
            struct Component {
                std::string id;
                std::any value;
            };
            Comet::EntityUuid uuid;
            Comet::EntityUuid parent;
            std::string name;
            std::vector<Component> components;
        };

        std::optional<std::vector<EntitySnapshot>> capture_tree(Comet::Scene& scene,
            const Comet::ComponentRegistry& registry, Comet::Entity root) {
            if(!root)
                return std::nullopt;
            std::vector<EntitySnapshot> result;
            std::vector<Comet::Entity> pending{root};
            for(std::size_t i = 0; i < pending.size(); ++i) {
                const auto entity = pending[i];
                if(!registry.covers_entity(entity))
                    return std::nullopt;
                const auto parent = scene.get_parent(entity);
                EntitySnapshot snapshot{.uuid = entity.get_uuid(),
                    .parent = parent ? parent.get_uuid() : Comet::EntityUuid{},
                    .name = entity.get_component<Comet::NameComponent>().name};
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

        bool restore_tree(Comet::Scene& scene, const Comet::ComponentRegistry& registry,
            const std::vector<EntitySnapshot>& snapshots) {
            std::unordered_set<Comet::EntityUuid> uuids;
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

            std::vector<Comet::Entity> created;
            created.reserve(snapshots.size());
            const auto rollback = [&]() {
                for(auto it = created.rbegin(); it != created.rend(); ++it)
                    scene.destroy_entity(*it);
            };
            try {
                for(const auto& snapshot : snapshots) {
                    auto entity =
                        scene.create_entity_with_uuid(snapshot.uuid, snapshot.name);
                    if(!entity) {
                        rollback();
                        return false;
                    }
                    created.push_back(entity);
                    // 空名称和缺少 Transform 的源实体也必须原样恢复。
                    entity.get_component<Comet::NameComponent>().name = snapshot.name;
                    entity.remove_component<Comet::TransformComponent>();
                    for(const auto& value : snapshot.components) {
                        if(!registry.find_component(value.id)->restore_component(
                               entity, value.value)) {
                            rollback();
                            return false;
                        }
                    }
                }
                for(const auto& snapshot : snapshots) {
                    if(snapshot.parent
                        && !scene.set_parent(scene.find_entity(snapshot.uuid),
                            scene.find_entity(snapshot.parent))) {
                        rollback();
                        return false;
                    }
                }
            } catch(...) {
                rollback();
                throw;
            }
            return true;
        }

        class EntityTreeCommand final: public CommandHistory::Command {
        public:
            EntityTreeCommand(const Comet::ComponentRegistry& registry,
                std::vector<EntitySnapshot> snapshots, bool creating)
                : m_registry(registry), m_snapshots(std::move(snapshots)),
                  m_creating(creating) {}

            bool undo(Comet::Scene& scene) override { return apply(scene, !m_creating); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_creating); }

        private:
            bool apply(Comet::Scene& scene, bool present) {
                if(present)
                    return restore_tree(scene, m_registry, m_snapshots);
                auto root = scene.find_entity(m_snapshots.front().uuid);
                auto current = capture_tree(scene, m_registry, root);
                if(!current)
                    return false;
                // 历史以外添加/移动的子节点不能被顺带删除。
                if(current->size() != m_snapshots.size())
                    return false;
                std::unordered_set<Comet::EntityUuid> expected;
                for(const auto& snapshot : m_snapshots)
                    expected.insert(snapshot.uuid);
                for(const auto& snapshot : *current) {
                    if(!expected.contains(snapshot.uuid))
                        return false;
                }
                scene.destroy_entity(root);
                m_snapshots = std::move(*current);
                return true;
            }

            const Comet::ComponentRegistry& m_registry;
            std::vector<EntitySnapshot> m_snapshots;
            bool m_creating;
        };

        class ReparentCommand final: public CommandHistory::Command {
        public:
            ReparentCommand(Comet::EntityUuid entity, Comet::EntityUuid before,
                Comet::EntityUuid after)
                : m_entity(entity), m_before(before), m_after(after) {}
            bool undo(Comet::Scene& scene) override { return apply(scene, m_before); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_after); }

        private:
            bool apply(Comet::Scene& scene, Comet::EntityUuid parent) const {
                auto entity = scene.find_entity(m_entity);
                if(!entity)
                    return false;
                if(parent)
                    return scene.set_parent(entity, scene.find_entity(parent));
                return scene.clear_parent(entity);
            }
            Comet::EntityUuid m_entity;
            Comet::EntityUuid m_before;
            Comet::EntityUuid m_after;
        };

        class ComponentCommand final: public CommandHistory::Command {
        public:
            ComponentCommand(const Comet::ComponentRegistry& registry,
                Comet::EntityUuid entity, std::string component, bool adding)
                : m_registry(registry), m_entity(entity),
                  m_component(std::move(component)), m_adding(adding) {}

            bool undo(Comet::Scene& scene) override { return apply(scene, !m_adding); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_adding); }

        private:
            bool apply(Comet::Scene& scene, bool present) {
                auto entity = scene.find_entity(m_entity);
                const auto* component = m_registry.find_component(m_component);
                if(!entity || !component || !can_edit_component_structure(*component)
                    || component->has_component(entity) == present)
                    return false;
                if(present) {
                    if(m_snapshot.has_value())
                        return component->restore_component(entity, m_snapshot);
                    if(!component->add_component(entity))
                        return false;
                    // Redo 使用初始快照，不重新取默认值。
                    try {
                        m_snapshot = component->capture_component(entity);
                    } catch(...) {
                        static_cast<void>(component->remove_component(entity));
                        throw;
                    }
                    if(!m_snapshot.has_value()) {
                        static_cast<void>(component->remove_component(entity));
                        return false;
                    }
                    return true;
                }
                auto snapshot = component->capture_component(entity);
                if(!snapshot.has_value() || !component->remove_component(entity))
                    return false;
                m_snapshot = std::move(snapshot);
                return true;
            }

            const Comet::ComponentRegistry& m_registry;
            Comet::EntityUuid m_entity;
            std::string m_component;
            bool m_adding;
            std::any m_snapshot;
        };

        Comet::EntityUuid create_from_components(CommandHistory& history,
            const Comet::ComponentRegistry& registry, std::string name,
            std::vector<EntitySnapshot::Component> components,
            const Comet::EntityUuid parent = {}) {
            auto* scene = history.get_scene();
            if(!scene)
                return {};
            Comet::EntityUuid uuid;
            do {
                uuid = Comet::EntityUuid::generate();
            } while(scene->find_entity(uuid));
            if(name.empty())
                name = "Entity";
            std::vector<EntitySnapshot> snapshots{{.uuid = uuid,
                .parent = parent,
                .name = std::move(name),
                .components = std::move(components)}};
            if(!history.execute(std::make_unique<EntityTreeCommand>(
                   registry, std::move(snapshots), true)))
                return {};
            return uuid;
        }
    }

    Comet::EntityUuid create_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::string name,
        const Comet::EntityUuid parent) {
        return create_from_components(history, registry, std::move(name),
            {{"transform", Comet::TransformComponent{}}}, parent);
    }

    Comet::EntityUuid create_mesh_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::string name,
        Comet::AssetHandle mesh, Comet::AssetHandle material,
        const Comet::Math::Vec3 position) {
        if(!mesh || !Comet::Math::is_finite(position))
            return {};
        return create_from_components(history, registry, std::move(name),
            {{"transform", Comet::TransformComponent{.translation = position}},
                {"mesh_renderer", Comet::MeshRendererComponent{mesh, material}}});
    }

    bool delete_entity(CommandHistory& history, const Comet::ComponentRegistry& registry,
        Comet::EntityUuid entity) {
        auto* scene = history.get_scene();
        if(!scene)
            return false;
        auto snapshots = capture_tree(*scene, registry, scene->find_entity(entity));
        return snapshots
               && history.execute(std::make_unique<EntityTreeCommand>(
                   registry, std::move(*snapshots), false));
    }

    Comet::EntityUuid duplicate_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity) {
        auto* scene = history.get_scene();
        if(!scene)
            return {};
        auto snapshots = capture_tree(*scene, registry, scene->find_entity(entity));
        if(!snapshots)
            return {};
        std::unordered_map<Comet::EntityUuid, Comet::EntityUuid> remap;
        std::unordered_set<Comet::EntityUuid> reserved;
        remap.reserve(snapshots->size());
        reserved.reserve(snapshots->size());
        for(const auto& snapshot : *snapshots) {
            Comet::EntityUuid uuid;
            do {
                uuid = Comet::EntityUuid::generate();
            } while(scene->find_entity(uuid) || reserved.contains(uuid));
            reserved.insert(uuid);
            remap.emplace(snapshot.uuid, uuid);
        }
        for(auto& snapshot : *snapshots) {
            snapshot.uuid = remap.at(snapshot.uuid);
            if(const auto parent = remap.find(snapshot.parent); parent != remap.end())
                snapshot.parent = parent->second;
        }
        auto& root = snapshots->front();
        if(root.name.empty())
            root.name = "Entity";
        root.name += " Copy";
        const auto uuid = root.uuid;
        if(!history.execute(std::make_unique<EntityTreeCommand>(
               registry, std::move(*snapshots), true)))
            return {};
        return uuid;
    }

    bool reparent_entity(
        CommandHistory& history, Comet::EntityUuid entity, Comet::EntityUuid parent) {
        auto* scene = history.get_scene();
        if(!scene || !scene->find_entity(entity))
            return false;
        auto previous = scene->get_parent(scene->find_entity(entity));
        const auto before = previous ? previous.get_uuid() : Comet::EntityUuid{};
        if(before == parent)
            return false;
        return history.execute(std::make_unique<ReparentCommand>(entity, before, parent));
    }

    bool can_edit_component_structure(const Comet::ComponentDescriptor& component) {
        // 编辑器实体必须保留 Transform。
        return component.id != "transform" && component.add_component_callback
               && component.remove_component_callback
               && component.capture_component_callback
               && component.restore_component_callback;
    }

    bool add_component(CommandHistory& history, const Comet::ComponentRegistry& registry,
        Comet::EntityUuid entity, std::string_view component) {
        return history.execute(std::make_unique<ComponentCommand>(
            registry, entity, std::string(component), true));
    }

    bool remove_component(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
        std::string_view component) {
        return history.execute(std::make_unique<ComponentCommand>(
            registry, entity, std::string(component), false));
    }
}
