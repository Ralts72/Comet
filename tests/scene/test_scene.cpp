#include <gtest/gtest.h>
#include "scene/components.h"
#include "scene/scene.h"
#include "../test_utils.h"

namespace Comet::Tests {

TEST(SceneTest, CreateEntityAddsDefaultComponents) {
    Scene scene;

    Entity entity = scene.create_entity("Camera");

    ASSERT_TRUE(entity.is_valid());
    EXPECT_TRUE(entity.has_component<IDComponent>());
    EXPECT_TRUE(entity.has_component<NameComponent>());
    EXPECT_TRUE(entity.has_component<TransformComponent>());
    EXPECT_TRUE(entity.has_component<RelationshipComponent>());
    EXPECT_EQ(entity.get_name(), "Camera");
    EXPECT_NE(entity.get_id(), 0u);
}

TEST(SceneTest, DestroyEntityInvalidatesHandle) {
    Scene scene;
    Entity entity = scene.create_entity("Temporary");
    const EntityId id = entity.get_id();

    scene.destroy_entity(entity);

    EXPECT_FALSE(entity.is_valid());
    EXPECT_FALSE(scene.find_entity_by_id(id).is_valid());
}

TEST(SceneTest, EntityManagesCustomComponents) {
    struct HealthComponent {
        explicit HealthComponent(const int initial_value) : value(initial_value) {}

        int value = 0;
    };

    Scene scene;
    Entity entity = scene.create_entity("Player");

    auto& health = entity.add_component<HealthComponent>(100);

    EXPECT_TRUE(entity.has_component<HealthComponent>());
    EXPECT_EQ(health.value, 100);
    EXPECT_EQ(entity.get_component<HealthComponent>().value, 100);

    entity.remove_component<HealthComponent>();

    EXPECT_FALSE(entity.has_component<HealthComponent>());
}

TEST(SceneTest, TransformBuildsLocalMatrix) {
    TransformComponent transform;
    transform.translation = Math::Vec3(1.0f, 2.0f, 3.0f);
    transform.scale = Math::Vec3(2.0f, 3.0f, 4.0f);

    const Math::Mat4 matrix = transform.get_local_matrix();
    const Math::Vec4 point = matrix * Math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);

    EXPECT_TRUE(TestUtils::Vec3Equal(Math::Vec3(point), Math::Vec3(3.0f, 5.0f, 7.0f)));
}

TEST(SceneTest, ParentTransformContributesToChildWorldTransform) {
    Scene scene;
    Entity parent = scene.create_entity("Parent");
    Entity child = scene.create_entity("Child");

    parent.get_component<TransformComponent>().translation = Math::Vec3(10.0f, 0.0f, 0.0f);
    child.get_component<TransformComponent>().translation = Math::Vec3(0.0f, 5.0f, 0.0f);

    scene.set_parent(child, parent);
    scene.update_transforms();

    const auto& child_transform = child.get_component<TransformComponent>();
    const Math::Vec4 world_origin = child_transform.world_matrix * Math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);

    EXPECT_TRUE(TestUtils::Vec3Equal(Math::Vec3(world_origin), Math::Vec3(10.0f, 5.0f, 0.0f)));
    ASSERT_TRUE(scene.get_parent(child).is_valid());
    EXPECT_EQ(scene.get_parent(child).get_id(), parent.get_id());
    ASSERT_EQ(scene.get_children(parent).size(), 1u);
    EXPECT_EQ(scene.get_children(parent).front().get_id(), child.get_id());
}

TEST(SceneTest, BuiltInRenderComponentsCanBeAttached) {
    Scene scene;
    Entity entity = scene.create_entity("Renderable");

    auto& mesh_renderer = entity.add_component<MeshRendererComponent>("cube", "default");
    auto& camera = entity.add_component<CameraComponent>();
    auto& light = entity.add_component<LightComponent>(LightType::Point);

    EXPECT_EQ(mesh_renderer.mesh_name, "cube");
    EXPECT_EQ(mesh_renderer.material_name, "default");
    EXPECT_FLOAT_EQ(camera.field_of_view, 45.0f);
    EXPECT_FLOAT_EQ(camera.near_clip, 0.1f);
    EXPECT_FLOAT_EQ(camera.far_clip, 100.0f);
    EXPECT_TRUE(camera.primary);
    EXPECT_EQ(light.type, LightType::Point);
    EXPECT_FLOAT_EQ(light.intensity, 1.0f);
    EXPECT_TRUE(TestUtils::Vec3Equal(light.color, Math::Vec3(1.0f)));
}

} // namespace Comet::Tests
