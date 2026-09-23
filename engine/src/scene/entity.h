#pragma once
#include "common/export.h"
#include "scene/components.h"
#include <entt.hpp>

namespace Comet {
    class Scene;
    class SceneSerializer;

    class COMET_API Entity {
    public:
        Entity() = default;

        [[nodiscard]] EntityId get_id() const;

        [[nodiscard]] EntityUuid get_uuid() const;

        template<typename T, typename... Args>
            requires(!is_scene_managed_component_v<T>)
        decltype(auto) add_component(Args&&... args);

        [[nodiscard]] bool try_set_transform(const TransformComponent& transform) const;
        // 确定有效的内部写入；违反约定时终止，不静默丢弃。
        void set_transform(const TransformComponent& transform) const;

        // 回调编辑临时副本；成功返回后提交，不能持有副本引用。
        template<typename Function> [[nodiscard]] bool try_edit_transform(Function&& edit) const;
        template<typename Function> void edit_transform(Function&& edit) const;

        template<typename T>
            requires(!is_scene_read_only_component_v<T>)
        T& get_component();

        template<typename T> [[nodiscard]] bool has_component() const;

        template<typename T> const T& get_component() const;

        template<typename T>
            requires(!is_scene_managed_component_v<T>)
        void remove_component() const;

        [[nodiscard]] explicit operator bool() const;

        [[nodiscard]] bool operator==(const Entity& other) const {
            return m_handle == other.m_handle && m_scene == other.m_scene;
        }

        [[nodiscard]] bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        friend class Scene;
        friend class SceneSerializer;
        friend class ComponentRegistry;

        Entity(entt::entity handle, Scene* scene);
        static void require_transform_write(bool accepted);

        entt::entity m_handle = entt::null;
        Scene* m_scene = nullptr;
    };
}
