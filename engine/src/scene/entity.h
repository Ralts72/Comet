#pragma once
#include "common/export.h"
#include "scene/components.h"
#include <stdexcept>

namespace Comet {
    class Scene;

    class COMET_API Entity {
    public:
        Entity() = default;

        Entity(entt::entity handle, Scene* scene, entt::registry* registry)
            : m_handle(handle), m_scene(scene), m_registry(registry) {}

        [[nodiscard]] bool is_valid() const {
            return m_registry && m_handle != entt::null && m_registry->valid(m_handle);
        }

        [[nodiscard]] EntityId get_id() const { return get_component<IDComponent>().id; }

        [[nodiscard]] const std::string& get_name() const { return get_component<NameComponent>().name; }

        void set_name(std::string name) { get_component<NameComponent>().name = std::move(name); }

        template<typename T, typename... Args>
        T& add_component(Args&&... args) {
            ensure_valid();
            if(has_component<T>()) {
                throw std::runtime_error("Entity already has requested component");
            }
            return m_registry->emplace<T>(m_handle, std::forward<Args>(args)...);
        }

        template<typename T>
        [[nodiscard]] bool has_component() const {
            return is_valid() && m_registry->all_of<T>(m_handle);
        }

        template<typename T>
        T& get_component() const {
            ensure_valid();
            if(!has_component<T>()) {
                throw std::runtime_error("Entity does not have requested component");
            }
            return m_registry->get<T>(m_handle);
        }

        template<typename T>
        void remove_component() {
            ensure_valid();
            if(!has_component<T>()) {
                throw std::runtime_error("Entity does not have requested component");
            }
            m_registry->remove<T>(m_handle);
        }

        [[nodiscard]] Scene* get_scene() const { return m_scene; }

        [[nodiscard]] entt::entity get_handle() const { return m_handle; }

        [[nodiscard]] explicit operator bool() const { return is_valid(); }

        [[nodiscard]] bool operator==(const Entity& other) const {
            return m_handle == other.m_handle && m_registry == other.m_registry;
        }

        [[nodiscard]] bool operator!=(const Entity& other) const {
            return !(*this == other);
        }

    private:
        void ensure_valid() const {
            if(!is_valid()) {
                throw std::runtime_error("Entity handle is invalid");
            }
        }

        entt::entity m_handle = entt::null;
        Scene* m_scene = nullptr;
        entt::registry* m_registry = nullptr;
    };
}
