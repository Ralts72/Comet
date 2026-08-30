#include "editor_command_history.h"

#include "scene/components.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

#include <memory>

namespace CometEditor::Tests {
    namespace {
        class IntegerDeltaCommand final : public EditorCommand {
        public:
            IntegerDeltaCommand(int& value, const int delta)
                : m_value(value), m_delta(delta) {}

            bool undo() override {
                m_value -= m_delta;
                return true;
            }

            bool redo() override {
                m_value += m_delta;
                return true;
            }

        private:
            int& m_value;
            int m_delta;
        };

        class FailingCommand final : public EditorCommand {
        public:
            bool undo() override { return false; }
            bool redo() override { return false; }
        };
    }

    TEST(EditorCommandHistoryTest, ExecutesUndoesAndRedoesInStackOrder) {
        int value = 0;
        EditorCommandHistory history;

        ASSERT_TRUE(history.execute(
            std::make_unique<IntegerDeltaCommand>(value, 2)));
        ASSERT_TRUE(history.execute(
            std::make_unique<IntegerDeltaCommand>(value, 5)));
        EXPECT_EQ(value, 7);
        EXPECT_EQ(history.undo_size(), 2u);

        ASSERT_TRUE(history.undo());
        EXPECT_EQ(value, 2);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(value, 0);
        EXPECT_FALSE(history.can_undo());

        ASSERT_TRUE(history.redo());
        EXPECT_EQ(value, 2);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(value, 7);
        EXPECT_FALSE(history.can_redo());
    }

    TEST(EditorCommandHistoryTest, NewBranchClearsRedoAndCapacityDropsOldest) {
        int value = 0;
        EditorCommandHistory history(2);

        ASSERT_TRUE(history.execute(
            std::make_unique<IntegerDeltaCommand>(value, 1)));
        ASSERT_TRUE(history.execute(
            std::make_unique<IntegerDeltaCommand>(value, 2)));
        ASSERT_TRUE(history.execute(
            std::make_unique<IntegerDeltaCommand>(value, 4)));
        EXPECT_EQ(history.undo_size(), 2u);

        ASSERT_TRUE(history.undo());
        EXPECT_EQ(value, 3);
        ASSERT_TRUE(history.execute(
            std::make_unique<IntegerDeltaCommand>(value, 8)));
        EXPECT_EQ(value, 11);
        EXPECT_FALSE(history.can_redo());

        ASSERT_TRUE(history.undo());
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(value, 1);
        EXPECT_FALSE(history.undo());
    }

    TEST(EditorCommandHistoryTest, RecordsAlreadyAppliedInteractionAsOneCommand) {
        int value = 9;
        EditorCommandHistory history;

        ASSERT_TRUE(history.push_applied(
            std::make_unique<IntegerDeltaCommand>(value, 9)));
        EXPECT_EQ(value, 9);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(value, 0);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(value, 9);
    }

    TEST(EditorCommandHistoryTest, FailedCommandDoesNotChangeStacks) {
        EditorCommandHistory history;

        EXPECT_FALSE(history.execute(std::make_unique<FailingCommand>()));
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(history.push_applied(nullptr));
        EXPECT_FALSE(history.can_undo());
    }

    TEST(EntityPropertyEditCommandTest, ResolvesEntityAndPropertyByStableIds) {
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity("Editable");
        const Comet::EntityUuid uuid = entity.get_uuid();
        const Comet::ComponentRegistry registry =
            Comet::create_scene_component_registry();
        const Comet::Math::Vec3 before(0.0f);
        const Comet::Math::Vec3 after(4.0f, -2.0f, 7.0f);
        EditorCommandHistory history;

        ASSERT_TRUE(history.execute(
            std::make_unique<EntityPropertyEditCommand>(
                [&scene]() { return &scene; },
                registry,
                uuid,
                "transform",
                "translation",
                Comet::PropertyValue(before),
                Comet::PropertyValue(after))));
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation,
            after);

        ASSERT_TRUE(history.undo());
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation,
            before);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(
            entity.get_component<Comet::TransformComponent>().translation,
            after);
    }

    TEST(EntityPropertyEditCommandTest, MissingStableTargetFailsWithoutMovingHistory) {
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity("Temporary");
        const Comet::ComponentRegistry registry =
            Comet::create_scene_component_registry();
        EditorCommandHistory history;
        entity.get_component<Comet::TransformComponent>().translation.x = 3.0f;

        ASSERT_TRUE(history.push_applied(
            std::make_unique<EntityPropertyEditCommand>(
                [&scene]() { return &scene; },
                registry,
                entity.get_uuid(),
                "transform",
                "translation",
                Comet::PropertyValue(Comet::Math::Vec3(0.0f)),
                Comet::PropertyValue(Comet::Math::Vec3(3.0f, 0.0f, 0.0f)))));
        scene.destroy_entity(entity);

        EXPECT_FALSE(history.undo());
        EXPECT_EQ(history.undo_size(), 1u);
        EXPECT_EQ(history.redo_size(), 0u);
        history.clear();
        EXPECT_FALSE(history.can_undo());
    }
}
