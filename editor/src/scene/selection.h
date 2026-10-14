#pragma once

#include "asset/handle.h"
#include "scene/scene.h"

#include <algorithm>
#include <span>

namespace CometEditor {
    class SelectionService {
    public:
        explicit SelectionService(Comet::Scene& scene) : m_scene(&scene) {}

        void set_scene(Comet::Scene& scene) {
            m_scene = &scene;
            clear();
        }

        void select_entity(Comet::EntityId entity_id) {
            clear();
            if(const auto entity = m_scene->find_entity(entity_id))
                m_entities.push_back(entity.get_uuid());
        }

        void select_entities(std::span<const Comet::EntityUuid> entities) {
            clear();
            for(const auto uuid : entities)
                if(m_scene->find_entity(uuid)
                    && std::find(m_entities.begin(), m_entities.end(), uuid) == m_entities.end())
                    m_entities.push_back(uuid);
        }

        void toggle_entity(Comet::EntityId entity_id) {
            const auto entity = m_scene->find_entity(entity_id);
            if(!entity)
                return;
            prune();
            m_selected_asset = {};
            m_scene_selected = false;
            const auto found = std::find(m_entities.begin(), m_entities.end(), entity.get_uuid());
            if(found == m_entities.end())
                m_entities.push_back(entity.get_uuid());
            else
                m_entities.erase(found);
        }

        void select_asset(Comet::AssetHandle asset) {
            clear();
            m_selected_asset = asset;
        }

        void clear() {
            m_scene_selected = false;
            m_entities.clear();
            m_selected_asset = {};
        }

        // 使用持久身份跨越撤销/Prefab 重建；最后选择的存活实体是 Inspector 主目标。
        [[nodiscard]] Comet::Entity get_selected_entity() const {
            prune();
            return m_entities.empty() ? Comet::Entity{} : m_scene->find_entity(m_entities.back());
        }

        [[nodiscard]] std::vector<Comet::EntityUuid> get_selected_entities() const {
            prune();
            return m_entities;
        }

        void select_scene() {
            clear();
            m_scene_selected = true;
        }

        [[nodiscard]] Comet::Scene* get_selected_scene() const {
            return m_scene_selected ? m_scene : nullptr;
        }
        [[nodiscard]] Comet::Scene& get_scene() const { return *m_scene; }
        [[nodiscard]] Comet::EntityId get_selected_entity_id() const {
            return get_selected_entity().get_id();
        }
        [[nodiscard]] Comet::AssetHandle get_selected_asset() const { return m_selected_asset; }
        [[nodiscard]] bool is_selected(Comet::EntityId entity_id) const {
            const auto entity = m_scene->find_entity(entity_id);
            return entity
                   && std::find(m_entities.begin(), m_entities.end(), entity.get_uuid())
                          != m_entities.end();
        }
        [[nodiscard]] bool is_selected(Comet::AssetHandle asset) const {
            return asset && m_selected_asset == asset;
        }

    private:
        void prune() const {
            std::erase_if(m_entities, [this](auto uuid) { return !m_scene->find_entity(uuid); });
        }
        Comet::Scene* m_scene;
        bool m_scene_selected = false;
        mutable std::vector<Comet::EntityUuid> m_entities;
        Comet::AssetHandle m_selected_asset = {};
    };
}
