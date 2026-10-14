#pragma once

#include "scene/command_history.h"
#include "scene/prefab.h"

#include <memory>

namespace CometEditor::SceneCommands {
    struct DuplicateOptions {
        static constexpr int MAX_COPIES = 1000;
        int copies = 1;
        Comet::Math::Vec3 world_step{};
    };
    class EntityClipboard {
    public:
        EntityClipboard();
        ~EntityClipboard();
        EntityClipboard(const EntityClipboard&) = delete;
        EntityClipboard& operator=(const EntityClipboard&) = delete;

        [[nodiscard]] bool copy(Comet::Scene& scene, const Comet::ComponentRegistry& registry,
            Comet::EntityUuid entity);
        [[nodiscard]] bool copy(Comet::Scene& scene, const Comet::ComponentRegistry& registry,
            std::span<const Comet::EntityUuid> entities);
        [[nodiscard]] Comet::EntityUuid paste(CommandHistory& history,
            const Comet::ComponentRegistry& registry, Comet::EntityUuid parent = {}) const;
        [[nodiscard]] std::vector<Comet::EntityUuid> paste_entities(CommandHistory& history,
            const Comet::ComponentRegistry& registry, Comet::EntityUuid parent = {}) const;
        [[nodiscard]] bool has_content() const noexcept;

    private:
        struct Data;
        std::unique_ptr<Data> m_data;
    };

    [[nodiscard]] Comet::EntityUuid create_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::string name = "Entity",
        Comet::EntityUuid parent = {});
    [[nodiscard]] Comet::EntityUuid create_mesh_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::string name, Comet::AssetHandle mesh,
        Comet::AssetHandle material, Comet::Math::Vec3 position);
    [[nodiscard]] Comet::EntityUuid create_model_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, const Comet::ModelData& model,
        const std::string& name, Comet::Math::Vec3 position);
    [[nodiscard]] Comet::EntityUuid create_prefab_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, const Comet::Prefab& prefab,
        Comet::Math::Vec3 position, Comet::AssetHandle source = {},
        Comet::AssetType source_type = Comet::AssetType::Prefab);
    [[nodiscard]] Comet::Result<void> update_prefab(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid root,
        const Comet::Prefab& source,
        Comet::Prefab::UpdateMode mode = Comet::Prefab::UpdateMode::KeepOverrides);
    [[nodiscard]] std::unique_ptr<CommandHistory::Command> replace_tree(Comet::Scene& scene,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid root,
        Comet::EntityTrees::Snapshot replacement);
    [[nodiscard]] bool detach_prefab(CommandHistory& history, Comet::EntityUuid root);

    [[nodiscard]] bool delete_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity);
    [[nodiscard]] bool delete_entities(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::span<const Comet::EntityUuid> entities);
    [[nodiscard]] Comet::EntityUuid duplicate_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity);
    [[nodiscard]] std::vector<Comet::EntityUuid> duplicate_entities(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::span<const Comet::EntityUuid> entities,
        DuplicateOptions options = {});
    [[nodiscard]] bool reparent_entity(
        CommandHistory& history, Comet::EntityUuid entity, Comet::EntityUuid parent = {});
    [[nodiscard]] bool can_edit_component_structure(const Comet::ComponentDescriptor& component);
    [[nodiscard]] bool add_component(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
        std::string_view component);
    [[nodiscard]] bool remove_component(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
        std::string_view component);
}
