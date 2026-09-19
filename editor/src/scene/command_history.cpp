#include "scene/command_history.h"

#include <algorithm>
#include <utility>

namespace CometEditor {
    namespace {
        struct ResolvedProperty {
            const Comet::PropertyDescriptor* descriptor = nullptr;
            void* component = nullptr;
        };

        ResolvedProperty resolve(Comet::Scene* scene, const Comet::ComponentRegistry& registry,
            const PropertyEditTransaction::Target& target) {
            if(!scene)
                return {};
            auto entity = scene->find_entity(target.entity);
            const auto* component = registry.find_component(target.component);
            if(!entity || !component)
                return {};
            const auto* property = component->find_property(target.property);
            if(!property || !property->editable || property->read_only)
                return {};
            return {property, component->get_component(entity)};
        }

        class PropertyCommand final: public CommandHistory::Command {
        public:
            PropertyCommand(const Comet::ComponentRegistry& registry,
                PropertyEditTransaction::Target target, Comet::PropertyValue before,
                Comet::PropertyValue after)
                : m_registry(registry), m_target(std::move(target)), m_before(std::move(before)),
                  m_after(std::move(after)) {}

            bool undo(Comet::Scene& scene) override { return apply(scene, m_before); }
            bool redo(Comet::Scene& scene) override { return apply(scene, m_after); }

        private:
            bool apply(Comet::Scene& scene, const Comet::PropertyValue& value) const {
                const auto property = resolve(&scene, m_registry, m_target);
                return property.descriptor
                       && property.descriptor->assign_value(property.component, value);
            }

            const Comet::ComponentRegistry& m_registry;
            PropertyEditTransaction::Target m_target;
            Comet::PropertyValue m_before;
            Comet::PropertyValue m_after;
        };
    }

    CommandHistory::CommandHistory(const std::size_t capacity)
        : m_capacity(std::max<std::size_t>(capacity, 1)) {
        // 先分配两条历史分支，命令成功修改场景后入栈不再分配。
        m_undo.reserve(m_capacity);
        m_redo.reserve(m_capacity);
    }

    void CommandHistory::bind_scene(Comet::Scene* scene) {
        clear();
        m_scene = scene;
    }

    void CommandHistory::clear() {
        m_undo.clear();
        m_redo.clear();
        ++m_generation;
        m_state_id = m_next_state_id++;
    }

    bool CommandHistory::execute(std::unique_ptr<Command> command) {
        if(!m_scene || !command || !command->redo(*m_scene))
            return false;
        return record_applied(std::move(command));
    }

    bool CommandHistory::record_applied(std::unique_ptr<Command> command) {
        if(!m_scene || !command)
            return false;
        if(m_undo.size() == m_capacity)
            m_undo.erase(m_undo.begin());
        const auto after = m_next_state_id++;
        m_undo.push_back({std::move(command), m_state_id, after});
        m_state_id = after;
        m_redo.clear();
        return true;
    }

    bool CommandHistory::undo() {
        if(!can_undo() || !m_undo.back().command->undo(*m_scene))
            return false;
        m_state_id = m_undo.back().before;
        m_redo.push_back(std::move(m_undo.back()));
        m_undo.pop_back();
        return true;
    }

    bool CommandHistory::redo() {
        if(!can_redo() || !m_redo.back().command->redo(*m_scene))
            return false;
        m_state_id = m_redo.back().after;
        m_undo.push_back(std::move(m_redo.back()));
        m_redo.pop_back();
        return true;
    }

    PropertyEditTransaction::PropertyEditTransaction(
        CommandHistory& history, const Comet::ComponentRegistry& registry)
        : m_history(history), m_registry(registry) {}

    bool PropertyEditTransaction::active() const {
        return m_edit && m_edit->generation == m_history.generation()
               && m_edit->history_state == m_history.state_id();
    }

    bool PropertyEditTransaction::targets(const Target& target) const {
        if(!active())
            return false;
        const auto* component = std::get_if<ComponentEdit>(&m_edit->change);
        return component && component->target == target;
    }

    bool PropertyEditTransaction::begin(Target target) {
        if(targets(target))
            return true;
        if(!commit())
            return false;
        const auto property = resolve(m_history.get_scene(), m_registry, target);
        if(!property.descriptor)
            return false;
        auto before = property.descriptor->copy_value(property.component);
        if(!before)
            return false;
        m_edit = Edit{ComponentEdit{std::move(target), std::move(*before)}, m_history.generation(),
            m_history.state_id()};
        return true;
    }

    bool PropertyEditTransaction::editing_scene() const {
        return active() && !std::holds_alternative<ComponentEdit>(m_edit->change);
    }

    bool PropertyEditTransaction::preview(const Comet::PropertyValue& value) {
        if(!active())
            return false;
        const auto* edit = std::get_if<ComponentEdit>(&m_edit->change);
        if(!edit)
            return false;
        const auto property = resolve(m_history.get_scene(), m_registry, edit->target);
        return property.descriptor && property.descriptor->assign_value(property.component, value);
    }

    bool PropertyEditTransaction::apply(Target target, const Comet::PropertyValue& value) {
        if(!commit() || !begin(std::move(target)))
            return false;
        if(preview(value) && commit())
            return true;
        static_cast<void>(cancel());
        return false;
    }

    bool PropertyEditTransaction::commit() {
        if(!active()) {
            m_edit.reset();
            return true;
        }
        if(auto* edit = std::get_if<std::unique_ptr<SceneEditBase>>(&m_edit->change)) {
            // 预览已修改文档，只记录前后值，不再恢复旧值后重放一次。
            const bool recorded = !(*edit)->capture_after(*m_history.get_scene())
                                  || m_history.record_applied(std::move(*edit));
            m_edit.reset();
            return recorded;
        }
        const auto& edit = std::get<ComponentEdit>(m_edit->change);
        const auto property = resolve(m_history.get_scene(), m_registry, edit.target);
        if(!property.descriptor) {
            m_edit.reset();
            return false;
        }
        const auto after = property.descriptor->copy_value(property.component);
        if(!after) {
            m_edit.reset();
            return false;
        }
        if(!Comet::property_values_equal(edit.before, *after)) {
            if(!m_history.record_applied(
                   std::make_unique<PropertyCommand>(m_registry, edit.target, edit.before, *after)))
                return false;
        }
        m_edit.reset();
        return true;
    }

    bool PropertyEditTransaction::cancel() {
        if(!active()) {
            m_edit.reset();
            return true;
        }
        if(const auto* edit = std::get_if<std::unique_ptr<SceneEditBase>>(&m_edit->change)) {
            const bool restored = (*edit)->undo(*m_history.get_scene());
            m_edit.reset();
            return restored;
        }
        const auto& edit = std::get<ComponentEdit>(m_edit->change);
        const auto property = resolve(m_history.get_scene(), m_registry, edit.target);
        const bool restored = property.descriptor
                              && property.descriptor->assign_value(property.component, edit.before);
        m_edit.reset();
        return restored;
    }
}
