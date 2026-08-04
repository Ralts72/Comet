#include "scene/entity.h"

#include "scene/scene.h"

namespace Comet {
    Entity::Entity(const entt::entity handle, Scene* scene)
        : m_handle(handle), m_scene(scene) {}

    EntityId Entity::get_id() const {
        if(!has_component<IdComponent>()) {
            return INVALID_ENTITY_ID;
        }

        return get_component<IdComponent>().id;
    }

    Entity::operator bool() const {
        return m_scene != nullptr
               && m_handle != entt::null
               && m_scene->m_registry.valid(m_handle);
    }
}
