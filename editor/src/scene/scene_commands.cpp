#include "scene/scene_commands.h"
#include "scene/entity_tree.h"
#include "common/scope_exit.h"

#include <unordered_set>

namespace CometEditor::SceneCommands {
    namespace {
        using Comet::EntityTrees::capture;
        using Comet::EntityTrees::EntitySnapshot;
        using Comet::EntityTrees::restore;

        class EntityTreeCommand final: public CommandHistory::Command {
        public:
            EntityTreeCommand(const Comet::ComponentRegistry& registry,
                std::vector<EntitySnapshot> snapshots, bool creating)
                : m_registry(registry), m_snapshots(std::move(snapshots)), m_creating(creating) {}

            bool undo(Comet::Scene& scene) override { return apply(scene, !m_creating); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_creating); }

        private:
            bool apply(Comet::Scene& scene, bool present) {
                if(present)
                    return restore(scene, m_registry, m_snapshots);
                const auto roots = Comet::EntityTrees::roots(m_snapshots);
                auto current = capture(scene, m_registry, roots);
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
                for(const auto uuid : roots)
                    scene.destroy_entity(scene.find_entity(uuid));
                m_snapshots = std::move(*current);
                return true;
            }

            const Comet::ComponentRegistry& m_registry;
            std::vector<EntitySnapshot> m_snapshots;
            bool m_creating;
        };

        Comet::EntityUuid insert_tree(CommandHistory& history,
            const Comet::ComponentRegistry& registry, Comet::Scene& candidate, Comet::Entity root) {
            auto snapshots = capture(candidate, registry, root);
            if(!snapshots)
                return {};
            const auto uuid = snapshots->front().uuid;
            if(!history.execute(
                   std::make_unique<EntityTreeCommand>(registry, std::move(*snapshots), true)))
                return {};
            return uuid;
        }

        class ReplaceTreeCommand final: public CommandHistory::Command {
        public:
            ReplaceTreeCommand(const Comet::ComponentRegistry& registry,
                Comet::EntityTrees::Snapshot before, Comet::EntityTrees::Snapshot after)
                : m_registry(registry), m_before(std::move(before)), m_after(std::move(after)) {}
            bool undo(Comet::Scene& scene) override { return apply(scene, m_after, m_before); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_before, m_after); }

        private:
            bool apply(Comet::Scene& scene, const Comet::EntityTrees::Snapshot& expected,
                const Comet::EntityTrees::Snapshot& replacement) const {
                auto root = scene.find_entity(expected.front().uuid);
                auto current = capture(scene, m_registry, root);
                if(!current || current->size() != expected.size())
                    return false;
                std::unordered_set<Comet::EntityUuid> members;
                for(const auto& entity : *current)
                    members.insert(entity.uuid);
                for(const auto& entity : expected)
                    if(!members.contains(entity.uuid))
                        return false;
                if(replacement.front().parent && !scene.find_entity(replacement.front().parent))
                    return false;
                for(const auto& entity : replacement)
                    if(scene.find_entity(entity.uuid) && !members.contains(entity.uuid))
                        return false;
                Comet::Scene candidate;
                auto detached = replacement;
                detached.front().parent = {};
                if(!restore(candidate, m_registry, detached))
                    return false;
                scene.destroy_entity(root);
                if(restore(scene, m_registry, replacement))
                    return true;
                static_cast<void>(restore(scene, m_registry, *current));
                return false;
            }
            const Comet::ComponentRegistry& m_registry;
            Comet::EntityTrees::Snapshot m_before, m_after;
        };

        class DetachPrefabCommand final: public CommandHistory::Command {
        public:
            explicit DetachPrefabCommand(Comet::Entity root)
                : m_root(root.get_uuid()),
                  m_link(root.get_component<Comet::PrefabInstanceComponent>()) {}
            bool undo(Comet::Scene& scene) override {
                auto root = scene.find_entity(m_root);
                if(!root || root.has_component<Comet::PrefabInstanceComponent>())
                    return false;
                root.add_component<Comet::PrefabInstanceComponent>(m_link);
                return true;
            }
            bool redo(Comet::Scene& scene) override {
                auto root = scene.find_entity(m_root);
                if(!root || !root.has_component<Comet::PrefabInstanceComponent>())
                    return false;
                root.remove_component<Comet::PrefabInstanceComponent>();
                return true;
            }

        private:
            Comet::EntityUuid m_root;
            Comet::PrefabInstanceComponent m_link;
        };

        class ReparentCommand final: public CommandHistory::Command {
        public:
            ReparentCommand(
                Comet::EntityUuid entity, Comet::EntityUuid before, Comet::EntityUuid after)
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
            ComponentCommand(const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
                std::string component, bool adding)
                : m_registry(registry), m_entity(entity), m_component(std::move(component)),
                  m_adding(adding) {}

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
                    Comet::ScopeExit rollback(
                        [&] { static_cast<void>(component->remove_component(entity)); });
                    m_snapshot = component->capture_component(entity);
                    if(!m_snapshot.has_value()) {
                        return false;
                    }
                    rollback.release();
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
            if(!history.execute(
                   std::make_unique<EntityTreeCommand>(registry, std::move(snapshots), true)))
                return {};
            return uuid;
        }

        std::vector<Comet::EntityUuid> create_copy(CommandHistory& history,
            const Comet::ComponentRegistry& registry, const std::vector<EntitySnapshot>& source,
            std::optional<Comet::EntityUuid> parent, DuplicateOptions options = {}) {
            auto* scene = history.get_scene();
            if(!scene || source.empty() || options.copies < 1
                || options.copies > DuplicateOptions::MAX_COPIES
                || !Comet::Math::is_finite(options.world_step))
                return {};
            Comet::Scene candidate;
            std::vector<Comet::EntityUuid> roots;
            std::unordered_map<Comet::EntityUuid, Comet::EntityUuid> parents;
            for(int index = 1; index <= options.copies; ++index) {
                auto snapshots = source;
                auto remap = Comet::EntityTrees::make_instance_ids(snapshots, *scene, parent);
                if(!remap)
                    return {};
                const auto group_roots = Comet::EntityTrees::roots(snapshots);
                const std::unordered_set<Comet::EntityUuid> root_ids(
                    group_roots.begin(), group_roots.end());
                for(auto& snapshot : snapshots) {
                    if(!root_ids.contains(snapshot.uuid))
                        continue;
                    if(snapshot.name.empty())
                        snapshot.name = "Entity";
                    snapshot.name += "_Copy";
                    if(options.world_step != Comet::Math::Vec3{}) {
                        auto offset = options.world_step * static_cast<float>(index);
                        if(snapshot.parent) {
                            const auto parent_entity = scene->find_entity(snapshot.parent);
                            if(!parent_entity)
                                return {};
                            offset = Comet::Math::Vec3(
                                Comet::Math::inverse(scene->get_world_matrix(parent_entity))
                                * Comet::Math::Vec4(offset, 0));
                        }
                        const auto component =
                            std::find_if(snapshot.components.begin(), snapshot.components.end(),
                                [](const auto& value) { return value.id == "transform"; });
                        if(component == snapshot.components.end())
                            return {};
                        auto* transform =
                            std::any_cast<Comet::TransformComponent>(&component->value);
                        if(!transform || !Comet::Math::is_finite(offset + transform->translation))
                            return {};
                        transform->translation += offset;
                    }
                    parents.emplace(snapshot.uuid, snapshot.parent);
                    snapshot.parent = {};
                }
                // 各份先独立重映射引用，再一起写入历史；组内引用不会串到另一份。
                if(!restore(candidate, registry, snapshots, *remap))
                    return {};
                roots.insert(roots.end(), group_roots.begin(), group_roots.end());
            }
            auto snapshots = capture(candidate, registry, roots);
            if(!snapshots)
                return {};
            for(auto& snapshot : *snapshots)
                if(const auto found = parents.find(snapshot.uuid); found != parents.end())
                    snapshot.parent = found->second;
            if(!history.execute(
                   std::make_unique<EntityTreeCommand>(registry, std::move(*snapshots), true)))
                return {};
            return roots;
        }
    }

    struct EntityClipboard::Data {
        std::vector<EntitySnapshot> snapshots;
    };

    EntityClipboard::EntityClipboard() = default;
    EntityClipboard::~EntityClipboard() = default;

    bool EntityClipboard::copy(Comet::Scene& scene, const Comet::ComponentRegistry& registry,
        const Comet::EntityUuid entity) {
        return copy(scene, registry, std::span(&entity, 1));
    }

    bool EntityClipboard::copy(Comet::Scene& scene, const Comet::ComponentRegistry& registry,
        std::span<const Comet::EntityUuid> entities) {
        auto snapshots = capture(scene, registry, entities);
        if(!snapshots)
            return false;
        m_data = std::make_unique<Data>(Data{std::move(*snapshots)});
        return true;
    }

    Comet::EntityUuid EntityClipboard::paste(CommandHistory& history,
        const Comet::ComponentRegistry& registry, const Comet::EntityUuid parent) const {
        const auto roots = paste_entities(history, registry, parent);
        return roots.empty() ? Comet::EntityUuid{} : roots.front();
    }

    std::vector<Comet::EntityUuid> EntityClipboard::paste_entities(CommandHistory& history,
        const Comet::ComponentRegistry& registry, const Comet::EntityUuid parent) const {
        return m_data ? create_copy(history, registry, m_data->snapshots, parent)
                      : std::vector<Comet::EntityUuid>{};
    }

    bool EntityClipboard::has_content() const noexcept {
        return m_data != nullptr;
    }

    Comet::EntityUuid create_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::string name,
        const Comet::EntityUuid parent) {
        return create_from_components(history, registry, std::move(name),
            {{"transform", Comet::TransformComponent{}}}, parent);
    }

    Comet::EntityUuid create_mesh_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::string name, Comet::AssetHandle mesh,
        Comet::AssetHandle material, const Comet::Math::Vec3 position) {
        if(!mesh || !Comet::Math::is_finite(position))
            return {};
        return create_from_components(history, registry, std::move(name),
            {{"transform", Comet::TransformComponent{.translation = position}},
                {"mesh_renderer", Comet::MeshRendererComponent{mesh, material}}});
    }

    Comet::EntityUuid create_model_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, const Comet::ModelData& model,
        const std::string& name, Comet::Math::Vec3 position) {
        if(!history.get_scene())
            return {};
        Comet::Scene candidate;
        const auto created = candidate.instantiate_model(model, name, position);
        if(!created)
            return {};
        return insert_tree(history, registry, candidate, created.value());
    }

    Comet::EntityUuid create_prefab_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, const Comet::Prefab& prefab,
        Comet::Math::Vec3 position, Comet::AssetHandle source, Comet::AssetType source_type) {
        if(!history.get_scene())
            return {};
        Comet::Scene candidate;
        auto created = prefab.instantiate(candidate, registry, position, {}, source, source_type);
        if(!created)
            return {};
        return insert_tree(history, registry, candidate, created.value());
    }

    std::unique_ptr<CommandHistory::Command> replace_tree(Comet::Scene& scene,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid root,
        Comet::EntityTrees::Snapshot replacement) {
        if(replacement.empty() || replacement.front().uuid != root)
            return {};
        auto before = capture(scene, registry, scene.find_entity(root));
        if(!before)
            return {};
        return std::make_unique<ReplaceTreeCommand>(
            registry, std::move(*before), std::move(replacement));
    }

    Comet::Result<void> update_prefab(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid root,
        const Comet::Prefab& source, Comet::Prefab::UpdateMode mode) {
        auto* scene = history.get_scene();
        if(!scene)
            return Comet::Result<void>::failure("No active scene");
        auto after = source.update_candidate(*scene, registry, scene->find_entity(root), mode);
        if(!after)
            return Comet::Result<void>::failure(after.error());
        auto command = replace_tree(*scene, registry, root, std::move(after).value());
        if(!command || !history.execute(std::move(command)))
            return Comet::Result<void>::failure("Cannot commit prefab update");
        return Comet::Result<void>::success();
    }

    bool detach_prefab(CommandHistory& history, Comet::EntityUuid root) {
        auto* scene = history.get_scene();
        const auto entity = scene ? scene->find_entity(root) : Comet::Entity{};
        return entity && entity.has_component<Comet::PrefabInstanceComponent>()
               && history.execute(std::make_unique<DetachPrefabCommand>(entity));
    }

    bool delete_entity(CommandHistory& history, const Comet::ComponentRegistry& registry,
        Comet::EntityUuid entity) {
        return delete_entities(history, registry, std::span(&entity, 1));
    }

    bool delete_entities(CommandHistory& history, const Comet::ComponentRegistry& registry,
        std::span<const Comet::EntityUuid> entities) {
        auto* scene = history.get_scene();
        if(!scene)
            return false;
        auto snapshots = capture(*scene, registry, entities);
        return snapshots
               && history.execute(
                   std::make_unique<EntityTreeCommand>(registry, std::move(*snapshots), false));
    }

    Comet::EntityUuid duplicate_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity) {
        const auto roots = duplicate_entities(history, registry, std::span(&entity, 1));
        return roots.empty() ? Comet::EntityUuid{} : roots.front();
    }

    std::vector<Comet::EntityUuid> duplicate_entities(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::span<const Comet::EntityUuid> entities,
        DuplicateOptions options) {
        auto* scene = history.get_scene();
        if(!scene)
            return {};
        auto snapshots = capture(*scene, registry, entities);
        if(!snapshots)
            return {};
        return create_copy(history, registry, *snapshots, std::nullopt, options);
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
               && component.remove_component_callback && component.capture_component_callback
               && component.restore_component_callback;
    }

    bool add_component(CommandHistory& history, const Comet::ComponentRegistry& registry,
        Comet::EntityUuid entity, std::string_view component) {
        return history.execute(
            std::make_unique<ComponentCommand>(registry, entity, std::string(component), true));
    }

    bool remove_component(CommandHistory& history, const Comet::ComponentRegistry& registry,
        Comet::EntityUuid entity, std::string_view component) {
        return history.execute(
            std::make_unique<ComponentCommand>(registry, entity, std::string(component), false));
    }
}
