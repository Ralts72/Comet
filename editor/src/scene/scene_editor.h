#pragma once

#include "editor_state.h"
#include "scene/command_history.h"
#include "scene/scene_commands.h"
#include "asset/metadata.h"
#include "common/error.h"

#include <string>

namespace CometEditor {
    class EditorAssets;
    class SelectionService;

    // 编辑命令的执行者；面板只采集请求，不决定场景身份和修改规则。
    class SceneEditor {
    public:
        struct StructureRequest {
            enum class Type {
                Create,
                Delete,
                Reparent,
                Duplicate,
                Paste,
                UpdatePrefab,
                RevertPrefab,
                ApplyPrefab,
                DetachPrefab
            };
            Type type;
            Comet::EntityUuid entity;
            Comet::EntityUuid parent;
            std::uint64_t generation;
            std::vector<Comet::EntityUuid> entities;
            SceneCommands::DuplicateOptions duplicate;
        };
        struct AssetInput {
            Comet::AssetTarget target;
            std::uint64_t generation;
            Comet::AssetType type;
        };

        SceneEditor(const EditorState& state, CommandHistory& history,
            PropertyEditTransaction& edit, const Comet::ComponentRegistry& components,
            SelectionService& selection, EditorAssets& assets);

        // 场景已安装且旧交互已结束；Play 保留 Edit 历史，恢复同一 Edit 场景不重置历史。
        void bind_scene(Comet::Scene& scene, EditorMode mode);
        [[nodiscard]] bool can_edit(const Comet::Scene* scene, std::uint64_t generation) const;
        [[nodiscard]] const SceneCommands::EntityClipboard& clipboard() const noexcept {
            return m_clipboard;
        }
        [[nodiscard]] bool copy_entity(
            Comet::Scene* scene, Comet::EntityUuid entity, std::uint64_t generation);
        [[nodiscard]] bool copy_entities(Comet::Scene* scene,
            std::span<const Comet::EntityUuid> entities, std::uint64_t generation);
        [[nodiscard]] bool execute(Comet::Scene* scene, const StructureRequest& request);
        [[nodiscard]] bool rename_entity(Comet::Scene* scene, Comet::EntityUuid entity,
            const std::string& name, std::uint64_t generation);
        [[nodiscard]] bool undo(Comet::Scene* scene);
        [[nodiscard]] bool redo(Comet::Scene* scene);
        [[nodiscard]] Comet::Result<void, Comet::Error> assign_asset(Comet::Scene* scene,
            const PropertyEditTransaction::Target& target, const AssetInput& asset);
        [[nodiscard]] Comet::Result<void, Comet::Error> assign_assets(Comet::Scene* scene,
            std::span<const PropertyEditTransaction::Target> targets, const AssetInput& asset);
        [[nodiscard]] Comet::Result<void, Comet::Error> save_prefab(Comet::Scene* scene,
            Comet::EntityUuid root, const std::filesystem::path& path, std::uint64_t generation);
        [[nodiscard]] Comet::Result<void, Comet::Error> place_asset(
            Comet::Scene* scene, const AssetInput& asset, Comet::Math::Vec3 position);

    private:
        const EditorState& m_state;
        CommandHistory& m_history;
        PropertyEditTransaction& m_edit;
        const Comet::ComponentRegistry& m_components;
        SelectionService& m_selection;
        EditorAssets& m_assets;
        SceneCommands::EntityClipboard m_clipboard;
    };
}
