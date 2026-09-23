#include "scene/scene_editor.h"

#include "scene/scene_commands.h"
#include "scene/selection.h"
#include "assets/editor_assets.h"
#include "scene/script_component.h"

namespace CometEditor {
    namespace {
        class ScriptAssignment final: public CommandHistory::Command {
        public:
            ScriptAssignment(Comet::Entity entity, Comet::AssetHandle asset)
                : m_entity(entity.get_uuid()),
                  m_before(entity.get_component<Comet::ScriptComponent>()) {
                m_after.asset = asset;
            }
            bool undo(Comet::Scene& scene) override { return apply(scene, m_before); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_after); }

        private:
            bool apply(Comet::Scene& scene, const Comet::ScriptComponent& value) const {
                auto entity = scene.find_entity(m_entity);
                if(!entity || !entity.has_component<Comet::ScriptComponent>())
                    return false;
                // 先复制配置再安装；恢复时生成新寿命，不携带运行绑定。
                auto replacement = value;
                entity.get_component<Comet::ScriptComponent>() = std::move(replacement);
                return true;
            }
            Comet::EntityUuid m_entity;
            Comet::ScriptComponent m_before;
            Comet::ScriptComponent m_after;
        };
    }
    SceneEditor::SceneEditor(const EditorState& state, CommandHistory& history,
        PropertyEditTransaction& edit, const Comet::ComponentRegistry& components,
        SelectionService& selection, EditorAssets& assets)
        : m_state(state), m_history(history), m_edit(edit), m_components(components),
          m_selection(selection), m_assets(assets) {}

    bool SceneEditor::can_edit(const Comet::Scene* scene, std::uint64_t generation) const {
        return scene && m_state.mode == EditorMode::Edit && m_history.get_scene() == scene
               && m_history.generation() == generation;
    }

    bool SceneEditor::execute(Comet::Scene* scene, const StructureRequest& request) {
        if(!can_edit(scene, request.generation) || !m_edit.commit())
            return false;
        using Type = StructureRequest::Type;
        switch(request.type) {
            case Type::Create:
            case Type::Duplicate: {
                Comet::EntityUuid uuid;
                if(request.type == Type::Create)
                    uuid = SceneCommands::create_entity(
                        m_history, m_components, "Entity", request.parent);
                else
                    uuid = SceneCommands::duplicate_entity(m_history, m_components, request.entity);
                if(!uuid)
                    return false;
                m_selection.select_entity(scene->find_entity(uuid).get_id());
                return true;
            }
            case Type::Delete:
                if(!SceneCommands::delete_entity(m_history, m_components, request.entity))
                    return false;
                m_selection.clear();
                return true;
            case Type::Reparent:
                return SceneCommands::reparent_entity(m_history, request.entity, request.parent);
        }
        return false;
    }

    bool SceneEditor::undo(Comet::Scene* scene) {
        return can_edit(scene, m_history.generation()) && m_edit.commit() && m_history.undo();
    }

    bool SceneEditor::redo(Comet::Scene* scene) {
        return can_edit(scene, m_history.generation()) && m_edit.commit() && m_history.redo();
    }

    Comet::Result<void, Comet::Error> SceneEditor::assign_asset(Comet::Scene* scene,
        const PropertyEditTransaction::Target& target, const AssetInput& asset) {
        using Result = Comet::Result<void, Comet::Error>;
        if(!scene || asset.generation != m_history.generation()
            || (m_state.mode == EditorMode::Edit && m_history.get_scene() != scene))
            return Result::failure({"Asset assignment targets an inactive scene"});
        auto entity = scene->find_entity(target.entity);
        const auto* component = m_components.find_component(target.component);
        const Comet::PropertyDescriptor* property = nullptr;
        if(component)
            property = component->find_property(target.property);
        if(!entity || !component || !component->has_component(entity) || !property
            || !property->editable || property->read_only
            || property->type != Comet::PropertyType::AssetHandle
            || property->asset_type != asset.type)
            return Result::failure({"Asset assignment property is no longer editable"});
        const auto current = property->copy_value(component->get_component(std::as_const(entity)));
        if(current && std::get<Comet::AssetHandle>(*current) == asset.handle)
            return Result::success();
        if(!m_edit.commit())
            return Result::failure({"Cannot finish active property edit"});
        if(auto loaded = m_assets.load_reference(asset.handle, asset.type, asset.revision); !loaded)
            return loaded;
        if(target.component == "script" && target.property == "asset") {
            auto command = std::make_unique<ScriptAssignment>(entity, asset.handle);
            if(m_state.mode == EditorMode::Play) {
                if(!command->redo(*scene))
                    return Result::failure({"Cannot update runtime script binding"});
                m_assets.track_scene(*scene, m_components);
            } else if(!m_history.execute(std::move(command)))
                return Result::failure({"Cannot commit script binding"});
            return Result::success();
        }
        if(m_state.mode == EditorMode::Play) {
            if(!component->assign_property(entity, property->id, asset.handle))
                return Result::failure({"Cannot update runtime asset reference"});
            m_assets.track_scene(*scene, m_components);
        } else if(!m_edit.apply(target, asset.handle))
            return Result::failure({"Cannot commit asset reference"});
        return Result::success();
    }

    Comet::Result<void, Comet::Error> SceneEditor::create_mesh(
        Comet::Scene* scene, const AssetInput& asset, Comet::Math::Vec3 position) {
        using Result = Comet::Result<void, Comet::Error>;
        if(!can_edit(scene, asset.generation) || asset.type != Comet::AssetType::Mesh
            || !asset.handle)
            return Result::failure({"Mesh insertion targets an inactive scene or invalid asset"});
        if(!m_edit.commit())
            return Result::failure({"Cannot finish active property edit"});
        if(auto loaded = m_assets.load_reference(asset.handle, asset.type, asset.revision); !loaded)
            return loaded;
        const auto* record = m_assets.database().find(asset.handle);
        const auto uuid = SceneCommands::create_mesh_entity(
            m_history, m_components, record->path.stem().string(), asset.handle, {}, position);
        if(!uuid)
            return Result::failure({"Cannot create entity for dropped mesh"});
        m_selection.select_entity(scene->find_entity(uuid).get_id());
        return Result::success();
    }
}
