#pragma once
#include "common/export.h"
#include "scene/entity.h"
#include <entt.hpp>
#include <string>
#include <vector>

namespace Comet {
    class COMET_API Scene {
    public:
        Scene() = default;
        ~Scene() = default;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) noexcept = delete;
        Scene& operator=(Scene&&) noexcept = delete;

        Entity create_entity(const std::string& name = "Entity");

        void destroy_entity(Entity entity);

        [[nodiscard]] Entity find_entity_by_id(EntityId id);

        void set_parent(Entity child, Entity parent);

        void clear_parent(Entity child);

        [[nodiscard]] Entity get_parent(Entity child);

        [[nodiscard]] std::vector<Entity> get_children(Entity parent);

        void update_transforms();

        [[nodiscard]] entt::registry& get_registry() { return m_registry; }
        [[nodiscard]] const entt::registry& get_registry() const { return m_registry; }

    private:
        [[nodiscard]] Entity make_entity(entt::entity handle);
        [[nodiscard]] bool would_create_cycle(entt::entity child, entt::entity parent) const;

        void remove_from_parent(entt::entity child);
        void update_transform_recursive(entt::entity entity, const Math::Mat4& parent_world);

        entt::registry m_registry;
        EntityId m_next_entity_id = 1;
    };
}
