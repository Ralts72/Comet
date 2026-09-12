#include "assets/asset_reference.h"
#include "asset/database.h"
#include <imgui.h>
#include <cstring>
#include <type_traits>

namespace CometEditor {
    bool edit_asset_reference(const char* label, Comet::AssetHandle& handle,
        const Comet::AssetDatabase& database, const std::optional<Comet::AssetType> type,
        const bool allow_none) {
        std::string preview = "None";
        if(handle.is_valid()) {
            const auto* record = database.find(handle);
            if(!record) {
                preview = "Missing";
            } else if(type && record->type != *type) {
                preview = "Invalid type: " + record->path.generic_string();
            } else {
                preview = record->path.generic_string();
            }
        }

        bool changed = false;
        if(ImGui::BeginCombo(label, preview.c_str())) {
            if(allow_none && ImGui::Selectable("None", !handle.is_valid())
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
                ImGui::TextDisabled("No matching assets");
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    std::optional<AssetDragPayload> read_asset_drag_payload(const ImGuiPayload* payload) {
        if(!payload || !payload->IsDataType(AssetDragPayload::TYPE) || !payload->Data
            || payload->DataSize != sizeof(AssetDragPayload))
            return std::nullopt;
        static_assert(std::is_trivially_copyable_v<AssetDragPayload>);
        AssetDragPayload result;
        std::memcpy(&result, payload->Data, sizeof(result));
        return result;
    }
}
