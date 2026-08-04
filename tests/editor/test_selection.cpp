#include "selection.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(SelectionServiceTest, SelectsEntityById) {
        Comet::Scene scene;
        const Comet::Entity entity = scene.create_entity("Selected");
        SelectionService selection(scene);

        selection.select_entity(entity.get_id());

        EXPECT_EQ(selection.get_selected_entity_id(), entity.get_id());
        EXPECT_EQ(selection.get_selected_entity(), entity);
        EXPECT_TRUE(selection.is_selected(entity.get_id()));
    }

    TEST(SelectionServiceTest, RejectsUnknownEntityId) {
        Comet::Scene scene;
        SelectionService selection(scene);

        selection.select_entity(42);

        EXPECT_EQ(selection.get_selected_entity_id(), Comet::INVALID_ENTITY_ID);
        EXPECT_FALSE(selection.get_selected_entity());
    }

    TEST(SelectionServiceTest, ClearsSelectionWhenEntityIsDestroyed) {
        Comet::Scene scene;
        const Comet::Entity entity = scene.create_entity("Selected");
        SelectionService selection(scene);
        selection.select_entity(entity.get_id());

        scene.destroy_entity(entity);

        EXPECT_FALSE(selection.get_selected_entity());
        EXPECT_EQ(selection.get_selected_entity_id(), Comet::INVALID_ENTITY_ID);
    }

    TEST(SelectionServiceTest, ResolvesLiveEntityForComponentEditing) {
        Comet::Scene scene;
        const Comet::Entity entity = scene.create_entity("Before");
        SelectionService selection(scene);
        selection.select_entity(entity.get_id());

        Comet::Entity selected_entity = selection.get_selected_entity();
        selected_entity.get_component<Comet::NameComponent>().name = "After";
        selected_entity.get_component<Comet::TransformComponent>().translation.x = 2.0f;

        const Comet::Entity stored_entity = scene.find_entity(entity.get_id());
        EXPECT_EQ(stored_entity.get_component<Comet::NameComponent>().name, "After");
        EXPECT_FLOAT_EQ(
            stored_entity.get_component<Comet::TransformComponent>().translation.x, 2.0f);
    }
}
