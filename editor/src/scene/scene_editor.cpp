#include "assets/editor_assets.h"
#include "scene/scene_editor.h"

#include "scene/scene_commands.h"
#include "scene/selection.h"
#include "scene/script_component.h"
#include "diagnostics/logger.h"
#include "common/scope_exit.h"

namespace CometEditor {
    namespace {
        class PrefabSourceCommand final: public CommandHistory::Command {
        public:
            PrefabSourceCommand(EditorAssets& assets, const Comet::ComponentRegistry& registry,
                Comet::AssetHandle asset, Comet::Prefab before, Comet::Prefab after,
                std::unique_ptr<CommandHistory::Command> instance)
                : m_assets(assets), m_registry(registry), m_asset(asset),
                  m_before(std::move(before)), m_after(std::move(after)),
                  m_instance(std::move(instance)) {}
            bool undo(Comet::Scene& scene) override { return apply(scene, false); }
            bool redo(Comet::Scene& scene) override { return apply(scene, true); }

        private:
            bool apply(Comet::Scene& scene, bool forward) {
                const auto& expected = forward ? m_before : m_after;
                const auto& replacement = forward ? m_after : m_before;
                const Comet::AssetTarget target{m_asset, m_assets.database().get_revision(m_asset)};
                auto current = m_assets.load_prefab(target, m_registry);
                if(!current) {
                    LOG_ERROR("Cannot write prefab: {}", current.error());
                    return false;
                }
                auto current_text = current.value().serialize(m_registry);
                auto expected_text = expected.serialize(m_registry);
                if(!current_text || !expected_text
                    || current_text.value() != expected_text.value()) {
                    LOG_ERROR(
                        "Prefab source changed since this edit; cannot overwrite it through undo/redo");
                    return false;
                }
                const bool changed = forward ? m_instance->redo(scene) : m_instance->undo(scene);
                if(!changed)
                    return false;
                if(auto saved = m_assets.save_prefab(target, replacement, m_registry); !saved) {
                    LOG_ERROR("Cannot save prefab: {}", saved.error());
                    const bool restored =
                        forward ? m_instance->undo(scene) : m_instance->redo(scene);
                    if(!restored)
                        LOG_ERROR("Cannot restore prefab instance after source save failure");
                    return false;
                }
                return true;
            }
            EditorAssets& m_assets;
            const Comet::ComponentRegistry& m_registry;
            Comet::AssetHandle m_asset;
            Comet::Prefab m_before, m_after;
            std::unique_ptr<CommandHistory::Command> m_instance;
        };

        class ScriptAssignment final: public CommandHistory::Command {
        public:
            ScriptAssignment(std::span<const Comet::Entity> entities, Comet::AssetHandle asset) {
                for(auto entity : entities) {
                    m_entities.push_back(entity.get_uuid());
                    m_before.push_back(entity.get_component<Comet::ScriptComponent>());
                    m_after.emplace_back();
                    m_after.back().asset = asset;
                }
            }
            bool undo(Comet::Scene& scene) override { return apply(scene, m_before); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_after); }

        private:
            bool apply(
                Comet::Scene& scene, const std::vector<Comet::ScriptComponent>& values) const {
                for(auto uuid : m_entities) {
                    auto entity = scene.find_entity(uuid);
                    if(!entity || !entity.has_component<Comet::ScriptComponent>())
                        return false;
                }
                // 先复制全部配置；安装时生成新寿命，不携带运行绑定。
                auto replacements = values;
                for(std::size_t i = 0; i < m_entities.size(); ++i)
                    scene.find_entity(m_entities[i]).get_component<Comet::ScriptComponent>() =
                        std::move(replacements[i]);
                return true;
            }
            std::vector<Comet::EntityUuid> m_entities;
            std::vector<Comet::ScriptComponent> m_before, m_after;
        };
    }
    SceneEditor::SceneEditor(const EditorState& state, CommandHistory& history,
        PropertyEditTransaction& edit, const Comet::ComponentRegistry& components,
        SelectionService& selection, EditorAssets& assets)
        : m_state(state), m_history(history), m_edit(edit), m_components(components),
          m_selection(selection), m_assets(assets) {}

    void SceneEditor::bind_scene(Comet::Scene& scene, const EditorMode mode) {
        if(mode == EditorMode::Edit && m_history.get_scene() != &scene)
            m_history.bind_scene(&scene);
        m_selection.set_scene(scene);
        m_assets.track_scene(scene, m_components);
    }

    bool SceneEditor::can_edit(const Comet::Scene* scene, std::uint64_t generation) const {
        return scene && m_state.mode == EditorMode::Edit && m_history.get_scene() == scene
               && m_history.generation() == generation;
    }

    bool SceneEditor::copy_entity(
        Comet::Scene* scene, const Comet::EntityUuid entity, const std::uint64_t generation) {
        return copy_entities(scene, std::span(&entity, 1), generation);
    }

    bool SceneEditor::copy_entities(Comet::Scene* scene,
        std::span<const Comet::EntityUuid> entities, std::uint64_t generation) {
        return can_edit(scene, generation) && m_clipboard.copy(*scene, m_components, entities);
    }

    bool SceneEditor::execute(Comet::Scene* scene, const StructureRequest& request) {
        if(!can_edit(scene, request.generation) || !m_edit.commit())
            return false;
        using Type = StructureRequest::Type;
        switch(request.type) {
            case Type::DetachPrefab:
                return SceneCommands::detach_prefab(m_history, request.entity);
            case Type::ApplyPrefab:
            case Type::RevertPrefab:
            case Type::UpdatePrefab: {
                const auto root = scene->find_entity(request.entity);
                if(!root || !root.has_component<Comet::PrefabInstanceComponent>())
                    return false;
                const auto& link = root.get_component<Comet::PrefabInstanceComponent>();
                if(request.type == Type::ApplyPrefab
                    && link.source_type != Comet::AssetType::Prefab)
                    return false;
                const auto source = link.asset;
                const auto* record = m_assets.database().find(source);
                if(!record || record->type != link.source_type)
                    return false;
                auto prefab = m_assets.load_prefab(
                    {source, m_assets.database().get_revision(source)}, m_components);
                if(!prefab) {
                    LOG_ERROR("Cannot update prefab: {}", prefab.error());
                    return false;
                }
                if(request.type == Type::ApplyPrefab) {
                    auto update = prefab.value().prepare_source_update(*scene, m_components, root);
                    if(!update) {
                        LOG_ERROR("Cannot apply prefab overrides: {}", update.error());
                        return false;
                    }
                    auto instance = SceneCommands::replace_tree(
                        *scene, m_components, request.entity, std::move(update.value().instance));
                    return instance
                           && m_history.execute(std::make_unique<PrefabSourceCommand>(m_assets,
                               m_components, source, std::move(prefab).value(),
                               std::move(update.value().source), std::move(instance)));
                }
                const auto mode = request.type == Type::RevertPrefab
                                      ? Comet::Prefab::UpdateMode::RevertOverrides
                                      : Comet::Prefab::UpdateMode::KeepOverrides;
                auto result = SceneCommands::update_prefab(
                    m_history, m_components, request.entity, prefab.value(), mode);
                if(!result) {
                    LOG_ERROR("Cannot update prefab: {}", result.error());
                    return false;
                }
                return true;
            }
            case Type::Create: {
                const auto uuid =
                    SceneCommands::create_entity(m_history, m_components, "Entity", request.parent);
                if(!uuid)
                    return false;
                m_selection.select_entity(scene->find_entity(uuid).get_id());
                return true;
            }
            case Type::Duplicate: {
                const auto targets = request.entities.empty()
                                         ? std::span(&request.entity, 1)
                                         : std::span<const Comet::EntityUuid>(request.entities);
                const auto roots = SceneCommands::duplicate_entities(
                    m_history, m_components, targets, request.duplicate);
                if(roots.empty())
                    return false;
                m_selection.select_entities(roots);
                return true;
            }
            case Type::Delete: {
                const auto targets = request.entities.empty()
                                         ? std::span(&request.entity, 1)
                                         : std::span<const Comet::EntityUuid>(request.entities);
                return SceneCommands::delete_entities(m_history, m_components, targets);
            }
            case Type::Reparent:
                return SceneCommands::reparent_entity(m_history, request.entity, request.parent);
            case Type::Paste: {
                const auto roots =
                    m_clipboard.paste_entities(m_history, m_components, request.parent);
                if(roots.empty())
                    return false;
                m_selection.select_entities(roots);
                return true;
            }
        }
        return false;
    }

    bool SceneEditor::rename_entity(Comet::Scene* scene, Comet::EntityUuid uuid,
        const std::string& name, std::uint64_t generation) {
        if(!can_edit(scene, generation) || name.empty() || !scene->find_entity(uuid))
            return false;
        return m_edit.apply({uuid, "name", "name"}, name);
    }

    bool SceneEditor::undo(Comet::Scene* scene) {
        if(!can_edit(scene, m_history.generation()) || !m_edit.commit())
            return false;
        return m_history.undo();
    }

    bool SceneEditor::redo(Comet::Scene* scene) {
        if(!can_edit(scene, m_history.generation()) || !m_edit.commit())
            return false;
        return m_history.redo();
    }

    Comet::Result<void, Comet::Error> SceneEditor::assign_asset(Comet::Scene* scene,
        const PropertyEditTransaction::Target& target, const AssetInput& asset) {
        return assign_assets(scene, std::span(&target, 1), asset);
    }

    Comet::Result<void, Comet::Error> SceneEditor::assign_assets(Comet::Scene* scene,
        std::span<const PropertyEditTransaction::Target> targets, const AssetInput& asset) {
        using Result = Comet::Result<void, Comet::Error>;
        if(!scene || targets.empty() || asset.generation != m_history.generation()
            || (m_state.mode == EditorMode::Edit && m_history.get_scene() != scene))
            return Result::failure({"Asset assignment targets an inactive scene"});
        std::vector<PropertyEditTransaction::Target> changed;
        std::vector<Comet::Entity> entities;
        for(const auto& target : targets) {
            auto entity = scene->find_entity(target.entity);
            const auto* component = m_components.find_component(target.component);
            const auto* property = component ? component->find_property(target.property) : nullptr;
            if(!entity || !component || !component->has_component(entity) || !property
                || !property->editable || property->read_only
                || property->type != Comet::PropertyType::AssetHandle
                || property->asset_type != asset.type
                || target.component != targets.front().component
                || target.property != targets.front().property)
                return Result::failure({"Asset assignment property is no longer editable"});
            const auto current =
                property->copy_value(component->get_component(std::as_const(entity)));
            if(current && std::get<Comet::AssetHandle>(*current) == asset.target.handle)
                continue;
            if(std::find(changed.begin(), changed.end(), target) != changed.end())
                return Result::failure({"Duplicate asset assignment target"});
            changed.push_back(target);
            entities.push_back(entity);
        }
        if(changed.empty())
            return Result::success();
        if(!m_edit.commit())
            return Result::failure({"Cannot finish active property edit"});
        if(auto loaded =
                m_assets.load_reference(asset.target.handle, asset.type, asset.target.revision);
            !loaded)
            return loaded;
        bool applied = false;
        if(changed.front().component == "script" && changed.front().property == "asset") {
            auto command = std::make_unique<ScriptAssignment>(entities, asset.target.handle);
            if(m_state.mode == EditorMode::Play)
                applied = command->redo(*scene);
            else
                applied = m_history.execute(std::move(command));
        } else {
            const std::vector<Comet::PropertyValue> values(changed.size(), asset.target.handle);
            if(m_state.mode == EditorMode::Play)
                applied = m_edit.write_many(*scene, changed, values);
            else
                applied = m_edit.apply_many(changed, values);
        }
        if(!applied)
            return Result::failure({"Cannot commit asset references"});
        if(m_state.mode == EditorMode::Play)
            m_assets.track_scene(*scene, m_components);
        return Result::success();
    }

    Comet::Result<void, Comet::Error> SceneEditor::save_prefab(Comet::Scene* scene,
        Comet::EntityUuid root, const std::filesystem::path& path, std::uint64_t generation) {
        using Result = Comet::Result<void, Comet::Error>;
        if(!can_edit(scene, generation) || !scene->find_entity(root) || !m_edit.commit())
            return Result::failure({"Prefab save targets an inactive scene or missing entity"});
        auto prefab = Comet::Prefab::capture(*scene, m_components, scene->find_entity(root));
        if(!prefab)
            return Result::failure({prefab.error()});
        const auto destination = path.is_absolute()
                                     ? path.lexically_relative(m_assets.database().paths().assets())
                                     : path;
        const auto report = m_assets.create_prefab(destination, prefab.value(), m_components);
        if(!report.succeeded() || !report.snapshot_updated)
            return Result::failure(
                {report.issues.empty() ? "Cannot save prefab" : report.issues.front().message});
        return Result::success();
    }

    Comet::Result<void, Comet::Error> SceneEditor::place_asset(
        Comet::Scene* scene, const AssetInput& asset, Comet::Math::Vec3 position) {
        using Result = Comet::Result<void, Comet::Error>;
        if(!can_edit(scene, asset.generation)
            || (asset.type != Comet::AssetType::Mesh && asset.type != Comet::AssetType::Model
                && asset.type != Comet::AssetType::Prefab)
            || !asset.target.handle || !Comet::Math::is_finite(position))
            return Result::failure({"Asset placement targets an inactive scene or invalid asset"});
        if(!m_edit.commit())
            return Result::failure({"Cannot finish active property edit"});
        if(asset.type == Comet::AssetType::Prefab || asset.type == Comet::AssetType::Model) {
            const auto* record = m_assets.database().find(asset.target.handle);
            if(!record || record->type != asset.type)
                return Result::failure({"Asset placement source type mismatch"});
            auto prefab = m_assets.load_prefab(asset.target, m_components);
            if(!prefab)
                return Result::failure({prefab.error()});
            const auto root = SceneCommands::create_prefab_entity(
                m_history, m_components, prefab.value(), position, asset.target.handle, asset.type);
            if(!root)
                return Result::failure({"Cannot place prefab"});
            m_selection.select_entity(scene->find_entity(root).get_id());
            return Result::success();
        }
        if(auto loaded =
                m_assets.load_reference(asset.target.handle, asset.type, asset.target.revision);
            !loaded)
            return loaded;
        const auto* record = m_assets.database().find(asset.target.handle);
        const auto uuid = SceneCommands::create_mesh_entity(m_history, m_components,
            record ? record->path.stem().string() : "Default character", asset.target.handle, {},
            position);
        if(!uuid)
            return Result::failure({"Cannot create entities for dropped asset"});
        m_selection.select_entity(scene->find_entity(uuid).get_id());
        return Result::success();
    }
}
