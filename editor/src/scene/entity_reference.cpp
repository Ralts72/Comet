#include "scene/entity_reference.h"
#include "scene/scene.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace CometEditor {
    namespace {
        std::string entity_path(Comet::Scene& scene, Comet::Entity entity) {
            std::string path;
            while(entity) {
                auto name = entity.get_component<Comet::NameComponent>().name;
                if(name.empty())
                    name = "未命名实体";
                if(!path.empty())
                    name += " / " + path;
                path = std::move(name);
                entity = scene.get_parent(entity);
            }
            return path;
        }
    }

    std::optional<Comet::EntityUuid> accept_entity_drop(
        Comet::Scene& scene, const std::uint64_t generation) {
        if(!ImGui::BeginDragDropTarget())
            return std::nullopt;
        std::optional<Comet::EntityUuid> result;
        const auto* payload = ImGui::GetDragDropPayload();
        if(payload && payload->IsDataType(EntityDragPayload::TYPE) && payload->Data
            && payload->DataSize == sizeof(EntityDragPayload)) {
            static_assert(std::is_trivially_copyable_v<EntityDragPayload>);
            EntityDragPayload source;
            std::memcpy(&source, payload->Data, sizeof(source));
            if(source.generation == generation && scene.find_entity(source.entity)
                && ImGui::AcceptDragDropPayload(EntityDragPayload::TYPE))
                result = source.entity;
        }
        ImGui::EndDragDropTarget();
        return result;
    }

    bool edit_entity_reference(const char* label, Comet::EntityUuid& reference, Comet::Scene& scene,
        const std::optional<std::uint64_t> drop_generation) {
        std::string preview = "无";
        if(reference) {
            const auto entity = scene.find_entity(reference);
            preview = "已丢失";
            if(entity)
                preview = entity_path(scene, entity);
        }
        bool changed = false;
        if(ImGui::BeginCombo(label, preview.c_str())) {
            if(ImGui::Selectable("无###None", !reference) && reference) {
                reference = {};
                changed = true;
            }
            std::vector<std::pair<std::string, Comet::EntityUuid>> candidates;
            for(const auto entity : scene.get_entities())
                candidates.emplace_back(entity_path(scene, entity), entity.get_uuid());
            std::ranges::sort(candidates);
            for(const auto& [path, uuid] : candidates) {
                const bool selected = uuid == reference;
                const std::string item = path + "###" + uuid.to_string();
                if(ImGui::Selectable(item.c_str(), selected) && !selected) {
                    reference = uuid;
                    changed = true;
                }
                if(selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if(drop_generation) {
            if(const auto dropped = accept_entity_drop(scene, *drop_generation);
                dropped && *dropped != reference) {
                reference = *dropped;
                changed = true;
            }
        }
        return changed;
    }
}
