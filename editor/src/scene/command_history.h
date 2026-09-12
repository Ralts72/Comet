#pragma once

#include "scene/component_registry.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

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

    private:
        std::size_t m_capacity;
        Comet::Scene* m_scene = nullptr;
        std::uint64_t m_generation = 0;
        std::vector<std::unique_ptr<Command>> m_undo;
        std::vector<std::unique_ptr<Command>> m_redo;
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

        PropertyEditTransaction(
            CommandHistory& history, const Comet::ComponentRegistry& registry);
        PropertyEditTransaction(const PropertyEditTransaction&) = delete;
        PropertyEditTransaction& operator=(const PropertyEditTransaction&) = delete;

        [[nodiscard]] bool begin(Target target);
        [[nodiscard]] bool apply(Target target, const Comet::PropertyValue& value);
        [[nodiscard]] bool preview(const Comet::PropertyValue& value);
        [[nodiscard]] bool commit();
        [[nodiscard]] bool cancel();
        [[nodiscard]] bool active() const;
        [[nodiscard]] bool targets(const Target& target) const;

    private:
        struct Edit {
            Target target;
            Comet::PropertyValue before;
            std::uint64_t generation;
        };

        CommandHistory& m_history;
        const Comet::ComponentRegistry& m_registry;
        std::optional<Edit> m_edit;
    };
}
