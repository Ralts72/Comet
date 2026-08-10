#pragma once

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
            m_selected_entity_id = m_scene->find_entity(entity_id)
                ? entity_id
                : Comet::INVALID_ENTITY_ID;
        }

        void clear() {
            m_selected_entity_id = Comet::INVALID_ENTITY_ID;
        }

        [[nodiscard]] Comet::Entity get_selected_entity() {
            Comet::Entity entity = m_scene->find_entity(m_selected_entity_id);
            if(!entity) {
                clear();
            }
            return entity;
        }

        [[nodiscard]] Comet::EntityId get_selected_entity_id() const {
            return m_selected_entity_id;
        }

        [[nodiscard]] bool is_selected(const Comet::EntityId entity_id) const {
            return entity_id != Comet::INVALID_ENTITY_ID
                && m_selected_entity_id == entity_id;
        }

    private:
        Comet::Scene* m_scene;
        Comet::EntityId m_selected_entity_id = Comet::INVALID_ENTITY_ID;
    };
}
