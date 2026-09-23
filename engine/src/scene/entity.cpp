#include "scene/entity.h"

#include "scene/scene.h"
#include "diagnostics/logger.h"

namespace Comet {
    Entity::Entity(const entt::entity handle, Scene* scene) : m_handle(handle), m_scene(scene) {}

    EntityId Entity::get_id() const {
        if(!has_component<IdComponent>()) {
            return INVALID_ENTITY_ID;
        }

        return get_component<IdComponent>().id;
    }

    EntityUuid Entity::get_uuid() const {
        if(!has_component<UuidComponent>()) {
            return INVALID_ENTITY_UUID;
        }

        return get_component<UuidComponent>().uuid;
    }

    Entity::operator bool() const {
        return m_scene != nullptr && m_handle != entt::null && m_scene->m_registry.valid(m_handle);
    }

    bool Entity::try_set_transform(const TransformComponent& transform) const {
        if(!has_component<TransformComponent>() || !Math::is_finite(transform.translation)
            || !Math::is_finite(transform.rotation) || !Math::is_finite(transform.scale))
            return false;
        auto& current = m_scene->m_registry.get<TransformComponent>(m_handle);
        if(current.translation == transform.translation && current.rotation == transform.rotation
            && current.scale == transform.scale)
            return true;
        m_scene->mark_transform_dirty(m_handle);
        current = transform;
        return true;
    }

    void Entity::require_transform_write(bool accepted) {
        if(!accepted)
            LOG_FATAL(
                "Cannot write transform: invalid entity, missing component or non-finite value");
    }

    void Entity::set_transform(const TransformComponent& transform) const {
        require_transform_write(try_set_transform(transform));
    }
}
