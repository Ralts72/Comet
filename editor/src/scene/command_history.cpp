#include "scene/command_history.h"
#include "diagnostics/logger.h"

#include <algorithm>
#include <utility>

namespace CometEditor {
    namespace {
        struct ResolvedProperty {
            const Comet::PropertyDescriptor* descriptor = nullptr;
            const Comet::ComponentDescriptor* component = nullptr;
            Comet::Entity entity;

            std::optional<Comet::PropertyValue> read() const {
                if(!descriptor)
                    return std::nullopt;
                return descriptor->copy_value(component->get_component(entity));
            }

            bool write(const Comet::PropertyValue& value) const {
                return descriptor && component->assign_property(entity, descriptor->id, value);
            }
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
            return {property, component, entity};
        }

        using Targets = std::vector<PropertyEditTransaction::Target>;
        using Values = std::vector<Comet::PropertyValue>;

        std::optional<Values> read_values(Comet::Scene* scene,
            const Comet::ComponentRegistry& registry,
            std::span<const PropertyEditTransaction::Target> targets) {
            Values values;
            values.reserve(targets.size());
            for(const auto& target : targets) {
                auto value = resolve(scene, registry, target).read();
                if(!value)
                    return std::nullopt;
                values.push_back(std::move(*value));
            }
            return values;
        }

        bool write_values(Comet::Scene* scene, const Comet::ComponentRegistry& registry,
            std::span<const PropertyEditTransaction::Target> targets,
            std::span<const Comet::PropertyValue> values) {
            if(targets.size() != values.size())
                return false;
            if(targets.size() == 1)
                return resolve(scene, registry, targets.front()).write(values.front());
            const auto before = read_values(scene, registry, targets);
            if(!before)
                return false;
            for(std::size_t i = 0; i < targets.size(); ++i) {
                if(resolve(scene, registry, targets[i]).write(values[i]))
                    continue;
                // 任一属性拒绝修改，恢复本次预览前已经写入的值。
                for(std::size_t restored = 0; restored <= i; ++restored)
                    if(!resolve(scene, registry, targets[restored]).write((*before)[restored]))
                        LOG_ERROR("Cannot restore rejected property batch");
                return false;
            }
            return true;
        }

        class PropertyCommand final: public CommandHistory::Command {
        public:
            PropertyCommand(const Comet::ComponentRegistry& registry, Targets targets,
                Values before, Values after)
                : m_registry(registry), m_targets(std::move(targets)), m_before(std::move(before)),
                  m_after(std::move(after)) {}
            bool undo(Comet::Scene& scene) override {
                return write_values(&scene, m_registry, m_targets, m_before);
            }
            bool redo(Comet::Scene& scene) override {
                return write_values(&scene, m_registry, m_targets, m_after);
            }

        private:
            const Comet::ComponentRegistry& m_registry;
            Targets m_targets;
            Values m_before, m_after;
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
        return targets_many(std::span(&target, 1));
    }

    bool PropertyEditTransaction::targets_many(std::span<const Target> targets) const {
        if(!active())
            return false;
        const auto* component = std::get_if<ComponentEdit>(&m_edit->change);
        return component && std::ranges::equal(component->targets, targets);
    }

    bool PropertyEditTransaction::begin(Target target) {
        return begin_many(std::span(&target, 1));
    }

    bool PropertyEditTransaction::begin_many(std::span<const Target> targets) {
        if(targets.empty())
            return false;
        if(targets_many(targets))
            return true;
        if(!commit())
            return false;
        for(std::size_t i = 0; i < targets.size(); ++i)
            for(std::size_t j = 0; j < i; ++j)
                if(targets[i] == targets[j])
                    return false;
        auto before = read_values(m_history.get_scene(), m_registry, targets);
        if(!before)
            return false;
        m_edit = Edit{ComponentEdit{{targets.begin(), targets.end()}, std::move(*before)},
            m_history.generation(), m_history.state_id()};
        return true;
    }

    bool PropertyEditTransaction::editing_scene() const {
        return active() && !std::holds_alternative<ComponentEdit>(m_edit->change);
    }

    bool PropertyEditTransaction::preview(const Comet::PropertyValue& value) {
        return preview_many(std::span(&value, 1));
    }

    bool PropertyEditTransaction::preview_many(std::span<const Comet::PropertyValue> values) {
        if(!active())
            return false;
        const auto* edit = std::get_if<ComponentEdit>(&m_edit->change);
        return edit && write_values(m_history.get_scene(), m_registry, edit->targets, values);
    }

    bool PropertyEditTransaction::apply(Target target, const Comet::PropertyValue& value) {
        return apply_many(std::span(&target, 1), std::span(&value, 1));
    }

    bool PropertyEditTransaction::apply_many(
        std::span<const Target> targets, std::span<const Comet::PropertyValue> values) {
        if(!commit() || !begin_many(targets))
            return false;
        if(preview_many(values) && commit())
            return true;
        static_cast<void>(cancel());
        return false;
    }

    bool PropertyEditTransaction::write_many(Comet::Scene& scene, std::span<const Target> targets,
        std::span<const Comet::PropertyValue> values) const {
        return !targets.empty() && write_values(&scene, m_registry, targets, values);
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
        const auto after = read_values(m_history.get_scene(), m_registry, edit.targets);
        if(!after) {
            static_cast<void>(cancel());
            return false;
        }
        if(edit.before != *after) {
            if(!m_history.record_applied(std::make_unique<PropertyCommand>(
                   m_registry, edit.targets, edit.before, *after)))
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
        // 被删除的目标无法恢复，但其余成员仍应退出预览，不能留在半次拖动的位置。
        bool restored = true;
        for(std::size_t i = 0; i < edit.targets.size(); ++i)
            restored =
                resolve(m_history.get_scene(), m_registry, edit.targets[i]).write(edit.before[i])
                && restored;
        m_edit.reset();
        return restored;
    }
}
