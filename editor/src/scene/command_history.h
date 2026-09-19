#pragma once

#include "scene/component_registry.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <variant>
#include <utility>

namespace CometEditor {
    class CommandHistory {
    public:
        class Command {
        public:
            virtual ~Command() = default;
            // 返回失败时不能留下部分修改；历史仅在成功后移动游标。
            [[nodiscard]] virtual bool undo(Comet::Scene& scene) = 0;
            [[nodiscard]] virtual bool redo(Comet::Scene& scene) = 0;
        };

        explicit CommandHistory(std::size_t capacity = 256);
        CommandHistory(const CommandHistory&) = delete;
        CommandHistory& operator=(const CommandHistory&) = delete;

        // 更换 Scene 时总是清空，不能用地址相等判断是否仍为同一文档。
        void bind_scene(Comet::Scene* scene);
        void clear();
        [[nodiscard]] bool execute(std::unique_ptr<Command> command);
        [[nodiscard]] bool record_applied(std::unique_ptr<Command> command);
        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();
        [[nodiscard]] bool can_undo() const { return m_scene && !m_undo.empty(); }
        [[nodiscard]] bool can_redo() const { return m_scene && !m_redo.empty(); }
        [[nodiscard]] std::size_t undo_size() const { return m_undo.size(); }
        [[nodiscard]] std::size_t redo_size() const { return m_redo.size(); }
        [[nodiscard]] Comet::Scene* get_scene() const { return m_scene; }
        [[nodiscard]] std::uint64_t generation() const { return m_generation; }
        [[nodiscard]] std::uint64_t state_id() const { return m_state_id; }

    private:
        std::size_t m_capacity;
        Comet::Scene* m_scene = nullptr;
        std::uint64_t m_generation = 0;
        struct Entry {
            std::unique_ptr<Command> command;
            std::uint64_t before;
            std::uint64_t after;
        };
        std::uint64_t m_next_state_id = 1;
        std::uint64_t m_state_id = 0;
        std::vector<Entry> m_undo;
        std::vector<Entry> m_redo;
    };

    // Inspector 与 Gizmo 共用，不依赖 ImGui，也不持有组件地址。
    class PropertyEditTransaction {
    public:
        struct Target {
            Comet::EntityUuid entity;
            std::string component;
            std::string property;
            bool operator==(const Target&) const = default;
        };

        template<typename Value> struct SceneTarget {
            const Value& (Comet::Scene::*get)() const = nullptr;
            bool (Comet::Scene::*set)(const Value&) = nullptr;
            bool operator==(const SceneTarget&) const = default;
        };

        PropertyEditTransaction(CommandHistory& history, const Comet::ComponentRegistry& registry);
        PropertyEditTransaction(const PropertyEditTransaction&) = delete;
        PropertyEditTransaction& operator=(const PropertyEditTransaction&) = delete;

        [[nodiscard]] bool begin(Target target);
        [[nodiscard]] bool editing_scene() const;
        [[nodiscard]] bool apply(Target target, const Comet::PropertyValue& value);
        [[nodiscard]] bool preview(const Comet::PropertyValue& value);
        [[nodiscard]] bool commit();
        [[nodiscard]] bool cancel();
        [[nodiscard]] bool active() const;
        [[nodiscard]] bool targets(const Target& target) const;

        template<typename Value> [[nodiscard]] bool targets(SceneTarget<Value> target) const {
            if(!editing_scene())
                return false;
            const auto* edit = dynamic_cast<const SceneEdit<Value>*>(
                std::get<std::unique_ptr<SceneEditBase>>(m_edit->change).get());
            return edit && edit->target == target;
        }

        template<typename Value> [[nodiscard]] bool begin(SceneTarget<Value> target) {
            if(!target.get || !target.set)
                return false;
            if(targets(target))
                return true;
            if(!commit() || !m_history.get_scene())
                return false;
            m_edit = Edit{
                std::make_unique<SceneEdit<Value>>(target, (m_history.get_scene()->*target.get)()),
                m_history.generation(), m_history.state_id()};
            return true;
        }

        template<typename Value>
        [[nodiscard]] bool preview(SceneTarget<Value> target, const Value& value) {
            return targets(target) && (m_history.get_scene()->*target.set)(value);
        }

        template<typename Value>
        [[nodiscard]] bool apply(SceneTarget<Value> target, const Value& value) {
            if(!commit() || !begin(target))
                return false;
            if(preview(target, value) && commit())
                return true;
            static_cast<void>(cancel());
            return false;
        }

    private:
        class SceneEditBase: public CommandHistory::Command {
        public:
            [[nodiscard]] virtual bool capture_after(const Comet::Scene& scene) = 0;
        };

        template<typename Value> class SceneEdit final: public SceneEditBase {
        public:
            SceneEdit(SceneTarget<Value> target, const Value& before)
                : target(target), m_before(before), m_after(before) {}

            bool undo(Comet::Scene& scene) override { return (scene.*target.set)(m_before); }
            bool redo(Comet::Scene& scene) override { return (scene.*target.set)(m_after); }
            bool capture_after(const Comet::Scene& scene) override {
                m_after = (scene.*target.get)();
                return m_before != m_after;
            }

            const SceneTarget<Value> target;

        private:
            Value m_before;
            Value m_after;
        };

        struct ComponentEdit {
            Target target;
            Comet::PropertyValue before;
        };
        struct Edit {
            std::variant<ComponentEdit, std::unique_ptr<SceneEditBase>> change;
            std::uint64_t generation;
            std::uint64_t history_state;
        };

        CommandHistory& m_history;
        const Comet::ComponentRegistry& m_registry;
        std::optional<Edit> m_edit;
    };
}
