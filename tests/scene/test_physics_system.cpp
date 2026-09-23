#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/systems/physics_system.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        Entity add_body(Scene& scene, const char* name, BodyMotion motion,
            const Math::Vec3 position, const Math::Vec3 scale = Math::Vec3(1.0f)) {
            auto entity = scene.create_entity(name);
            auto transform = entity.get_component<TransformComponent>();
            transform.translation = position;
            transform.scale = scale;
            entity.set_transform(transform);
            entity.add_component<RigidBodyComponent>().motion = motion;
            entity.add_component<ColliderComponent>();
            return entity;
        }
    }

    TEST(PhysicsSystemTest, CollidesOnFixedStepsAndRespectsPauseStepAndStop) {
        Scene scene;
        add_body(scene, "Floor", BodyMotion::Static, {0, -1, 0}, {10, 0.2f, 10});
        const auto falling = add_body(scene, "Falling", BodyMotion::Dynamic, {0, 2, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(falling.get_component<TransformComponent>().translation.y, 2);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_FLOAT_EQ(falling.get_component<TransformComponent>().translation.y, 2);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_LT(falling.get_component<TransformComponent>().translation.y, 2);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        for(int i = 0; i < 180; ++i)
            ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        EXPECT_NEAR(falling.get_component<TransformComponent>().translation.y, -0.4f, 0.08f);
        ASSERT_TRUE(runtime.stop());
        falling.edit_transform(
            [](TransformComponent& transform) { transform.translation = {0, 2, 0}; });
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        EXPECT_LT(falling.get_component<TransformComponent>().translation.y, 2);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, RejectsParentedBodiesAndCleansPartialStart) {
        Scene scene;
        add_body(scene, "Floor", BodyMotion::Static, {0, -1, 0});
        auto parent = scene.create_entity("Parent");
        auto child = add_body(scene, "Child", BodyMotion::Dynamic, {0, 2, 0});
        ASSERT_TRUE(scene.set_parent(child, parent));
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        const auto started = runtime.start(scene);
        ASSERT_FALSE(started);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_NE(started.error().message.find("Parented"), std::string::npos);
        ASSERT_TRUE(scene.clear_parent(child));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, AppliesExternalPoseAtNextFixedStepAndRemovesDeletedBody) {
        Scene scene;
        auto falling = add_body(scene, "Falling", BodyMotion::Dynamic, {0, 2, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        falling.edit_transform(
            [](TransformComponent& transform) { transform.translation = {0, 5, 0}; });
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        EXPECT_GT(falling.get_component<TransformComponent>().translation.y, 4.9f);
        falling.remove_component<RigidBodyComponent>();
        const auto frozen = falling.get_component<TransformComponent>().translation.y;
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        EXPECT_FLOAT_EQ(falling.get_component<TransformComponent>().translation.y, frozen);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, ComponentsSurviveSceneCloneWithoutRuntimeState) {
        Scene scene;
        auto entity = add_body(scene, "Ball", BodyMotion::Dynamic, {1, 2, 3});
        auto& collider = entity.get_component<ColliderComponent>();
        collider.shape = ColliderShape::Sphere;
        collider.radius = 0.25f;
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto clone = serializer.clone(scene);
        ASSERT_TRUE(clone) << clone.error();
        auto copied = clone.value()->find_entity(entity.get_uuid());
        ASSERT_TRUE(copied);
        EXPECT_EQ(copied.get_component<RigidBodyComponent>().motion, BodyMotion::Dynamic);
        EXPECT_EQ(copied.get_component<ColliderComponent>().shape, ColliderShape::Sphere);
        EXPECT_FLOAT_EQ(copied.get_component<ColliderComponent>().radius, 0.25f);
    }

    TEST(PhysicsSystemTest, DemoSceneLoadsAndStartsItsPhysicsBodies) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto loaded = serializer.load(
            std::string(COMET_SAMPLE_PROJECT_DIRECTORY) + "/assets/scenes/default.scene");
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value()->component_count<RigidBodyComponent>(), 2u);
        EXPECT_EQ(loaded.value()->component_count<ColliderComponent>(), 2u);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        ASSERT_TRUE(runtime.start(*loaded.value()));
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        ASSERT_TRUE(runtime.stop());
    }
}
