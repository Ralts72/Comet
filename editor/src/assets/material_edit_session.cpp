#include "assets/material_edit_session.h"
#include "assets/editor_assets.h"
#include "assets/material_editing.h"
#include "render/renderer.h"

#include <utility>

namespace CometEditor {
    MaterialEditSession::MaterialEditSession(EditorAssets& assets, Comet::Renderer& renderer)
        : m_assets(assets), m_renderer(renderer) {}

    void MaterialEditSession::History::record(const MaterialEdit& edit) {
        edits.resize(cursor);
        if(edits.size() == 128)
            edits.erase(edits.begin());
        edits.push_back(edit);
        cursor = edits.size();
    }

    Comet::Result<void, Comet::Error> MaterialEditSession::process(const AssetEdit& edit) {
        if(edit.action == AssetEdit::Action::Cancel)
            return cancel();
        if(edit.action == AssetEdit::Action::Apply) {
            if(auto saved = commit(); !saved)
                return saved;
            const auto& change = std::get<MaterialEdit>(edit.value);
            if(change.before == change.after)
                return Comet::Result<void, Comet::Error>::success();
            auto& history = m_histories[edit.handle];
            if(auto applied = apply_material_edit(m_assets, m_renderer, edit); !applied)
                return applied;
            history.record(change);
            return Comet::Result<void, Comet::Error>::success();
        }
        if(auto result = preview(edit); !result)
            return result;
        if(edit.action == AssetEdit::Action::Preview)
            return Comet::Result<void, Comet::Error>::success();
        return commit();
    }

    Comet::Result<void, Comet::Error> MaterialEditSession::preview(const AssetEdit& edit) {
        using Result = Comet::Result<void, Comet::Error>;
        const auto* change = std::get_if<MaterialEdit>(&edit.value);
        if(!change)
            return Result::failure({"Expected a material edit"});
        if(m_active
            && (m_active->edit.handle != edit.handle || m_active->edit.revision != edit.revision))
            return Result::failure({"Finish the active material edit first"});
        if(!m_active && change->before == change->after)
            return Result::success();
        if(m_active && std::get<MaterialEdit>(m_active->edit.value).after == change->after)
            return Result::success();
        if(!m_active) {
            if(auto loaded =
                    m_assets.load_reference(edit.handle, Comet::AssetType::Material, edit.revision);
                !loaded)
                return loaded;
        }
        auto candidate = m_assets.prepare_material_edit(edit);
        if(!candidate)
            return Result::failure(candidate.error());
        auto original = candidate.value().expected_material();
        std::optional<Comet::MaterialRenderer::MaterialUpdate> rollback;
        if(!m_active) {
            auto prepared = m_renderer.prepare_material_update(edit.handle, original);
            if(!prepared)
                return Result::failure(prepared.error().as_error());
            rollback.emplace(std::move(prepared).value());
        }
        auto bindings =
            m_renderer.prepare_material_update(edit.handle, candidate.value().material());
        if(!bindings)
            return Result::failure(bindings.error().as_error());
        if(auto published = m_assets.preview_material_edit(candidate.value()); !published)
            return published;
        std::move(bindings).value().publish();
        if(m_active) {
            std::get<MaterialEdit>(m_active->edit.value).after = change->after;
            m_active->candidate = std::move(candidate).value();
        } else {
            m_active.emplace(ActiveEdit{
                edit, std::move(original), std::move(candidate).value(), std::move(*rollback)});
        }
        return Result::success();
    }

    Comet::Result<void, Comet::Error> MaterialEditSession::commit() {
        using Result = Comet::Result<void, Comet::Error>;
        if(!m_active)
            return Result::success();
        const auto& change = std::get<MaterialEdit>(m_active->edit.value);
        if(change.before == change.after)
            return cancel();
        auto& history = m_histories[m_active->edit.handle];
        if(auto saved = m_assets.commit_material_edit(m_active->candidate); !saved)
            return saved;
        history.record(change);
        m_active.reset();
        return Result::success();
    }

    Comet::Result<void, Comet::Error> MaterialEditSession::cancel() {
        if(!m_active)
            return Comet::Result<void, Comet::Error>::success();
        auto restored = m_assets.restore_material_preview(m_active->candidate, m_active->original);
        if(restored)
            std::move(m_active->rollback).publish();
        m_active.reset();
        return restored;
    }

    bool MaterialEditSession::can_undo(const Comet::AssetHandle handle) const {
        const auto found = m_histories.find(handle);
        return found != m_histories.end() && found->second.cursor > 0;
    }

    bool MaterialEditSession::can_redo(const Comet::AssetHandle handle) const {
        const auto found = m_histories.find(handle);
        return found != m_histories.end() && found->second.cursor < found->second.edits.size();
    }

    Comet::Result<void, Comet::Error> MaterialEditSession::undo(const Comet::AssetHandle handle) {
        return replay(handle, false);
    }

    Comet::Result<void, Comet::Error> MaterialEditSession::redo(const Comet::AssetHandle handle) {
        return replay(handle, true);
    }

    Comet::Result<void, Comet::Error> MaterialEditSession::replay(
        const Comet::AssetHandle handle, const bool forward) {
        using Result = Comet::Result<void, Comet::Error>;
        if(auto saved = commit(); !saved)
            return saved;
        if((forward && !can_redo(handle)) || (!forward && !can_undo(handle)))
            return Result::failure({"No material edit to replay"});
        auto& history = m_histories.at(handle);
        const auto& change = history.edits[forward ? history.cursor : history.cursor - 1];
        auto replayed = change;
        if(!forward)
            std::swap(replayed.before, replayed.after);
        if(auto applied = apply_material_edit(m_assets, m_renderer,
               {handle, m_assets.database().get_revision(handle), std::move(replayed)});
            !applied)
            return applied;
        if(forward)
            ++history.cursor;
        else
            --history.cursor;
        return Result::success();
    }

}
