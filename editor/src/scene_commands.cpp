#include "scene_commands.h"

namespace CometEditor::SceneCommands {
    namespace {
        class ComponentCommand final: public CommandHistory::Command {
        public:
            ComponentCommand(const Comet::ComponentRegistry& registry,
                Comet::EntityUuid entity, std::string component, bool adding)
                : m_registry(registry), m_entity(entity),
                  m_component(std::move(component)), m_adding(adding) {}

            bool undo(Comet::Scene& scene) override { return apply(scene, !m_adding); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_adding); }

        private:
            bool apply(Comet::Scene& scene, bool present) {
                auto entity = scene.find_entity(m_entity);
                const auto* component = m_registry.find_component(m_component);
                if(!entity || !component || !can_edit_component_structure(*component)
                    || component->has_component(entity) == present)
                    return false;
                if(present) {
                    if(m_snapshot.has_value())
                        return component->restore_component(entity, m_snapshot);
                    if(!component->add_component(entity))
                        return false;
                    // 默认组件也保存一次，Redo 不依赖之后可能变化的默认值。
                    try {
                        m_snapshot = component->capture_component(entity);
                    } catch(...) {
                        static_cast<void>(component->remove_component(entity));
                        throw;
                    }
                    if(!m_snapshot.has_value()) {
                        static_cast<void>(component->remove_component(entity));
                        return false;
                    }
                    return true;
                }
                auto snapshot = component->capture_component(entity);
                if(!snapshot.has_value() || !component->remove_component(entity))
                    return false;
                m_snapshot = std::move(snapshot);
                return true;
            }

            const Comet::ComponentRegistry& m_registry;
            Comet::EntityUuid m_entity;
            std::string m_component;
            bool m_adding;
            std::any m_snapshot;
        };
    }

    bool can_edit_component_structure(const Comet::ComponentDescriptor& component) {
        // Transform 是编辑器实体的基础空间信息，不开放结构编辑；序列化能力不变。
        return component.id != "transform" && component.add_component_callback
               && component.remove_component_callback
               && component.capture_component_callback
               && component.restore_component_callback;
    }

    bool add_component(CommandHistory& history, const Comet::ComponentRegistry& registry,
        Comet::EntityUuid entity, std::string_view component) {
        return history.execute(std::make_unique<ComponentCommand>(
            registry, entity, std::string(component), true));
    }

    bool remove_component(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
        std::string_view component) {
        return history.execute(std::make_unique<ComponentCommand>(
            registry, entity, std::string(component), false));
    }
}
