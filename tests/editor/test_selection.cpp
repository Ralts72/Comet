#include "selection.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(SelectionServiceTest, RejectsUnknownEntityId) {
        Comet::Scene scene;
        SelectionService selection(scene);

        selection.select_entity(42);

        EXPECT_EQ(selection.get_selected_entity_id(), Comet::INVALID_ENTITY_ID);
        EXPECT_FALSE(selection.get_selected_entity());
    }

    TEST(SelectionServiceTest, EntityAndAssetSelectionsAreMutuallyExclusive) {
        Comet::Scene scene;
        const Comet::Entity entity = scene.create_entity("Selected");
        SelectionService selection(scene);

        selection.select_asset(Comet::AssetHandle(73));
        selection.select_entity(entity.get_id());
        EXPECT_EQ(selection.get_selected_entity(), entity);
        EXPECT_EQ(selection.get_selected_entity_id(), entity.get_id());
        EXPECT_TRUE(selection.is_selected(entity.get_id()));
        EXPECT_FALSE(selection.get_selected_asset());

        selection.select_asset(Comet::AssetHandle(42));
        EXPECT_FALSE(selection.get_selected_entity());
        EXPECT_EQ(selection.get_selected_asset(), Comet::AssetHandle(42));
        EXPECT_TRUE(selection.is_selected(Comet::AssetHandle(42)));

        selection.select_asset(Comet::INVALID_ASSET_HANDLE);
        EXPECT_FALSE(selection.get_selected_asset());
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

    TEST(SelectionServiceTest, RebindingSceneClearsSelection) {
        Comet::Scene first_scene;
        const Comet::Entity first = first_scene.create_entity("First");
        SelectionService selection(first_scene);
        selection.select_entity(first.get_id());

        Comet::Scene second_scene;
        const Comet::Entity second = second_scene.create_entity("Second");
        ASSERT_EQ(first.get_id(), second.get_id());

        selection.set_scene(second_scene);

        EXPECT_EQ(selection.get_selected_entity_id(), Comet::INVALID_ENTITY_ID);
        EXPECT_FALSE(selection.get_selected_entity());

        selection.select_entity(second.get_id());
        EXPECT_EQ(selection.get_selected_entity(), second);
    }
}
