#include "scene/selection.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(SelectionServiceTest, SceneSelectionIsExclusiveAndClearedOnDocumentSwitch) {
        Comet::Scene scene;
        SelectionService selection(scene);
        const auto entity = scene.create_entity();
        selection.select_entity(entity.get_id());
        selection.select_scene();
        EXPECT_EQ(selection.get_selected_scene(), &scene);
        EXPECT_FALSE(selection.get_selected_entity());
        EXPECT_FALSE(selection.get_selected_asset());
        selection.select_asset(Comet::AssetHandle(8));
        EXPECT_EQ(selection.get_selected_scene(), nullptr);
        selection.select_scene();
        selection.select_entity(entity.get_id());
        EXPECT_EQ(selection.get_selected_scene(), nullptr);
        selection.select_scene();
        Comet::Scene other;
        selection.set_scene(other);
        EXPECT_EQ(selection.get_selected_scene(), nullptr);
    }

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

        EXPECT_EQ(&selection.get_scene(), &second_scene);
        EXPECT_EQ(selection.get_selected_entity_id(), Comet::INVALID_ENTITY_ID);
        EXPECT_FALSE(selection.get_selected_entity());

        selection.select_entity(second.get_id());
        EXPECT_EQ(selection.get_selected_entity(), second);
    }
    TEST(SelectionServiceTest, MultipleSelectionKeepsPrimaryAndPersistentIdentity) {
        Comet::Scene scene;
        SelectionService selection(scene);
        auto first = scene.create_entity("First");
        auto second = scene.create_entity("Second");
        const auto first_uuid = first.get_uuid();
        const auto second_uuid = second.get_uuid();
        selection.select_entity(first.get_id());
        selection.toggle_entity(second.get_id());
        EXPECT_EQ(selection.get_selected_entities(), (std::vector{first_uuid, second_uuid}));
        EXPECT_EQ(selection.get_selected_entity(), second);
        selection.toggle_entity(first.get_id());
        EXPECT_EQ(selection.get_selected_entities(), (std::vector{second_uuid}));
        selection.select_entities(std::vector{first_uuid, second_uuid, first_uuid});
        scene.destroy_entity(second);
        second = scene.create_entity_with_uuid(second_uuid, "Rebuilt");
        ASSERT_TRUE(second);
        EXPECT_EQ(selection.get_selected_entity(), second);
        EXPECT_TRUE(selection.is_selected(second.get_id()));
        scene.destroy_entity(second);
        EXPECT_EQ(selection.get_selected_entity(), first);
        EXPECT_EQ(selection.get_selected_entities(), (std::vector{first_uuid}));
        selection.select_asset(Comet::AssetHandle(17));
        EXPECT_TRUE(selection.get_selected_entities().empty());
        selection.select_entities(std::vector{first_uuid});
        EXPECT_FALSE(selection.get_selected_asset());
        selection.select_scene();
        EXPECT_TRUE(selection.get_selected_entities().empty());
    }

}
