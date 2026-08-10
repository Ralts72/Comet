#include <gtest/gtest.h>
#include "scene/components.h"
#include "scene/scene.h"
#include "../test_utils.h"
#include <algorithm>
#include <type_traits>
#include <utility>

namespace Comet::Tests {

template<typename, typename = void>
struct HasGetScene : std::false_type {};

template<typename T>
struct HasGetScene<T, std::void_t<decltype(std::declval<T>().get_scene())>> : std::true_type {};

template<typename, typename = void>
struct HasGetHandle : std::false_type {};

template<typename T>
struct HasGetHandle<T, std::void_t<decltype(std::declval<T>().get_handle())>> : std::true_type {};

static_assert(!std::is_constructible_v<Entity, entt::entity, Scene*, entt::registry*>,
              "Entity construction must not expose the Scene registry");
static_assert(!std::is_constructible_v<Entity, entt::entity, Scene*>,
              "Only Scene should create Entity handles");
static_assert(!HasGetScene<Entity>::value, "Entity must not expose its owning Scene");
static_assert(!HasGetHandle<Entity>::value, "Entity must not expose the raw entt handle");

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
    EXPECT_EQ(
        entity.get_component<RelationshipComponent>().parent,
        INVALID_ENTITY_ID);
    EXPECT_TRUE(TestUtils::IsIdentityMatrix(
        entity.get_component<WorldTransformComponent>().world_matrix));
    EXPECT_NE(entity.get_id(), INVALID_ENTITY_ID);
    EXPECT_TRUE(entity.get_uuid());
    EXPECT_EQ(scene.entity_count(), 1u);
}

TEST(SceneTest, CreatesAndFindsEntityByUuid) {
    Scene scene;
    const auto uuid = EntityUuid::parse(
        "550e8400-e29b-41d4-a716-446655440000");
    ASSERT_TRUE(uuid.has_value());

    Entity entity = scene.create_entity_with_uuid(*uuid, "Persistent Entity");

    ASSERT_TRUE(entity);
    EXPECT_EQ(entity.get_uuid(), *uuid);
    EXPECT_EQ(scene.find_entity(*uuid), entity);
    EXPECT_NE(entity.get_id(), INVALID_ENTITY_ID);
}

TEST(SceneTest, RejectsInvalidAndDuplicateUuids) {
    Scene scene;
    const auto uuid = EntityUuid::parse(
        "550e8400-e29b-41d4-a716-446655440000");
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

    scene.destroy_entity(entity);

    EXPECT_FALSE(entity);
    EXPECT_FALSE(scene.find_entity(id));
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
        return std::any_of(entities.begin(), entities.end(), [id](const Entity& entity) {
            return entity.get_id() == id;
        });
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

TEST(SceneTest, ReparentKeepsLocalTransformAndUpdatesWorldMatrix) {
    Scene scene;
    Entity first_parent = scene.create_entity("First Parent");
    Entity second_parent = scene.create_entity("Second Parent");
    Entity child = scene.create_entity("Child");

    first_parent.get_component<TransformComponent>().translation =
        Math::Vec3(1.0f, 0.0f, 0.0f);
    second_parent.get_component<TransformComponent>().translation =
        Math::Vec3(5.0f, 0.0f, 0.0f);
    auto& child_transform = child.get_component<TransformComponent>();
    child_transform.translation = Math::Vec3(0.0f, 2.0f, 0.0f);
    const TransformComponent local_before_reparent = child_transform;

    ASSERT_TRUE(scene.set_parent(child, first_parent));
    EXPECT_TRUE(TestUtils::Mat4Equal(
        scene.get_world_matrix(child),
        first_parent.get_component<TransformComponent>().to_matrix()
            * child_transform.to_matrix()));

    first_parent.get_component<TransformComponent>().translation.x = 3.0f;
    EXPECT_TRUE(TestUtils::Mat4Equal(
        scene.get_world_matrix(child),
        first_parent.get_component<TransformComponent>().to_matrix()
            * child_transform.to_matrix()));

    ASSERT_TRUE(scene.set_parent(child, second_parent));
    EXPECT_TRUE(TestUtils::Mat4Equal(
        child_transform.to_matrix(), local_before_reparent.to_matrix()));
    EXPECT_TRUE(TestUtils::Mat4Equal(
        scene.get_world_matrix(child),
        second_parent.get_component<TransformComponent>().to_matrix()
            * child_transform.to_matrix()));

    ASSERT_TRUE(scene.clear_parent(child));
    EXPECT_TRUE(TestUtils::Mat4Equal(
        scene.get_world_matrix(child), child_transform.to_matrix()));
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

TEST(SceneTest, TransformComponentStoresLocalTRS) {
    TransformComponent transform;
    transform.translation = Math::Vec3(1.0f, 2.0f, 3.0f);
    transform.rotation = Math::Vec3(0.0f, 1.0f, 2.0f);
    transform.scale = Math::Vec3(2.0f, 3.0f, 4.0f);

    EXPECT_TRUE(TestUtils::Vec3Equal(transform.translation, Math::Vec3(1.0f, 2.0f, 3.0f)));
    EXPECT_TRUE(TestUtils::Vec3Equal(transform.rotation, Math::Vec3(0.0f, 1.0f, 2.0f)));
    EXPECT_TRUE(TestUtils::Vec3Equal(transform.scale, Math::Vec3(2.0f, 3.0f, 4.0f)));
}

TEST(SceneTest, TransformComponentProvidesValueOperations) {
    TransformComponent transform;
    transform.translation = Math::Vec3(1.0f, 2.0f, 3.0f);
    transform.rotation = Math::Vec3(170.0f, -170.0f, 0.0f);
    transform.scale = Math::Vec3(2.0f);

    transform.rotate(Math::Vec3(20.0f, -20.0f, 360.0f));

    EXPECT_TRUE(TestUtils::Vec3Equal(
        transform.rotation, Math::Vec3(-170.0f, 170.0f, 0.0f)));
    EXPECT_TRUE(TestUtils::Mat4Equal(
        transform.to_matrix(),
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

} // namespace Comet::Tests
