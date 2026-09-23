#pragma once
#include <cstddef>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/export.h"
#include "scene/entity.h"
#include "scene/scene_settings.h"
#include <entt.hpp>

namespace Comet {
    class SceneSerializer;

    class COMET_API Scene {
    public:
        Scene() = default;

        ~Scene() = default;

        Scene(const Scene&) = delete;

        Scene& operator=(const Scene&) = delete;

        Scene(Scene&&) noexcept = delete;

        Scene& operator=(Scene&&) noexcept = delete;

        Entity create_entity(const std::string& name = "Entity");

        [[nodiscard]] Entity create_entity_with_uuid(
            EntityUuid uuid, const std::string& name = "Entity");

        void destroy_entity(Entity entity);

        [[nodiscard]] bool set_parent(Entity child, Entity parent);

        [[nodiscard]] bool clear_parent(Entity child);

        [[nodiscard]] Entity get_parent(Entity entity);

        [[nodiscard]] std::vector<Entity> get_children(Entity entity);

        [[nodiscard]] std::vector<Entity> get_root_entities();

        // 同步脏节点；之后可直接读取只读 WorldTransformComponent，返回重算节点数。
        std::size_t update_world_transforms();

        // 即时查询：仅同步该实体的脏祖先链，不刷新无关分支。
        [[nodiscard]] const Math::Mat4& get_world_matrix(Entity entity);

        [[nodiscard]] Entity find_entity(EntityId id);

        [[nodiscard]] Entity find_entity(EntityUuid uuid);

        [[nodiscard]] std::vector<Entity> get_entities();

        // 只遍历匹配组件的实体，不保证顺序；回调内不增删实体或组件。
        template<typename... Components, typename Function> void each(Function&& function) {
            auto view = m_registry.view<QueryComponent<Components>...>();
            for(const auto handle : view)
                function(
                    Entity(handle, this), view.template get<QueryComponent<Components>>(handle)...);
        }

        template<typename Component> [[nodiscard]] std::size_t component_count() const {
            const auto* storage = m_registry.storage<std::remove_const_t<Component>>();
            return storage ? storage->size() : 0;
        }

        [[nodiscard]] bool is_valid(Entity entity) const;

        [[nodiscard]] std::size_t entity_count() const;

        [[nodiscard]] const SceneEnvironment& get_environment() const { return m_environment; }
        [[nodiscard]] bool set_environment(const SceneEnvironment& environment);
        [[nodiscard]] const PostProcessSettings& get_post_process() const { return m_post_process; }
        [[nodiscard]] bool set_post_process(const PostProcessSettings& settings);

    private:
        friend class Entity;
        friend class SceneSerializer;
        friend class ComponentRegistry;

        template<typename Component>
        using QueryComponent = std::conditional_t<is_scene_read_only_component_v<Component>,
            const Component, Component>;

        [[nodiscard]] bool has_cycle(Entity child, Entity parent);
        void remove_child_index(EntityId parent, entt::entity child);
        void mark_transform_dirty(entt::entity handle);
        void update_world_transform(entt::entity handle);
        std::size_t sync_transform_chain(entt::entity handle);

        EntityId m_next_entity_id = 1;
        SceneEnvironment m_environment;
        PostProcessSettings m_post_process;
        entt::registry m_registry;
        std::unordered_map<EntityId, entt::entity> m_entities_by_id;
        std::unordered_map<EntityUuid, entt::entity> m_entities_by_uuid;
        std::unordered_map<EntityId, std::vector<entt::entity>> m_children_by_parent;
        std::unordered_set<entt::entity> m_dirty_transforms;
        std::vector<entt::entity> m_transform_work;
    };

    template<typename T, typename... Args>
        requires(!is_scene_managed_component_v<T>)
    decltype(auto) Entity::add_component(Args&&... args) {
        if constexpr(std::is_same_v<T, TransformComponent>) {
            m_scene->mark_transform_dirty(m_handle);
            return std::as_const(
                m_scene->m_registry.emplace<T>(m_handle, std::forward<Args>(args)...));
        } else {
            return m_scene->m_registry.emplace<T>(m_handle, std::forward<Args>(args)...);
        }
    }

    template<typename T>
        requires(!is_scene_read_only_component_v<T>)
    T& Entity::get_component() {
        return m_scene->m_registry.get<T>(m_handle);
    }

    template<typename Function> bool Entity::try_edit_transform(Function&& edit) const {
        if(!has_component<TransformComponent>())
            return false;
        auto candidate = get_component<TransformComponent>();
        std::forward<Function>(edit)(candidate);
        return try_set_transform(candidate);
    }

    template<typename Function> void Entity::edit_transform(Function&& edit) const {
        require_transform_write(try_edit_transform(std::forward<Function>(edit)));
    }

    template<typename T> const T& Entity::get_component() const {
        return m_scene->m_registry.get<T>(m_handle);
    }

    template<typename T> bool Entity::has_component() const {
        return static_cast<bool>(*this) && m_scene->m_registry.all_of<T>(m_handle);
    }

    template<typename T>
        requires(!is_scene_managed_component_v<T>)
    void Entity::remove_component() const {
        if constexpr(std::is_same_v<T, TransformComponent>) {
            if(has_component<T>())
                m_scene->mark_transform_dirty(m_handle);
        }
        m_scene->m_registry.remove<T>(m_handle);
    }
}
