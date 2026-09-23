#include <gtest/gtest.h>
#include "scene/components.h"
#include "scene/scene.h"
#include "support/math_assertions.h"
#include <algorithm>
#include <concepts>
#include <type_traits>
#include <utility>
#include <limits>
#include <random>

namespace Comet::Tests {

    template<typename, typename = void> struct HasGetScene: std::false_type {};

    template<typename T>
    struct HasGetScene<T, std::void_t<decltype(std::declval<T>().get_scene())>>: std::true_type {};

    template<typename, typename = void> struct HasGetHandle: std::false_type {};

    template<typename T>
    struct HasGetHandle<T, std::void_t<decltype(std::declval<T>().get_handle())>>: std::true_type {
    };

    static_assert(!std::is_constructible_v<Entity, entt::entity, Scene*, entt::registry*>,
        "Entity construction must not expose the Scene registry");
    static_assert(!std::is_constructible_v<Entity, entt::entity, Scene*>,
        "Only Scene should create Entity handles");
    static_assert(!HasGetScene<Entity>::value, "Entity must not expose its owning Scene");
    static_assert(!HasGetHandle<Entity>::value, "Entity must not expose the raw entt handle");

    template<typename T>
    concept HasMutableComponentAccess = requires(Entity entity) {
        { entity.get_component<T>() } -> std::same_as<T&>;
    };

    template<typename T>
    concept CanAddComponent = requires(Entity entity) { entity.add_component<T>(); };

    template<typename T>
    concept CanRemoveComponent = requires(Entity entity) { entity.remove_component<T>(); };

    static_assert(!HasMutableComponentAccess<IdComponent>);
    static_assert(!HasMutableComponentAccess<UuidComponent>);
    static_assert(!HasMutableComponentAccess<RelationshipComponent>);
    static_assert(!HasMutableComponentAccess<WorldTransformComponent>);
    static_assert(!CanAddComponent<IdComponent>);
    static_assert(!CanAddComponent<NameComponent>);
    static_assert(!CanAddComponent<RelationshipComponent>);
    static_assert(!CanRemoveComponent<UuidComponent>);
    static_assert(!CanRemoveComponent<NameComponent>);
    static_assert(!CanRemoveComponent<WorldTransformComponent>);
    static_assert(HasMutableComponentAccess<NameComponent>);
    static_assert(!HasMutableComponentAccess<TransformComponent>);
    static_assert(
        std::is_same_v<decltype(std::declval<Entity>().add_component<TransformComponent>()),
            const TransformComponent&>);
    static_assert(CanAddComponent<CameraComponent>);
    static_assert(CanRemoveComponent<CameraComponent>);

    TEST(SceneTest, TypedQueryFiltersComponentsAndProtectsSceneManagedValues) {
        Scene scene;
        EXPECT_EQ(scene.component_count<CameraComponent>(), 0u);
        int visited = 0;
        scene.each<CameraComponent>([&](Entity, CameraComponent&) { ++visited; });
        EXPECT_EQ(visited, 0);

        const auto ordinary = scene.create_entity("Ordinary");
        auto camera = scene.create_entity("Camera");
        camera.add_component<CameraComponent>().primary = true;
        scene.each<const CameraComponent, TransformComponent, IdComponent, UuidComponent,
            RelationshipComponent, WorldTransformComponent>(
            [&](Entity entity, auto& lens, auto& transform, auto& id, auto& uuid,
                auto& relationship, auto& world) {
                static_assert(std::is_same_v<decltype(lens), const CameraComponent&>);
                static_assert(std::is_same_v<decltype(transform), const TransformComponent&>);
                static_assert(std::is_same_v<decltype(id), const IdComponent&>);
                static_assert(std::is_same_v<decltype(uuid), const UuidComponent&>);
                static_assert(std::is_same_v<decltype(relationship), const RelationshipComponent&>);
                static_assert(std::is_same_v<decltype(world), const WorldTransformComponent&>);
                EXPECT_EQ(entity, camera);
                EXPECT_EQ(id.id, camera.get_id());
                EXPECT_TRUE(lens.primary);
                EXPECT_TRUE(
                    entity.try_edit_transform([](auto& value) { value.translation.x = 3; }));
                ++visited;
            });
        EXPECT_EQ(visited, 1);
        EXPECT_EQ(scene.component_count<const CameraComponent>(), 1u);
        EXPECT_EQ(camera.get_component<TransformComponent>().translation.x, 3);
        EXPECT_EQ(ordinary.get_component<TransformComponent>().translation.x, 0);
        camera.remove_component<CameraComponent>();
        scene.each<CameraComponent>([&](Entity, CameraComponent&) { ++visited; });
        EXPECT_EQ(visited, 1);
        EXPECT_EQ(scene.component_count<CameraComponent>(), 0u);
    }

    TEST(SceneTest, CreateEntityAddsDefaultComponents) {
        Scene scene;

        Entity entity = scene.create_entity("Camera");

        ASSERT_TRUE(entity);
        EXPECT_TRUE(entity.has_component<IdComponent>());
        EXPECT_TRUE(entity.has_component<UuidComponent>());
        EXPECT_TRUE(entity.has_component<NameComponent>());
        EXPECT_TRUE(entity.has_component<TransformComponent>());
        EXPECT_TRUE(entity.has_component<RelationshipComponent>());
        EXPECT_TRUE(entity.has_component<WorldTransformComponent>());
        EXPECT_EQ(entity.get_component<NameComponent>().name, "Camera");
        EXPECT_EQ(entity.get_component<RelationshipComponent>().parent, INVALID_ENTITY_ID);
        EXPECT_TRUE(TestUtils::IsIdentityMatrix(
            entity.get_component<WorldTransformComponent>().world_matrix));
        EXPECT_NE(entity.get_id(), INVALID_ENTITY_ID);
        EXPECT_TRUE(entity.get_uuid());
        EXPECT_EQ(scene.entity_count(), 1u);
    }

    TEST(SceneTest, CreatesAndFindsEntityByUuid) {
        Scene scene;
        const auto uuid = EntityUuid::parse("550e8400-e29b-41d4-a716-446655440000");
        ASSERT_TRUE(uuid.has_value());

        Entity entity = scene.create_entity_with_uuid(*uuid, "Persistent Entity");

        ASSERT_TRUE(entity);
        EXPECT_EQ(entity.get_uuid(), *uuid);
        EXPECT_EQ(scene.find_entity(*uuid), entity);
        EXPECT_NE(entity.get_id(), INVALID_ENTITY_ID);
    }

    TEST(SceneTest, RejectsInvalidAndDuplicateUuids) {
        Scene scene;
        const auto uuid = EntityUuid::parse("550e8400-e29b-41d4-a716-446655440000");
        ASSERT_TRUE(uuid.has_value());

        EXPECT_FALSE(scene.create_entity_with_uuid(INVALID_ENTITY_UUID));
        ASSERT_TRUE(scene.create_entity_with_uuid(*uuid, "First"));
        EXPECT_FALSE(scene.create_entity_with_uuid(*uuid, "Duplicate"));
        EXPECT_EQ(scene.entity_count(), 1u);
    }

    TEST(SceneTest, InvalidEntityHasInvalidId) {
        Entity entity;

        EXPECT_FALSE(entity);
        EXPECT_EQ(entity.get_id(), INVALID_ENTITY_ID);
        EXPECT_EQ(entity.get_uuid(), INVALID_ENTITY_UUID);
    }

    TEST(SceneTest, CreateEntityUsesDefaultNameWhenNameIsEmpty) {
        Scene scene;

        Entity entity = scene.create_entity("");

        ASSERT_TRUE(entity);
        EXPECT_EQ(entity.get_component<NameComponent>().name, "Entity");
    }

    TEST(SceneTest, DestroyEntityInvalidatesHandle) {
        Scene scene;
        Entity entity = scene.create_entity("Temporary");
        const EntityId id = entity.get_id();
        const EntityUuid uuid = entity.get_uuid();

        scene.destroy_entity(entity);

        EXPECT_FALSE(entity);
        EXPECT_FALSE(scene.find_entity(id));
        EXPECT_FALSE(scene.find_entity(uuid));
        EXPECT_EQ(scene.entity_count(), 0u);
    }

    TEST(SceneTest, FindAndEnumerateEntitiesById) {
        Scene scene;
        Entity first = scene.create_entity("First");
        Entity second = scene.create_entity("Second");

        Entity found = scene.find_entity(second.get_id());
        std::vector<Entity> entities = scene.get_entities();

        ASSERT_TRUE(found);
        EXPECT_EQ(found.get_id(), second.get_id());
        ASSERT_EQ(entities.size(), 2u);
        const auto has_id = [&entities](const EntityId id) {
            return std::any_of(entities.begin(), entities.end(),
                [id](const Entity& entity) { return entity.get_id() == id; });
        };
        EXPECT_TRUE(scene.is_valid(entities[0]));
        EXPECT_TRUE(scene.is_valid(entities[1]));
        EXPECT_TRUE(has_id(first.get_id()));
        EXPECT_TRUE(has_id(second.get_id()));
        EXPECT_FALSE(scene.find_entity(INVALID_ENTITY_ID));
        EXPECT_TRUE(scene.is_valid(first));
        EXPECT_TRUE(scene.is_valid(second));
    }

    TEST(SceneTest, EstablishesAndClearsParentChildRelationships) {
        Scene scene;
        Entity root = scene.create_entity("Root");
        Entity first_child = scene.create_entity("First Child");
        Entity second_child = scene.create_entity("Second Child");

        EXPECT_TRUE(scene.set_parent(first_child, root));
        EXPECT_TRUE(scene.set_parent(second_child, root));
        EXPECT_EQ(scene.get_parent(first_child), root);
        EXPECT_EQ(scene.get_parent(second_child), root);

        const std::vector<Entity> children = scene.get_children(root);
        ASSERT_EQ(children.size(), 2u);
        EXPECT_EQ(children[0], first_child);
        EXPECT_EQ(children[1], second_child);

        const std::vector<Entity> roots = scene.get_root_entities();
        ASSERT_EQ(roots.size(), 1u);
        EXPECT_EQ(roots.front(), root);

        EXPECT_TRUE(scene.clear_parent(first_child));
        EXPECT_FALSE(scene.get_parent(first_child));
        EXPECT_EQ(scene.get_root_entities().size(), 2u);
    }

    TEST(SceneTest, RejectsInvalidAndCyclicParenting) {
        Scene scene;
        Scene other_scene;
        Entity parent = scene.create_entity("Parent");
        Entity child = scene.create_entity("Child");
        Entity grandchild = scene.create_entity("Grandchild");
        Entity foreign = other_scene.create_entity("Foreign");

        ASSERT_TRUE(scene.set_parent(child, parent));
        ASSERT_TRUE(scene.set_parent(grandchild, child));

        EXPECT_FALSE(scene.set_parent(parent, parent));
        EXPECT_FALSE(scene.set_parent(parent, grandchild));
        EXPECT_FALSE(scene.set_parent(child, foreign));
        EXPECT_EQ(scene.get_parent(child), parent);
        EXPECT_EQ(scene.get_parent(grandchild), child);
    }

    TEST(SceneTest, ReparentUpdatesIndexedChildren) {
        Scene scene;
        Entity first_parent = scene.create_entity("First Parent");
        Entity second_parent = scene.create_entity("Second Parent");
        Entity child = scene.create_entity("Child");

        ASSERT_TRUE(scene.set_parent(child, first_parent));
        ASSERT_EQ(scene.get_children(first_parent).size(), 1u);

        ASSERT_TRUE(scene.set_parent(child, second_parent));
        EXPECT_TRUE(scene.get_children(first_parent).empty());
        ASSERT_EQ(scene.get_children(second_parent).size(), 1u);
        EXPECT_EQ(scene.get_children(second_parent).front(), child);

        ASSERT_TRUE(scene.clear_parent(child));
        EXPECT_TRUE(scene.get_children(second_parent).empty());
        EXPECT_EQ(scene.get_root_entities().size(), 3u);
    }

    TEST(SceneTest, ReparentKeepsLocalTransformAndUpdatesWorldMatrix) {
        Scene scene;
        Entity first_parent = scene.create_entity("First Parent");
        Entity second_parent = scene.create_entity("Second Parent");
        Entity child = scene.create_entity("Child");

        EXPECT_TRUE(first_parent.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(1.0f, 0.0f, 0.0f); }));
        EXPECT_TRUE(second_parent.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(5.0f, 0.0f, 0.0f); }));
        const auto& child_transform = child.get_component<TransformComponent>();
        EXPECT_TRUE(child.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(0.0f, 2.0f, 0.0f); }));
        const TransformComponent local_before_reparent = child_transform;

        ASSERT_TRUE(scene.set_parent(child, first_parent));
        EXPECT_TRUE(TestUtils::Mat4Equal(scene.get_world_matrix(child),
            first_parent.get_component<TransformComponent>().to_matrix()
                * child_transform.to_matrix()));

        EXPECT_TRUE(
            first_parent.try_edit_transform([&](auto& value) { value.translation.x = 3.0f; }));
        EXPECT_TRUE(TestUtils::Mat4Equal(scene.get_world_matrix(child),
            first_parent.get_component<TransformComponent>().to_matrix()
                * child_transform.to_matrix()));

        ASSERT_TRUE(scene.set_parent(child, second_parent));
        EXPECT_TRUE(
            TestUtils::Mat4Equal(child_transform.to_matrix(), local_before_reparent.to_matrix()));
        EXPECT_TRUE(TestUtils::Mat4Equal(scene.get_world_matrix(child),
            second_parent.get_component<TransformComponent>().to_matrix()
                * child_transform.to_matrix()));

        ASSERT_TRUE(scene.clear_parent(child));
        EXPECT_TRUE(
            TestUtils::Mat4Equal(scene.get_world_matrix(child), child_transform.to_matrix()));
    }

    TEST(SceneTest, RecomputesOnlyChangedTransformSubtrees) {
        Scene scene;
        auto parent = scene.create_entity("Parent");
        auto child = scene.create_entity("Child");
        auto other = scene.create_entity("Other");
        ASSERT_TRUE(scene.set_parent(child, parent));
        EXPECT_EQ(scene.update_world_transforms(), 3u);
        EXPECT_EQ(scene.update_world_transforms(), 0u);

        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.translation.x = 2; }));
        EXPECT_EQ(scene.update_world_transforms(), 2u);
        EXPECT_EQ(scene.update_world_transforms(), 0u);
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.translation.x = 4; }));
        EXPECT_FLOAT_EQ(scene.get_world_matrix(child)[3].x, 4);
        EXPECT_EQ(scene.update_world_transforms(), 0u);

        EXPECT_TRUE(other.try_edit_transform([&](auto& value) { value.translation.y = 3; }));
        static_cast<void>(scene.get_world_matrix(child));
        EXPECT_EQ(scene.update_world_transforms(), 1u);
        ASSERT_TRUE(scene.set_parent(child, other));
        EXPECT_EQ(scene.update_world_transforms(), 1u);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(child)[3].y, 3);
    }

    TEST(SceneTest, RequiredTransformWritesCommitAndKeepNoOpSemantics) {
        Scene scene;
        auto entity = scene.create_entity();
        entity.set_transform({.translation = {1, 2, 3}});
        EXPECT_EQ(scene.update_world_transforms(), 1u);
        int calls = 0;
        entity.edit_transform([&](auto& value) {
            ++calls;
            value.translation.x = 4;
        });
        EXPECT_EQ(calls, 1);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(entity)[3].x, 4);
        entity.set_transform(entity.get_component<TransformComponent>());
        entity.edit_transform([](auto&) {});
        EXPECT_EQ(scene.update_world_transforms(), 0u);
    }

    TEST(SceneDeathTest, RequiredTransformWritesRejectInvalidContracts) {
        Scene scene;
        auto entity = scene.create_entity();
        EXPECT_DEATH(Entity{}.set_transform({}), "");
        EXPECT_DEATH(entity.edit_transform([](auto& value) {
            value.translation.x = std::numeric_limits<float>::infinity();
        }),
            "");
        entity.remove_component<TransformComponent>();
        EXPECT_DEATH(entity.edit_transform([](auto&) {}), "");
    }

    TEST(SceneTest, TransformWritesCommitCopiesAndRejectInvalidOrUnchangedValues) {
        Scene scene;
        auto entity = scene.create_entity();
        scene.update_world_transforms();
        auto draft = entity.get_component<TransformComponent>();
        draft.translation.x = 7;
        EXPECT_FLOAT_EQ(scene.get_world_matrix(entity)[3].x, 0);
        ASSERT_TRUE(entity.try_set_transform(draft));
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 7);
        EXPECT_FLOAT_EQ(entity.get_component<WorldTransformComponent>().world_matrix[3].x, 0);
        EXPECT_EQ(scene.update_world_transforms(), 1u);
        EXPECT_FLOAT_EQ(entity.get_component<WorldTransformComponent>().world_matrix[3].x, 7);
        EXPECT_TRUE(entity.try_set_transform(draft));
        EXPECT_TRUE(entity.try_edit_transform([](auto&) {}));
        EXPECT_FALSE(entity.try_edit_transform([](auto& value) {
            value.translation.y = 8;
            value.rotation.z = std::numeric_limits<float>::infinity();
        }));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, draft.translation);
        EXPECT_EQ(scene.update_world_transforms(), 0u);
        scene.destroy_entity(entity);
        EXPECT_FALSE(entity.try_set_transform(draft));
        EXPECT_FALSE(entity.try_edit_transform([](auto&) { FAIL() << "Invalid entity callback"; }));
    }

    TEST(SceneTest, ImmediateQueryLeavesOtherDirtyBranchesPendingAndDestructionClearsThem) {
        Scene scene;
        auto root = scene.create_entity();
        auto left = scene.create_entity();
        auto right = scene.create_entity();
        ASSERT_TRUE(scene.set_parent(left, root));
        ASSERT_TRUE(scene.set_parent(right, root));
        scene.update_world_transforms();
        ASSERT_TRUE(root.try_set_transform({.translation = {2, 0, 0}}));
        EXPECT_FLOAT_EQ(scene.get_world_matrix(left)[3].x, 2);
        // 祖先已同步，但另一支仍脏；再次写祖先必须重新覆盖已干净的分支。
        ASSERT_TRUE(root.try_set_transform({.translation = {4, 0, 0}}));
        EXPECT_EQ(scene.update_world_transforms(), 3u);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(right)[3].x, 4);
        ASSERT_TRUE(right.try_set_transform({.translation = {1, 0, 0}}));
        ASSERT_TRUE(scene.clear_parent(right));
        EXPECT_FLOAT_EQ(scene.get_world_matrix(right)[3].x, 1);
        ASSERT_TRUE(root.try_set_transform({.translation = {6, 0, 0}}));
        scene.destroy_entity(root);
        EXPECT_EQ(scene.update_world_transforms(), 0u);
        auto replacement = scene.create_entity();
        EXPECT_TRUE(TestUtils::IsIdentityMatrix(scene.get_world_matrix(replacement)));
        EXPECT_EQ(scene.update_world_transforms(), 0u);
    }

    TEST(SceneTest, DirtyCacheMatchesDirectTrsCompositionAcrossMixedHierarchyEdits) {
        Scene scene;
        std::vector<Entity> entities;
        for(int i = 0; i < 24; ++i)
            entities.push_back(scene.create_entity());
        std::mt19937 random(42);
        for(int step = 0; step < 160; ++step) {
            const auto index = random() % entities.size();
            auto entity = entities[index];
            switch(step % 4) {
                case 0:
                    if(!entity.has_component<TransformComponent>())
                        entity.add_component<TransformComponent>();
                    ASSERT_TRUE(entity.try_set_transform({.translation = {float(step % 7), -2, 1},
                        .rotation = {10, float(step % 45), -20},
                        .scale = {1, -0.5f, 2}}));
                    break;
                case 1:
                    if(index > 0)
                        ASSERT_TRUE(scene.set_parent(entity, entities[random() % index]));
                    break;
                case 2:
                    entity.remove_component<TransformComponent>();
                    break;
                case 3:
                    ASSERT_TRUE(scene.clear_parent(entity));
                    break;
            }
            if(step % 2 == 0)
                static_cast<void>(scene.get_world_matrix(entity));
            scene.update_world_transforms();
            for(const auto target : entities) {
                Math::Mat4 expected(1);
                for(auto current = target; current; current = scene.get_parent(current)) {
                    if(current.has_component<TransformComponent>())
                        expected =
                            current.get_component<TransformComponent>().to_matrix() * expected;
                }
                EXPECT_TRUE(TestUtils::Mat4Equal(
                    target.get_component<WorldTransformComponent>().world_matrix, expected));
            }
            EXPECT_EQ(scene.update_world_transforms(), 0u);
        }
    }

    TEST(SceneTest, RecomputesDescendantsWhenTransformIsRemoved) {
        Scene scene;
        auto parent = scene.create_entity();
        auto child = scene.create_entity();
        ASSERT_TRUE(scene.set_parent(child, parent));
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.translation.x = 5; }));
        EXPECT_EQ(scene.update_world_transforms(), 2u);
        parent.remove_component<TransformComponent>();
        EXPECT_EQ(scene.update_world_transforms(), 2u);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(child)[3].x, 0);
        parent.add_component<Comet::TransformComponent>();
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.translation.x = 9; }));
        EXPECT_EQ(scene.update_world_transforms(), 2u);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(child)[3].x, 9);
    }

    TEST(SceneTest, ReadOnlyTransformReferenceObservesExplicitWrites) {
        Scene scene;
        Entity entity = scene.create_entity();
        const auto& transform = entity.get_component<TransformComponent>();
        EXPECT_TRUE(TestUtils::IsIdentityMatrix(scene.get_world_matrix(entity)));

        EXPECT_TRUE(entity.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(3.0f, 4.0f, 5.0f); }));
        EXPECT_TRUE(TestUtils::Mat4Equal(scene.get_world_matrix(entity), transform.to_matrix()));
        EXPECT_TRUE(entity.try_edit_transform(
            [&](auto& value) { value.rotate(Math::Vec3(0.0f, 45.0f, 0.0f)); }));
        EXPECT_TRUE(TestUtils::Mat4Equal(scene.get_world_matrix(entity), transform.to_matrix()));
    }

    TEST(SceneTest, WorldMatrixReflectsTransformStructureChanges) {
        Scene scene;
        Entity entity = scene.create_entity("Transform");
        EXPECT_TRUE(entity.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(2.0f, 3.0f, 4.0f); }));
        EXPECT_TRUE(TestUtils::Mat4Equal(scene.get_world_matrix(entity),
            Math::translate(Math::Mat4(1.0f), Math::Vec3(2.0f, 3.0f, 4.0f))));

        entity.remove_component<TransformComponent>();
        EXPECT_TRUE(TestUtils::IsIdentityMatrix(scene.get_world_matrix(entity)));

        const auto& transform = entity.add_component<TransformComponent>();
        EXPECT_TRUE(entity.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(-1.0f, 0.5f, 7.0f); }));
        EXPECT_TRUE(TestUtils::Mat4Equal(scene.get_world_matrix(entity), transform.to_matrix()));
    }

    TEST(SceneTest, CameraWorldTransformIgnoresHierarchyScaleButKeepsWorldPosition) {
        Scene scene;
        Entity parent = scene.create_entity("Parent");
        Entity camera = scene.create_entity("Camera");
        const auto& parent_transform = parent.get_component<TransformComponent>();
        EXPECT_TRUE(parent.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(4.0f, 5.0f, 6.0f); }));
        EXPECT_TRUE(parent.try_edit_transform(
            [&](auto& value) { value.rotation = Math::Vec3(0.0f, 35.0f, 0.0f); }));
        EXPECT_TRUE(parent.try_edit_transform(
            [&](auto& value) { value.scale = Math::Vec3(2.0f, 3.0f, 4.0f); }));
        const auto& camera_transform = camera.get_component<TransformComponent>();
        EXPECT_TRUE(camera.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(1.0f, 2.0f, 3.0f); }));
        EXPECT_TRUE(camera.try_edit_transform(
            [&](auto& value) { value.rotation = Math::Vec3(10.0f, 20.0f, 30.0f); }));
        EXPECT_TRUE(camera.try_edit_transform(
            [&](auto& value) { value.scale = Math::Vec3(5.0f, 6.0f, 7.0f); }));
        ASSERT_TRUE(scene.set_parent(camera, parent));

        scene.update_world_transforms();

        const auto& world = camera.get_component<WorldTransformComponent>();
        const Math::Mat4 expected_world =
            parent_transform.to_matrix() * camera_transform.to_matrix();
        Math::Mat4 expected_camera = Math::compose_trs(parent_transform.translation,
                                         parent_transform.rotation, Math::Vec3(1))
                                     * Math::compose_trs(camera_transform.translation,
                                         camera_transform.rotation, Math::Vec3(1));
        expected_camera[3] = expected_world[3];
        EXPECT_TRUE(TestUtils::Mat4Equal(world.world_matrix, expected_world));
        EXPECT_TRUE(TestUtils::Mat4Equal(world.pose_world_matrix, expected_camera));
    }

    TEST(SceneTest, DestroyingParentDestroysEntireSubtree) {
        Scene scene;
        Entity parent = scene.create_entity("Parent");
        Entity child = scene.create_entity("Child");
        Entity grandchild = scene.create_entity("Grandchild");
        Entity survivor = scene.create_entity("Survivor");

        ASSERT_TRUE(scene.set_parent(child, parent));
        ASSERT_TRUE(scene.set_parent(grandchild, child));

        scene.destroy_entity(parent);

        EXPECT_FALSE(parent);
        EXPECT_FALSE(child);
        EXPECT_FALSE(grandchild);
        EXPECT_TRUE(survivor);
        EXPECT_EQ(scene.entity_count(), 1u);
    }

    TEST(SceneTest, EntityManagesCustomComponents) {
        struct HealthComponent {
            explicit HealthComponent(const int initial_value) : value(initial_value) {}

            int value = 0;
        };

        Scene scene;
        Entity entity = scene.create_entity("Player");

        auto& health = entity.add_component<HealthComponent>(100);
        const Entity& const_entity = entity;

        EXPECT_TRUE(entity.has_component<HealthComponent>());
        EXPECT_EQ(health.value, 100);
        EXPECT_EQ(entity.get_component<HealthComponent>().value, 100);
        EXPECT_EQ(const_entity.get_component<HealthComponent>().value, 100);

        entity.remove_component<HealthComponent>();

        EXPECT_FALSE(entity.has_component<HealthComponent>());
    }

    TEST(SceneTest, TransformComponentProvidesValueOperations) {
        TransformComponent transform;
        transform.translation = Math::Vec3(1.0f, 2.0f, 3.0f);
        transform.rotation = Math::Vec3(170.0f, -170.0f, 0.0f);
        transform.scale = Math::Vec3(2.0f);

        transform.rotate(Math::Vec3(20.0f, -20.0f, 360.0f));

        EXPECT_TRUE(TestUtils::Vec3Equal(transform.rotation, Math::Vec3(-170.0f, 170.0f, 0.0f)));
        EXPECT_TRUE(TestUtils::Mat4Equal(transform.to_matrix(),
            Math::compose_trs(transform.translation, transform.rotation, transform.scale)));
    }

    TEST(SceneTest, BuiltInRenderComponentsCanBeAttached) {
        Scene scene;
        Entity entity = scene.create_entity("Renderable");
        const AssetHandle mesh_handle(10);
        const AssetHandle material_handle(20);

        auto& mesh_renderer =
            entity.add_component<MeshRendererComponent>(mesh_handle, material_handle);
        auto& camera = entity.add_component<CameraComponent>();

        EXPECT_EQ(mesh_renderer.mesh, mesh_handle);
        EXPECT_EQ(mesh_renderer.material, material_handle);
        EXPECT_FLOAT_EQ(camera.fov, 45.0f);
        EXPECT_FLOAT_EQ(camera.near_clip, 0.1f);
        EXPECT_FLOAT_EQ(camera.far_clip, 1000.0f);
        EXPECT_FALSE(camera.primary);
    }

    TEST(SceneTest, MeshRendererComponentDefaultsToInvalidAssetHandles) {
        const MeshRendererComponent mesh_renderer;

        EXPECT_EQ(mesh_renderer.mesh, INVALID_ASSET_HANDLE);
        EXPECT_EQ(mesh_renderer.material, INVALID_ASSET_HANDLE);
        EXPECT_FALSE(mesh_renderer.mesh);
        EXPECT_FALSE(mesh_renderer.material);
    }

}
