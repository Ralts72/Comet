#include "scene/scene_editor.h"

#include "scene/scene_commands.h"
#include "scene/selection.h"
#include "assets/editor_assets.h"

namespace CometEditor {
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
        if(m_state.mode == EditorMode::Play) {
            if(!property->assign_value(component->get_component(entity), asset.handle))
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
