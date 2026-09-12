#include "scene/command_history.h"

#include <algorithm>
#include <utility>

namespace CometEditor {
    namespace {
        struct ResolvedProperty {
            const Comet::PropertyDescriptor* descriptor = nullptr;
            void* component = nullptr;
        };

        ResolvedProperty resolve(Comet::Scene* scene,
            const Comet::ComponentRegistry& registry,
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
                : m_registry(registry), m_target(std::move(target)),
                  m_before(std::move(before)), m_after(std::move(after)) {}

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
        m_undo.push_back(std::move(command));
        m_redo.clear();
        return true;
    }

    bool CommandHistory::undo() {
        if(!can_undo() || !m_undo.back()->undo(*m_scene))
            return false;
        m_redo.push_back(std::move(m_undo.back()));
        m_undo.pop_back();
        return true;
    }

    bool CommandHistory::redo() {
        if(!can_redo() || !m_redo.back()->redo(*m_scene))
            return false;
        m_undo.push_back(std::move(m_redo.back()));
        m_redo.pop_back();
        return true;
    }

    PropertyEditTransaction::PropertyEditTransaction(
        CommandHistory& history, const Comet::ComponentRegistry& registry)
        : m_history(history), m_registry(registry) {}

    bool PropertyEditTransaction::active() const {
        return m_edit && m_edit->generation == m_history.generation();
    }

    bool PropertyEditTransaction::targets(const Target& target) const {
        return active() && m_edit->target == target;
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
        m_edit = Edit{std::move(target), std::move(*before), m_history.generation()};
        return true;
    }

    bool PropertyEditTransaction::preview(const Comet::PropertyValue& value) {
        if(!active())
            return false;
        const auto property = resolve(m_history.get_scene(), m_registry, m_edit->target);
        return property.descriptor
               && property.descriptor->assign_value(property.component, value);
    }

    bool PropertyEditTransaction::apply(
        Target target, const Comet::PropertyValue& value) {
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
        const auto property = resolve(m_history.get_scene(), m_registry, m_edit->target);
        if(!property.descriptor) {
            m_edit.reset();
            return false;
        }
        const auto after = property.descriptor->copy_value(property.component);
        if(!after) {
            m_edit.reset();
            return false;
        }
        if(!Comet::property_values_equal(m_edit->before, *after)) {
            if(!m_history.record_applied(std::make_unique<PropertyCommand>(
                   m_registry, m_edit->target, m_edit->before, *after)))
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
        const auto property = resolve(m_history.get_scene(), m_registry, m_edit->target);
        const bool restored =
            property.descriptor
            && property.descriptor->assign_value(property.component, m_edit->before);
        m_edit.reset();
        return restored;
    }
}
