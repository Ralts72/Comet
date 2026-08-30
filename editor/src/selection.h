#pragma once

#include "asset/handle.h"
#include "scene/scene.h"

namespace CometEditor {
    class SelectionService {
    public:
        explicit SelectionService(Comet::Scene& scene) : m_scene(&scene) {}

        void set_scene(Comet::Scene& scene) {
            m_scene = &scene;
            clear();
        }

        void select_entity(const Comet::EntityId entity_id) {
            if(!m_scene->find_entity(entity_id)) {
                clear();
                return;
            }

            m_selected_entity_id = entity_id;
            m_selected_asset = Comet::INVALID_ASSET_HANDLE;
        }

        void select_asset(const Comet::AssetHandle asset) {
            if(!asset) {
                clear();
                return;
            }

            m_selected_entity_id = Comet::INVALID_ENTITY_ID;
            m_selected_asset = asset;
        }

        void clear() {
            m_selected_entity_id = Comet::INVALID_ENTITY_ID;
            m_selected_asset = Comet::INVALID_ASSET_HANDLE;
        }

        [[nodiscard]] Comet::Entity get_selected_entity() {
            if(m_selected_entity_id == Comet::INVALID_ENTITY_ID) {
                return {};
            }

            Comet::Entity entity = m_scene->find_entity(m_selected_entity_id);
            if(!entity) {
                m_selected_entity_id = Comet::INVALID_ENTITY_ID;
            }
            return entity;
        }

        [[nodiscard]] Comet::EntityId get_selected_entity_id() const {
            return m_selected_entity_id;
        }

        [[nodiscard]] Comet::AssetHandle get_selected_asset() const {
            return m_selected_asset;
        }

        [[nodiscard]] bool is_selected(const Comet::EntityId entity_id) const {
            return entity_id != Comet::INVALID_ENTITY_ID
                && m_selected_entity_id == entity_id;
        }

        [[nodiscard]] bool is_selected(
            const Comet::AssetHandle asset) const {
            return asset && m_selected_asset == asset;
        }

    private:
        Comet::Scene* m_scene;
        Comet::EntityId m_selected_entity_id = Comet::INVALID_ENTITY_ID;
        Comet::AssetHandle m_selected_asset = Comet::INVALID_ASSET_HANDLE;
    };
}
