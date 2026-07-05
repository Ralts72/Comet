#include "scene/scene.h"

namespace Comet {
    Entity Scene::create_entity(const std::string& name) {
        const entt::entity handle = m_registry.create();
        Entity entity(handle, this);

        entity.add_component<IdComponent>(m_next_entity_id++);
        entity.add_component<NameComponent>(name.empty() ? "Entity" : name);
        entity.add_component<TransformComponent>();

        return entity;
    }

    void Scene::destroy_entity(const Entity entity) {
        if(!is_valid(entity)) {
            return;
        }

        m_registry.destroy(entity.m_handle);
    }

    Entity Scene::find_entity(const EntityId id) {
        if(id == INVALID_ENTITY_ID) {
            return {};
        }

        const auto view = m_registry.view<IdComponent>();
        for(const entt::entity handle: view) {
            if(view.get<IdComponent>(handle).id == id) {
                return Entity(handle, this);
            }
        }

        return {};
    }

    std::vector<Entity> Scene::get_entities() {
        std::vector<Entity> entities;
        entities.reserve(entity_count());

        const auto view = m_registry.view<IdComponent>();
        for(const entt::entity handle: view) {
            entities.push_back(Entity(handle, this));
        }

        return entities;
    }

    bool Scene::is_valid(const Entity entity) const {
        return entity.m_scene == this
            && entity.m_handle != entt::null
            && m_registry.valid(entity.m_handle);
    }

    std::size_t Scene::entity_count() const {
        return m_registry.view<IdComponent>().size();
    }
}
