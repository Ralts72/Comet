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

    TEST_F(SceneCommandsTest, CreateRenameDeleteUseOneHistoryAndStableUuid) {
        const auto uuid = SceneCommands::create_entity(history, registry, "Created");
        ASSERT_TRUE(uuid);
        const auto original_id = scene.find_entity(uuid).get_id();
        ASSERT_TRUE(edit.begin({uuid, "name", "name"}));
        ASSERT_TRUE(edit.preview(std::string("Renamed")));
        ASSERT_TRUE(edit.commit());
        ASSERT_TRUE(SceneCommands::delete_entity(history, registry, uuid));
        EXPECT_FALSE(scene.find_entity(uuid));
        ASSERT_TRUE(history.undo());
        EXPECT_NE(scene.find_entity(uuid).get_id(), original_id);
        EXPECT_EQ(scene.find_entity(uuid).get_component<Comet::NameComponent>().name,
            "Renamed");
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(scene.find_entity(uuid).get_component<Comet::NameComponent>().name,
            "Created");
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(scene.find_entity(uuid));
        ASSERT_TRUE(history.redo());
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(scene.find_entity(uuid).get_component<Comet::NameComponent>().name,
            "Renamed");
        ASSERT_TRUE(history.redo());
        EXPECT_FALSE(scene.find_entity(uuid));
    }

    TEST_F(SceneCommandsTest, DeleteRestoresSubtreeAndExternalParentWithoutReusingIds) {
        const auto parent = scene.create_entity("Parent");
        auto child = scene.create_entity("Child");
        child.get_component<Comet::NameComponent>().name.clear();
        child.remove_component<Comet::TransformComponent>();
        child.add_component<Comet::CameraComponent>().fov = 82;
        entity.add_component<Comet::MeshRendererComponent>(
            Comet::AssetHandle(1), Comet::AssetHandle(2));
        ASSERT_TRUE(scene.set_parent(entity, parent));
        ASSERT_TRUE(scene.set_parent(child, entity));
        const auto root_uuid = entity.get_uuid();
        const auto child_uuid = child.get_uuid();
        ASSERT_TRUE(SceneCommands::delete_entity(history, registry, root_uuid));
        EXPECT_EQ(scene.entity_count(), 1);
        EXPECT_TRUE(parent);
        ASSERT_TRUE(history.undo());
        entity = scene.find_entity(root_uuid);
        child = scene.find_entity(child_uuid);
        EXPECT_EQ(scene.get_parent(entity), parent);
        EXPECT_EQ(scene.get_parent(child), entity);
        EXPECT_TRUE(child.get_component<Comet::NameComponent>().name.empty());
        EXPECT_FALSE(child.has_component<Comet::TransformComponent>());
        EXPECT_FLOAT_EQ(child.get_component<Comet::CameraComponent>().fov, 82);
        EXPECT_EQ(entity.get_component<Comet::MeshRendererComponent>().material,
            Comet::AssetHandle(2));
        Comet::SceneSerializer serializer(registry);
        auto loaded = serializer.deserialize(serializer.serialize(scene));
        EXPECT_EQ(
            loaded->get_parent(loaded->find_entity(child_uuid)).get_uuid(), root_uuid);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(scene.entity_count(), 1);
    }

    TEST_F(SceneCommandsTest, RestorePreflightRejectsUuidCollisionAndMissingParent) {
        auto parent = scene.create_entity("Parent");
        ASSERT_TRUE(scene.set_parent(entity, parent));
        const auto uuid = entity.get_uuid();
        const auto parent_uuid = parent.get_uuid();
        ASSERT_TRUE(SceneCommands::delete_entity(history, registry, uuid));
        auto collision = scene.create_entity_with_uuid(uuid, "Collision");
        EXPECT_FALSE(history.undo());
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_EQ(collision.get_component<Comet::NameComponent>().name, "Collision");
        scene.destroy_entity(collision);
        scene.destroy_entity(parent);
        EXPECT_FALSE(history.undo());
        EXPECT_EQ(scene.entity_count(), 0);
        parent = scene.create_entity_with_uuid(parent_uuid);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(scene.get_parent(scene.find_entity(uuid)), parent);
    }

    TEST_F(SceneCommandsTest, UnknownComponentsPreventLossyDelete) {
        struct UnknownComponent {
            int value = 3;
        };
        entity.add_component<UnknownComponent>();
        EXPECT_FALSE(registry.covers_entity(entity));
        EXPECT_FALSE(SceneCommands::delete_entity(history, registry, entity.get_uuid()));
        EXPECT_EQ(entity.get_component<UnknownComponent>().value, 3);
        EXPECT_FALSE(history.can_undo());
        entity.remove_component<UnknownComponent>();
        EXPECT_TRUE(registry.covers_entity(entity));
    }

    TEST_F(SceneCommandsTest, RegisteredButUnrestorableComponentPreventsDelete) {
        struct Unrestorable {
            int value = 17;
        };
        auto component = Comet::make_component_descriptor<Unrestorable>(
            "unrestorable", "Unrestorable", {});
        component.restore_component_callback = {};
        ASSERT_TRUE(registry.register_component(std::move(component)));
        entity.add_component<Unrestorable>();
        EXPECT_TRUE(registry.covers_entity(entity));
        EXPECT_FALSE(SceneCommands::delete_entity(history, registry, entity.get_uuid()));
        EXPECT_EQ(entity.get_component<Unrestorable>().value, 17);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(SceneCommandsTest, RestoreFailureRollsBackEveryNewEntityAndPreservesCursor) {
        struct FallibleComponent {
            int value = 5;
        };
        bool fail = false;
        auto component = Comet::make_component_descriptor<FallibleComponent>(
            "fallible", "Fallible", {});
        auto restore = component.restore_component_callback;
        component.restore_component_callback = [&fail, restore](Comet::Entity& target,
                                                   const std::any& snapshot) {
            return !fail && restore(target, snapshot);
        };
        ASSERT_TRUE(registry.register_component(std::move(component)));
        auto child = scene.create_entity("Child");
        child.add_component<FallibleComponent>();
        ASSERT_TRUE(scene.set_parent(child, entity));
        const auto uuid = entity.get_uuid();
        ASSERT_TRUE(SceneCommands::delete_entity(history, registry, uuid));
        fail = true;
        EXPECT_FALSE(history.undo());
        EXPECT_EQ(scene.entity_count(), 0);
        EXPECT_EQ(history.undo_size(), 1);
        fail = false;
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(scene.entity_count(), 2);
    }

    TEST_F(SceneCommandsTest, UndoCreationDoesNotDeleteUntrackedDescendants) {
        const auto uuid = SceneCommands::create_entity(history, registry);
        ASSERT_TRUE(uuid);
        ASSERT_TRUE(scene.set_parent(entity, scene.find_entity(uuid)));
        EXPECT_FALSE(history.undo());
        EXPECT_TRUE(entity);
        EXPECT_TRUE(scene.find_entity(uuid));
        ASSERT_TRUE(scene.clear_parent(entity));
        ASSERT_TRUE(history.undo());
        EXPECT_TRUE(entity);
    }

    TEST_F(SceneCommandsTest, ReparentKeepsLocalTransformAndRejectsCyclesAndNoOps) {
        const auto parent = scene.create_entity("Parent");
        entity.get_component<Comet::TransformComponent>().translation.x = 7;
        ASSERT_TRUE(SceneCommands::reparent_entity(
            history, entity.get_uuid(), parent.get_uuid()));
        EXPECT_EQ(scene.get_parent(entity), parent);
        EXPECT_FLOAT_EQ(
            entity.get_component<Comet::TransformComponent>().translation.x, 7);
        EXPECT_FALSE(SceneCommands::reparent_entity(
            history, parent.get_uuid(), entity.get_uuid()));
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(scene.get_parent(entity));
        EXPECT_FALSE(SceneCommands::reparent_entity(history, entity.get_uuid()));
        EXPECT_EQ(history.redo_size(), 1);
        ASSERT_TRUE(history.redo());
        ASSERT_TRUE(SceneCommands::reparent_entity(history, entity.get_uuid()));
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(scene.get_parent(entity), parent);
        history.bind_scene(nullptr);
        EXPECT_FALSE(SceneCommands::create_entity(history, registry));
        EXPECT_FALSE(SceneCommands::delete_entity(history, registry, entity.get_uuid()));
        EXPECT_FALSE(SceneCommands::reparent_entity(history, entity.get_uuid()));
    }

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
