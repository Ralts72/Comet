#include "assets/asset_reference.h"
#include "asset/database.h"
#include "ui/widgets.h"
#include <imgui.h>

namespace CometEditor {
    bool edit_asset_reference(const char* label, Comet::AssetHandle& handle,
        const Comet::AssetDatabase& database, const std::optional<Comet::AssetType> type,
        const bool allow_none) {
        std::string preview = "无";
        if(handle.is_valid()) {
            const auto* record = database.find(handle);
            if(!record) {
                preview = "已丢失";
            } else if(type && record->type != *type) {
                preview = std::string("类型不匹配：") + record->path.generic_string();
            } else {
                preview = record->path.generic_string();
            }
        }

        bool changed = false;
        if(ImGui::BeginCombo(label, preview.c_str())) {
            if(allow_none && ImGui::Selectable("无###None", !handle.is_valid())
                && handle.is_valid()) {
                handle = {};
                changed = true;
            }
            bool has_candidates = false;
            for(const auto& candidate : database.get_assets()) {
                if(type && candidate.type != *type) {
                    continue;
                }
                has_candidates = true;
                const bool selected = candidate.handle == handle;
                const std::string item_label = candidate.path.generic_string() + "###"
                                               + std::to_string(candidate.handle.value());
                if(ImGui::Selectable(item_label.c_str(), selected) && !selected) {
                    handle = candidate.handle;
                    changed = true;
                }
                if(selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            if(!has_candidates) {
                ImGui::TextDisabled("%s", "没有匹配的资产");
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    std::optional<AssetDragPayload> accept_asset_drop(
        const Comet::AssetDatabase& database, Comet::AssetType type, std::uint64_t generation) {
        if(!ImGui::BeginDragDropTarget())
            return std::nullopt;
        std::optional<AssetDragPayload> result;
        if(const auto payload = read_asset_drag_payload(ImGui::GetDragDropPayload())) {
            const auto* record = database.find(payload->target.handle);
            if(payload->type == type && record && record->type == type
                && database.is_current(payload->target) && payload->generation == generation
                && ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE))
                result = payload;
        }
        ImGui::EndDragDropTarget();
        return result;
    }

    std::optional<AssetDragPayload> read_asset_drag_payload(const ImGuiPayload* payload) {
        return Ui::read_drag_payload<AssetDragPayload>(payload);
    }
}
