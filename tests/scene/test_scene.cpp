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
    EXPECT_TRUE(entity.has_component<NameComponent>());
    EXPECT_TRUE(entity.has_component<TransformComponent>());
    EXPECT_EQ(entity.get_component<NameComponent>().name, "Camera");
    EXPECT_NE(entity.get_id(), INVALID_ENTITY_ID);
    EXPECT_EQ(scene.entity_count(), 1u);
}

TEST(SceneTest, InvalidEntityHasInvalidId) {
    Entity entity;

    EXPECT_FALSE(entity);
    EXPECT_EQ(entity.get_id(), INVALID_ENTITY_ID);
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
