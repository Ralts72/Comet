#include "scene_commands.h"
#include "scene/scene_serializer.h"

#include <gtest/gtest.h>
#include <stdexcept>

namespace CometEditor::Tests {
    class SceneCommandsTest: public ::testing::Test {
    protected:
        Comet::ComponentRegistry registry = Comet::create_scene_component_registry();
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity("Edited");
        CommandHistory history;
        PropertyEditTransaction edit{history, registry};

        void SetUp() override { history.bind_scene(&scene); }
        bool add(std::string_view component) {
            return SceneCommands::add_component(
                history, registry, entity.get_uuid(), component);
        }
        bool remove(std::string_view component) {
            return SceneCommands::remove_component(
                history, registry, entity.get_uuid(), component);
        }
    };

    TEST_F(SceneCommandsTest, AddEditRemoveShareOneHistoryAndRestoreValues) {
        ASSERT_TRUE(add("camera"));
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "camera", "fov"}));
        ASSERT_TRUE(edit.preview(65.0f));
        ASSERT_TRUE(edit.commit());
        ASSERT_TRUE(remove("camera"));
        EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
        EXPECT_EQ(history.undo_size(), 3);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 65);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 45);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
        ASSERT_TRUE(history.redo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 45);
        ASSERT_TRUE(history.redo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 65);
        ASSERT_TRUE(history.redo());
        EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
    }

    TEST_F(SceneCommandsTest, RemovedMeshRetainsAssetHandlesAndCanBeSaved) {
        entity.add_component<Comet::MeshRendererComponent>(
            Comet::AssetHandle(41), Comet::AssetHandle(42));
        ASSERT_TRUE(remove("mesh_renderer"));
        ASSERT_TRUE(history.undo());
        const auto& mesh = entity.get_component<Comet::MeshRendererComponent>();
        EXPECT_EQ(mesh.mesh, Comet::AssetHandle(41));
        EXPECT_EQ(mesh.material, Comet::AssetHandle(42));
        Comet::SceneSerializer serializer(registry);
        const auto restored = serializer.deserialize(serializer.serialize(scene));
        EXPECT_EQ(restored->find_entity(entity.get_uuid())
                      .get_component<Comet::MeshRendererComponent>()
                      .material,
            Comet::AssetHandle(42));
    }

    TEST_F(SceneCommandsTest, InvalidOperationsDoNotMoveCursorOrDropRedo) {
        ASSERT_TRUE(add("camera"));
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(remove("camera"));
        EXPECT_FALSE(add("missing"));
        EXPECT_FALSE(remove("name"));
        EXPECT_FALSE(remove("transform"));
        EXPECT_EQ(history.redo_size(), 1);
        entity.add_component<Comet::CameraComponent>();
        EXPECT_FALSE(history.redo());
        EXPECT_EQ(history.redo_size(), 1);
        EXPECT_FALSE(add("camera"));
        entity.remove_component<Comet::CameraComponent>();
        ASSERT_TRUE(history.redo());
        scene.destroy_entity(entity);
        EXPECT_FALSE(history.undo());
        EXPECT_EQ(history.undo_size(), 1);
    }

    TEST_F(SceneCommandsTest, ResolvesUuidAfterEntityRestorationAndDoesNotCrossScenes) {
        entity.add_component<Comet::CameraComponent>().fov = 71;
        ASSERT_TRUE(remove("camera"));
        const auto uuid = entity.get_uuid();
        scene.destroy_entity(entity);
        entity = scene.create_entity_with_uuid(uuid);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 71);
        history.bind_scene(nullptr);
        EXPECT_FALSE(remove("camera"));
        EXPECT_TRUE(entity.has_component<Comet::CameraComponent>());
        history.bind_scene(&scene);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(SceneCommandsTest, FailedSnapshotRollsBackNewComponentBeforePropagating) {
        struct BrokenComponent {
            int value = 0;
        };
        auto descriptor =
            Comet::make_component_descriptor<BrokenComponent>("broken", "Broken", {});
        descriptor.capture_component_callback = [](const Comet::Entity&) -> std::any {
            throw std::runtime_error("Cannot capture");
        };
        ASSERT_TRUE(registry.register_component(std::move(descriptor)));
        EXPECT_THROW(static_cast<void>(add("broken")), std::runtime_error);
        EXPECT_FALSE(entity.has_component<BrokenComponent>());
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(SceneCommandsTest, EmptySnapshotLeavesStructureAndRedoUnchanged) {
        struct EmptySnapshotComponent {
            int value = 0;
        };
        auto descriptor = Comet::make_component_descriptor<EmptySnapshotComponent>(
            "empty", "Empty", {});
        descriptor.capture_component_callback = [](const Comet::Entity&) {
            return std::any{};
        };
        ASSERT_TRUE(registry.register_component(std::move(descriptor)));
        ASSERT_TRUE(add("camera"));
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(add("empty"));
        EXPECT_FALSE(entity.has_component<EmptySnapshotComponent>());
        entity.add_component<EmptySnapshotComponent>();
        EXPECT_FALSE(remove("empty"));
        EXPECT_TRUE(entity.has_component<EmptySnapshotComponent>());
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_EQ(history.redo_size(), 1);
    }

    TEST_F(SceneCommandsTest, FailedRestoreDoesNotMoveHistory) {
        struct UnrestorableComponent {
            int value = 0;
        };
        auto descriptor = Comet::make_component_descriptor<UnrestorableComponent>(
            "unrestorable", "Unrestorable", {});
        descriptor.restore_component_callback = [](Comet::Entity&, const std::any&) {
            return false;
        };
        ASSERT_TRUE(registry.register_component(std::move(descriptor)));
        entity.add_component<UnrestorableComponent>();
        ASSERT_TRUE(remove("unrestorable"));
        EXPECT_FALSE(history.undo());
        EXPECT_FALSE(entity.has_component<UnrestorableComponent>());
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_EQ(history.redo_size(), 0);
    }

    TEST_F(SceneCommandsTest, SnapshotIncludesFieldsNotExposedToInspector) {
        struct ExtraComponent {
            float visible = 1;
            std::string hidden = "default";
        };
        ASSERT_TRUE(registry.register_component(
            Comet::make_component_descriptor<ExtraComponent>("extra", "Extra",
                {Comet::make_property_descriptor(
                    "visible", "Visible", &ExtraComponent::visible)})));
        auto& extra = entity.add_component<ExtraComponent>();
        extra.visible = 9;
        extra.hidden = "owned hidden data";
        ASSERT_TRUE(remove("extra"));
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<ExtraComponent>().hidden, "owned hidden data");
        EXPECT_FLOAT_EQ(entity.get_component<ExtraComponent>().visible, 9);
    }
}
