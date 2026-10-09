#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/systems/physics_system.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    namespace {
        class ContactProbe final: public System {
        public:
            Result<void, Error> update(Scene& scene, const Context&) override {
                contacts = scene.get_contact_events();
                history.insert(history.end(), contacts.begin(), contacts.end());
                return Result<void, Error>::success();
            }
            size_t count(Scene::ContactEvent::Kind kind) const {
                return std::count_if(history.begin(), history.end(),
                    [kind](const auto& event) { return event.kind == kind; });
            }
            std::vector<Scene::ContactEvent> contacts;
            std::vector<Scene::ContactEvent> history;
        };

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

    TEST(PhysicsSystemTest, PublishesContactTransitionsAfterFixedStep) {
        for(const bool trigger : {false, true}) {
            Scene scene;
            PhysicsService physics;
            auto floor = add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0});
            floor.get_component<ColliderComponent>().is_trigger = trigger;
            auto falling = add_body(scene, "Falling", BodyMotion::Dynamic, {0, 0.3f, 0});
            SceneRuntime runtime;
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
            auto probe = std::make_unique<ContactProbe>();
            auto* observed = probe.get();
            ASSERT_TRUE(runtime.add_system(std::move(probe)));
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0.01));
            ASSERT_EQ(observed->contacts.size(), 1u);
            if(trigger)
                EXPECT_EQ(observed->contacts[0].kind, Scene::ContactEvent::Kind::TriggerEnter);
            else
                EXPECT_EQ(observed->contacts[0].kind, Scene::ContactEvent::Kind::CollisionEnter);
            EXPECT_TRUE(
                observed->contacts[0].first == floor || observed->contacts[0].second == floor);
            EXPECT_TRUE(scene.get_contact_events().empty());
            ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
            falling.edit_transform(
                [](TransformComponent& transform) { transform.translation = {10, 3, 0}; });
            observed->contacts.clear();
            ASSERT_TRUE(runtime.advance(0.1));
            EXPECT_TRUE(observed->contacts.empty());
            ASSERT_TRUE(runtime.request_step());
            ASSERT_TRUE(runtime.advance(0));
            ASSERT_EQ(observed->contacts.size(), 1u);
            if(trigger)
                EXPECT_EQ(observed->contacts[0].kind, Scene::ContactEvent::Kind::TriggerExit);
            else
                EXPECT_EQ(observed->contacts[0].kind, Scene::ContactEvent::Kind::CollisionExit);
            ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_TRUE(observed->contacts.empty());
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST(PhysicsSystemTest, RecreatedColliderExitsOldContactBeforeEnteringNewKind) {
        Scene scene;
        PhysicsService physics;
        auto floor = add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0});
        add_body(scene, "Resting", BodyMotion::Dynamic, {0, 0.3f, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        ASSERT_EQ(observed->contacts.size(), 1u);
        using Kind = Scene::ContactEvent::Kind;
        ASSERT_EQ(observed->contacts.front().kind, Kind::CollisionEnter);

        for(const bool trigger : {true, false, true, false}) {
            floor.get_component<ColliderComponent>().is_trigger = trigger;
            ASSERT_TRUE(runtime.advance(0.01));
            ASSERT_EQ(observed->contacts.size(), 2u);
            EXPECT_EQ(
                observed->contacts[0].kind, trigger ? Kind::CollisionExit : Kind::TriggerExit);
            EXPECT_EQ(
                observed->contacts[1].kind, trigger ? Kind::TriggerEnter : Kind::CollisionEnter);
            EXPECT_EQ(physics.get_statistics().bodies, 2u);
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_TRUE(observed->contacts.empty());
        }
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, CollidesOnFixedStepsAndRespectsPauseStepAndStop) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -1, 0}, {10, 0.2f, 10});
        const auto falling = add_body(scene, "Falling", BodyMotion::Dynamic, {0, 2, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
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

    TEST(PhysicsSystemTest, SleepingContactPersistsUntilBodyActuallyMovesAway) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        auto resting = add_body(scene, "Resting", BodyMotion::Dynamic, {0, 2, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        for(int step = 0; step < 200; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 1u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 0u);
        EXPECT_NEAR(resting.get_component<TransformComponent>().translation.y, 0.5f, 0.03f);

        resting.edit_transform(
            [](TransformComponent& transform) { transform.translation = {20, 3, 0}; });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 1u);
        resting.edit_transform(
            [](TransformComponent& transform) { transform.translation = {0, 0.5f, 0}; });
        for(int step = 0; step < 200; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 2u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 1u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, StableSleepingBodiesKeepTransformsCleanAndStepPublishesWakeUp) {
        Scene scene;
        PhysicsService physics;
        EXPECT_EQ(physics.get_statistics().bodies, 0u);
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        const auto resting = add_body(scene, "Resting", BodyMotion::Dynamic, {0, 2, 0});
        const auto child = scene.create_entity("Visual child");
        ASSERT_TRUE(scene.set_parent(child, resting));
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(physics.get_statistics().bodies, 2u);
        EXPECT_EQ(physics.get_statistics().active_bodies, 1u);
        for(int step = 0; step < 300; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        const auto sleeping_pose = resting.get_component<TransformComponent>();
        EXPECT_EQ(physics.get_statistics().active_bodies, 0u);
        EXPECT_EQ(physics.get_statistics().pose_updates, 0u);
        scene.update_world_transforms();
        for(int step = 0; step < 30; ++step) {
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_EQ(scene.update_world_transforms(), 0u);
            EXPECT_EQ(
                resting.get_component<TransformComponent>().translation, sleeping_pose.translation);
            EXPECT_EQ(resting.get_component<TransformComponent>().rotation, sleeping_pose.rotation);
        }

        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(physics.request_impulse(resting, {0, 10, 0}));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_EQ(scene.update_world_transforms(), 0u);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(physics.get_statistics().active_bodies, 1u);
        EXPECT_EQ(physics.get_statistics().pose_updates, 1u);
        EXPECT_GT(
            resting.get_component<TransformComponent>().translation.y, sleeping_pose.translation.y);
        EXPECT_EQ(scene.update_world_transforms(), 2u);
        EXPECT_EQ(scene.update_world_transforms(), 0u);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(physics.get_statistics().bodies, 0u);
        EXPECT_EQ(physics.get_statistics().active_bodies, 0u);
        EXPECT_EQ(physics.get_statistics().pose_updates, 0u);
    }

    TEST(PhysicsSystemTest, CollisionWakesSleepingBodyAndPublishesItsPoseInTheSameStep) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        const auto resting = add_body(scene, "Resting", BodyMotion::Dynamic, {0, 0.5f, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        for(int step = 0; step < 300; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        ASSERT_EQ(physics.get_statistics().active_bodies, 0u);
        const auto sleeping_pose = resting.get_component<TransformComponent>();
        const Math::Vec3 striker_position(-0.9f, sleeping_pose.translation.y, 0);
        const auto striker = add_body(scene, "Striker", BodyMotion::Dynamic, striker_position);
        ASSERT_TRUE(physics.request_impulse(striker, {10, 0, 0}));
        ASSERT_EQ(physics.get_statistics().active_bodies, 0u);

        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().bodies, 3u);
        EXPECT_EQ(physics.get_statistics().active_bodies, 2u);
        EXPECT_EQ(physics.get_statistics().pose_updates, 2u);
        EXPECT_GT(
            resting.get_component<TransformComponent>().translation.x, sleeping_pose.translation.x);
        EXPECT_GT(striker.get_component<TransformComponent>().translation.x, striker_position.x);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, RuntimeBodyGrowthPreservesPosesImpulsesAndExistingContacts) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0});
        const auto resting = add_body(scene, "Resting", BodyMotion::Dynamic, {0, 0.3f, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 1u);

        std::vector<Entity> added;
        for(int i = 0; i < 128; ++i)
            added.push_back(add_body(scene, "Added", BodyMotion::Dynamic, {4.0f + i * 3.0f, 5, 0}));
        ASSERT_TRUE(physics.request_impulse(added.front(), {10, 0, 0}));
        ASSERT_TRUE(physics.request_impulse(added.back(), {10, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().bodies, 130u);
        EXPECT_EQ(physics.get_statistics().pose_updates, 129u);
        for(size_t i = 0; i < added.size(); ++i) {
            const auto& position = added[i].get_component<TransformComponent>().translation;
            EXPECT_LT(position.y, 5);
            if(i == 0 || i == added.size() - 1)
                EXPECT_GT(position.x, 4.0f + i * 3.0f);
            else
                EXPECT_FLOAT_EQ(position.x, 4.0f + i * 3.0f);
        }
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 1u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 0u);

        for(size_t i = 0; i < added.size(); i += 2)
            scene.destroy_entity(added[i]);
        resting.edit_transform(
            [](TransformComponent& transform) { transform.translation = {-10, 3, 0}; });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().bodies, 66u);
        EXPECT_EQ(physics.get_statistics().pose_updates, 65u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 1u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 1u);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(physics.get_statistics().bodies, 0u);
    }

    TEST(PhysicsSystemTest, PreservesAuthoredEulerUntilPhysicsRotationChanges) {
        Scene scene;
        PhysicsService physics;
        const auto falling = add_body(scene, "Falling", BodyMotion::Dynamic, {0, 10, 0});
        const Math::Vec3 authored_rotation(0, 0, 385);
        falling.edit_transform(
            [&](TransformComponent& transform) { transform.rotation = authored_rotation; });
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_LT(falling.get_component<TransformComponent>().translation.y, 10);
        EXPECT_EQ(falling.get_component<TransformComponent>().rotation, authored_rotation);

        const Math::Vec3 teleported_rotation(0, 0, 745);
        falling.edit_transform([&](TransformComponent& transform) {
            transform.translation.y = 3;
            transform.rotation = teleported_rotation;
        });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_LT(falling.get_component<TransformComponent>().translation.y, 3);
        EXPECT_EQ(falling.get_component<TransformComponent>().rotation, teleported_rotation);

        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        for(int step = 0; step < 300; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        const auto rotation = falling.get_component<TransformComponent>().rotation;
        EXPECT_GT(
            glm::length(Math::wrap_degrees(rotation) - Math::wrap_degrees(teleported_rotation)),
            1.0f);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, MovingStaticSupportWakesRestingBodyAndEndsContact) {
        Scene scene;
        PhysicsService physics;
        auto floor = add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0});
        auto resting = add_body(scene, "Resting", BodyMotion::Dynamic, {0, 0.5f, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        for(int step = 0; step < 200; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        const auto rest_y = resting.get_component<TransformComponent>().translation.y;
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 0u);
        floor.edit_transform([](TransformComponent& transform) { transform.translation.x = 10; });
        for(int step = 0; step < 10; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 1u);
        EXPECT_LT(resting.get_component<TransformComponent>().translation.y, rest_y - 0.02f);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, StaticSensorEditsRecheckSleepingOverlapAndDropDestroyedContacts) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        add_body(scene, "Resting", BodyMotion::Dynamic, {0, 0.5f, 0});
        auto sensor = add_body(scene, "Sensor", BodyMotion::Static, {10, 0.5f, 0});
        sensor.get_component<ColliderComponent>().is_trigger = true;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        for(int step = 0; step < 200; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerEnter), 0u);
        sensor.edit_transform(
            [](TransformComponent& transform) { transform.translation.x = 0.8f; });
        for(int step = 0; step < 200; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerEnter), 1u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerExit), 0u);

        sensor.get_component<ColliderComponent>().half_extents = Math::Vec3(0.1f);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerExit), 1u);
        sensor.get_component<ColliderComponent>().half_extents = Math::Vec3(0.5f);
        for(int step = 0; step < 200; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerEnter), 2u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerExit), 1u);

        scene.destroy_entity(sensor);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerExit), 1u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, NewStaticSensorDetectsAnAlreadySleepingBody) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        add_body(scene, "Resting", BodyMotion::Dynamic, {0, 0.5f, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        for(int step = 0; step < 200; ++step)
            ASSERT_TRUE(runtime.advance(0.01));
        auto sensor = add_body(scene, "Sensor", BodyMotion::Static, {0, 0.5f, 0});
        sensor.get_component<ColliderComponent>().is_trigger = true;
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerEnter), 1u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 1u);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 0u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, KinematicMotionPushesDynamicBodyWithoutPhysicsWritingItsPose) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        auto pusher = add_body(scene, "Pusher", BodyMotion::Kinematic, {-1.5f, 0.5f, 0});
        auto pushed = add_body(scene, "Pushed", BodyMotion::Dynamic, {0, 0.5f, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        for(int step = 0; step < 30; ++step) {
            pusher.edit_transform(
                [](TransformComponent& transform) { transform.translation.x += 0.04f; });
            const auto target = pusher.get_component<TransformComponent>();
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_EQ(pusher.get_component<TransformComponent>().translation, target.translation);
        }
        EXPECT_GT(pushed.get_component<TransformComponent>().translation.x, 0.4f);
        EXPECT_FLOAT_EQ(pusher.get_component<TransformComponent>().translation.y, 0.5f);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, KinematicStopsAtUnchangedTargetAndExitsStaticTriggerWhenMovedAway) {
        Scene scene;
        PhysicsService physics;
        auto sensor = add_body(scene, "Sensor", BodyMotion::Static, {0, 0, 0});
        sensor.get_component<ColliderComponent>().is_trigger = true;
        auto mover = add_body(scene, "Mover", BodyMotion::Kinematic, {-2, 0, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        int entries = 0;
        for(int step = 0; step < 8; ++step) {
            mover.edit_transform(
                [](TransformComponent& transform) { transform.translation.x += 0.2f; });
            ASSERT_TRUE(runtime.advance(0.01));
            for(const auto& contact : observed->contacts) {
                EXPECT_EQ(contact.kind, Scene::ContactEvent::Kind::TriggerEnter);
                ++entries;
            }
        }
        ASSERT_EQ(entries, 1);
        for(int step = 0; step < 20; ++step) {
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_TRUE(observed->contacts.empty()) << "Stopped at step " << step;
        }
        mover.edit_transform([](TransformComponent& transform) { transform.translation.x = -2; });
        // 接触检测观察固定步开始时的姿态，下一步确认已经分离。
        ASSERT_TRUE(runtime.advance(0.02));
        ASSERT_EQ(observed->contacts.size(), 1u);
        EXPECT_EQ(observed->contacts[0].kind, Scene::ContactEvent::Kind::TriggerExit);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, KinematicRotationMovesColliderAndClearsAngularVelocityAtTarget) {
        Scene scene;
        PhysicsService physics;
        auto sensor = add_body(scene, "Sensor", BodyMotion::Static, {0, 0, 1.5f});
        sensor.get_component<ColliderComponent>().is_trigger = true;
        auto rod = add_body(scene, "Rod", BodyMotion::Kinematic, {0, 0, 0});
        rod.get_component<ColliderComponent>().half_extents = {2, 0.2f, 0.2f};
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        ASSERT_TRUE(observed->contacts.empty());
        rod.edit_transform([](TransformComponent& transform) { transform.rotation.y = 90; });
        ASSERT_TRUE(runtime.advance(0.02));
        ASSERT_EQ(observed->contacts.size(), 1u);
        EXPECT_EQ(observed->contacts[0].kind, Scene::ContactEvent::Kind::TriggerEnter);
        for(int step = 0; step < 20; ++step) {
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_TRUE(observed->contacts.empty());
            EXPECT_FLOAT_EQ(rod.get_component<TransformComponent>().rotation.y, 90);
        }
        rod.edit_transform([](TransformComponent& transform) { transform.rotation.y = 180; });
        ASSERT_TRUE(runtime.advance(0.02));
        ASSERT_EQ(observed->contacts.size(), 1u);
        EXPECT_EQ(observed->contacts[0].kind, Scene::ContactEvent::Kind::TriggerExit);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, SwitchingMotionRecreatesBodyWithTheNewPoseOwner) {
        Scene scene;
        PhysicsService physics;
        auto body = add_body(scene, "Body", BodyMotion::Kinematic, {0, 2, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        EXPECT_FLOAT_EQ(body.get_component<TransformComponent>().translation.y, 2);
        body.get_component<RigidBodyComponent>().motion = BodyMotion::Dynamic;
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        EXPECT_LT(body.get_component<TransformComponent>().translation.y, 2);
        body.get_component<RigidBodyComponent>().motion = BodyMotion::Kinematic;
        const auto frozen = body.get_component<TransformComponent>().translation;
        for(int step = 0; step < 10; ++step)
            ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        EXPECT_EQ(body.get_component<TransformComponent>().translation, frozen);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, RejectsParentedBodiesAndCleansPartialStart) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -1, 0});
        auto parent = scene.create_entity("Parent");
        auto child = add_body(scene, "Child", BodyMotion::Dynamic, {0, 2, 0});
        ASSERT_TRUE(scene.set_parent(child, parent));
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        const auto started = runtime.start(scene);
        ASSERT_FALSE(started);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_NE(started.error().message.find("Parented"), std::string::npos);
        ASSERT_TRUE(scene.clear_parent(child));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, RejectsSphereRadiusOverflowAndUnderflowAndCleansRuntime) {
        for(const auto value :
            {std::numeric_limits<float>::max(), std::numeric_limits<float>::min()}) {
            SCOPED_TRACE(value);
            Scene scene;
            PhysicsService physics;
            add_body(scene, "Floor", BodyMotion::Static, {0, -1, 0});
            auto ball = add_body(scene, "Ball", BodyMotion::Dynamic, {0, 2, 0}, Math::Vec3(value));
            auto& collider = ball.get_component<ColliderComponent>();
            collider.shape = ColliderShape::Sphere;
            collider.radius = value;
            SceneRuntime runtime;
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));

            const auto started = runtime.start(scene);
            ASSERT_FALSE(started);
            EXPECT_NE(started.error().message.find("scaled radius"), std::string::npos);
            EXPECT_FALSE(runtime.is_active());
            EXPECT_FALSE(runtime.get_session().request_restart());

            collider.radius = 0.5f;
            ball.edit_transform(
                [](TransformComponent& transform) { transform.scale = Math::Vec3(1); });
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(1.0 / 60.0));
            EXPECT_LT(ball.get_component<TransformComponent>().translation.y, 2);

            collider.radius = value;
            ball.edit_transform(
                [value](TransformComponent& transform) { transform.scale = Math::Vec3(value); });
            const auto advanced = runtime.advance(1.0 / 60.0);
            ASSERT_FALSE(advanced);
            EXPECT_NE(advanced.error().message.find("scaled radius"), std::string::npos);
            EXPECT_FALSE(runtime.is_active());
            EXPECT_FALSE(runtime.get_session().request_restart());

            collider.radius = 0.5f;
            ball.edit_transform(
                [](TransformComponent& transform) { transform.scale = Math::Vec3(1); });
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(1.0 / 60.0));
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST(PhysicsSystemTest, AppliesExternalPoseAtNextFixedStepAndRemovesDeletedBody) {
        Scene scene;
        PhysicsService physics;
        auto falling = add_body(scene, "Falling", BodyMotion::Dynamic, {0, 2, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
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

    TEST(PhysicsSystemTest, QueuedBodyRemovalExitsTriggerOnceWithoutDestroyingTheEntity) {
        for(const bool remove_sensor : {false, true}) {
            SCOPED_TRACE(remove_sensor);
            Scene scene;
            PhysicsService physics;
            auto sensor = add_body(scene, "Sensor", BodyMotion::Static, {0, 0, 0});
            sensor.get_component<ColliderComponent>().is_trigger = true;
            const auto falling = add_body(scene, "Falling", BodyMotion::Dynamic, {0, 0.3f, 0});
            auto removed = remove_sensor ? sensor : falling;
            removed.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{12});
            const auto uuid = removed.get_uuid();
            const auto id = removed.get_id();
            SceneRuntime runtime;
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
            auto probe = std::make_unique<ContactProbe>();
            auto* observed = probe.get();
            ASSERT_TRUE(runtime.add_system(std::move(probe)));
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0.01));
            ASSERT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerEnter), 1u);
            ASSERT_TRUE(scene.request_remove_rigid_body(removed));
            ASSERT_TRUE(scene.request_remove_rigid_body(removed));
            EXPECT_TRUE(removed.has_component<RigidBodyComponent>());

            ASSERT_TRUE(runtime.advance(0));
            ASSERT_TRUE(removed);
            EXPECT_FALSE(removed.has_component<RigidBodyComponent>());
            EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerExit), 0u);
            const auto frozen = removed.get_component<TransformComponent>().translation;
            ASSERT_TRUE(runtime.advance(0.01));
            ASSERT_EQ(observed->contacts.size(), 1u);
            EXPECT_EQ(observed->contacts[0].kind, Scene::ContactEvent::Kind::TriggerExit);
            EXPECT_TRUE(
                observed->contacts[0].first == removed || observed->contacts[0].second == removed);
            ASSERT_TRUE(removed);
            EXPECT_EQ(removed.get_component<TransformComponent>().translation, frozen);
            EXPECT_EQ(removed.get_uuid(), uuid);
            EXPECT_EQ(removed.get_id(), id);
            EXPECT_EQ(scene.entity_count(), 2u);
            ASSERT_TRUE(removed.has_component<ColliderComponent>());
            EXPECT_EQ(removed.get_component<ColliderComponent>().is_trigger, remove_sensor);
            ASSERT_TRUE(removed.has_component<MeshRendererComponent>());
            EXPECT_EQ(removed.get_component<MeshRendererComponent>().mesh, AssetHandle{11});
            EXPECT_EQ(removed.get_component<MeshRendererComponent>().material, AssetHandle{12});
            EXPECT_EQ(
                removed.get_component<NameComponent>().name, remove_sensor ? "Sensor" : "Falling");

            ASSERT_TRUE(scene.request_remove_rigid_body(removed));
            removed.edit_transform(
                [](TransformComponent& transform) { transform.translation = {20, 5, 0}; });
            for(int step = 0; step < 5; ++step)
                ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerEnter), 1u);
            EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::TriggerExit), 1u);
            EXPECT_EQ(
                removed.get_component<TransformComponent>().translation, Math::Vec3(20, 5, 0));
            EXPECT_FALSE(removed.has_component<RigidBodyComponent>());
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST(PhysicsSystemTest, ComponentsSurviveSceneCloneWithoutRuntimeState) {
        Scene scene;
        auto entity = add_body(scene, "Ball", BodyMotion::Dynamic, {1, 2, 3});
        entity.get_component<RigidBodyComponent>().mass = 7;
        auto& collider = entity.get_component<ColliderComponent>();
        collider.shape = ColliderShape::Sphere;
        collider.radius = 0.25f;
        collider.is_trigger = true;
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto clone = serializer.clone(scene);
        ASSERT_TRUE(clone) << clone.error();
        auto copied = clone.value()->find_entity(entity.get_uuid());
        ASSERT_TRUE(copied);
        EXPECT_EQ(copied.get_component<RigidBodyComponent>().motion, BodyMotion::Dynamic);
        EXPECT_FLOAT_EQ(copied.get_component<RigidBodyComponent>().mass, 7);
        EXPECT_EQ(copied.get_component<ColliderComponent>().shape, ColliderShape::Sphere);
        EXPECT_FLOAT_EQ(copied.get_component<ColliderComponent>().radius, 0.25f);
        EXPECT_TRUE(copied.get_component<ColliderComponent>().is_trigger);
    }

    TEST(PhysicsSystemTest, ImpulseRequestsValidateTargetsAndBoundThePendingQueue) {
        Scene scene;
        PhysicsService physics;
        const auto dynamic = add_body(scene, "Dynamic", BodyMotion::Dynamic, {0, 10, 0});
        const auto stationary = add_body(scene, "Static", BodyMotion::Static, {10, 0, 0});
        const auto kinematic = add_body(scene, "Kinematic", BodyMotion::Kinematic, {20, 0, 0});
        const auto no_body = scene.create_entity("NoBody");
        Scene other_scene;
        const auto foreign = add_body(other_scene, "Foreign", BodyMotion::Dynamic, {0, 10, 0});
        EXPECT_FALSE(physics.request_impulse(dynamic, {1, 0, 0}));
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(physics.request_impulse({}, {1, 0, 0}));
        EXPECT_FALSE(physics.request_impulse(foreign, {1, 0, 0}));
        EXPECT_FALSE(physics.request_impulse(stationary, {1, 0, 0}));
        EXPECT_FALSE(physics.request_impulse(kinematic, {1, 0, 0}));
        EXPECT_FALSE(physics.request_impulse(no_body, {1, 0, 0}));
        EXPECT_FALSE(
            physics.request_impulse(dynamic, {std::numeric_limits<float>::infinity(), 0, 0}));
        EXPECT_FALSE(
            physics.request_impulse(dynamic, {0, std::numeric_limits<float>::quiet_NaN(), 0}));
        for(int i = 0; i < 128; ++i)
            ASSERT_TRUE(physics.request_impulse(dynamic, {0, 0, 0}));
        EXPECT_FALSE(physics.request_impulse(dynamic, {1, 0, 0}));
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        EXPECT_TRUE(physics.request_impulse(dynamic, {1, 0, 0}));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(physics.request_impulse(dynamic, {1, 0, 0}));
    }

    TEST(PhysicsSystemTest, ImpulseRunsOnceAtFixedBoundaryAndStopDiscardsPendingRequests) {
        Scene scene;
        PhysicsService physics;
        auto body = add_body(scene, "Body", BodyMotion::Dynamic, {0, 10, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(body, {10, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.005));
        EXPECT_FLOAT_EQ(body.get_component<TransformComponent>().translation.x, 0);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FLOAT_EQ(body.get_component<TransformComponent>().translation.x, 0);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_NEAR(body.get_component<TransformComponent>().translation.x, 0.1f, 0.002f);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(0.03));
        EXPECT_NEAR(body.get_component<TransformComponent>().translation.x, 0.4f, 0.005f);

        ASSERT_TRUE(physics.request_impulse(body, {10, 0, 0}));
        ASSERT_TRUE(runtime.stop());
        body.edit_transform(
            [](TransformComponent& transform) { transform.translation = {0, 10, 0}; });
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.03));
        EXPECT_FLOAT_EQ(body.get_component<TransformComponent>().translation.x, 0);
        EXPECT_LT(body.get_component<TransformComponent>().translation.y, 10);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, ImpulsesKeepSubmissionOrderWithinOneFixedStep) {
        Scene scene;
        PhysicsService physics;
        const auto first = add_body(scene, "First", BodyMotion::Dynamic, {0, 10, -10});
        const auto second = add_body(scene, "Second", BodyMotion::Dynamic, {0, 10, 10});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(first, {1000000, 0, 0}));
        ASSERT_TRUE(physics.request_impulse(first, {-1000000, 0, 0}));
        ASSERT_TRUE(physics.request_impulse(second, {-1000000, 0, 0}));
        ASSERT_TRUE(physics.request_impulse(second, {1000000, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_LT(first.get_component<TransformComponent>().translation.x, -1);
        EXPECT_GT(second.get_component<TransformComponent>().translation.x, 1);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, ImpulseCannotReachRecreatedOrNoLongerDynamicTargets) {
        Scene scene;
        PhysicsService physics;
        const auto original = add_body(scene, "Original", BodyMotion::Dynamic, {0, 10, 0});
        const auto uuid = original.get_uuid();
        const auto original_id = original.get_id();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(original, {10, 0, 0}));
        scene.destroy_entity(original);
        auto replacement = scene.create_entity_with_uuid(uuid, "Replacement");
        ASSERT_TRUE(replacement);
        ASSERT_NE(replacement.get_id(), original_id);
        replacement.edit_transform(
            [](TransformComponent& transform) { transform.translation = {0, 10, 0}; });
        replacement.add_component<RigidBodyComponent>();
        replacement.add_component<ColliderComponent>();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(replacement.get_component<TransformComponent>().translation.x, 0);
        for(const auto motion : {BodyMotion::Static, BodyMotion::Kinematic}) {
            replacement.get_component<RigidBodyComponent>().motion = BodyMotion::Dynamic;
            ASSERT_TRUE(physics.request_impulse(replacement, {10, 0, 0}));
            replacement.get_component<RigidBodyComponent>().motion = motion;
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_FLOAT_EQ(replacement.get_component<TransformComponent>().translation.x, 0);
        }
        replacement.get_component<RigidBodyComponent>().motion = BodyMotion::Dynamic;
        ASSERT_TRUE(physics.request_impulse(replacement, {10, 0, 0}));
        replacement.remove_component<RigidBodyComponent>();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(replacement.get_component<TransformComponent>().translation.x, 0);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, ImpulseWakesRestingBodyAndRechecksItsContact) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        const auto resting = add_body(scene, "Resting", BodyMotion::Dynamic, {0, 2, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        for(int i = 0; i < 250; ++i)
            ASSERT_TRUE(runtime.advance(0.01));
        ASSERT_NEAR(resting.get_component<TransformComponent>().translation.y, 0.5f, 0.03f);
        ASSERT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 1);
        ASSERT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 0);
        ASSERT_TRUE(physics.request_impulse(resting, {0, 10, 0}));
        for(int i = 0; i < 5; ++i)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(resting.get_component<TransformComponent>().translation.y, 0.9f);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 1);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, ExcessiveFiniteImpulseFailsBeforeWritingInvalidVelocity) {
        Scene scene;
        PhysicsService physics;
        const auto body = add_body(scene, "Body", BodyMotion::Dynamic, {0, 10, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(body, {std::numeric_limits<float>::max(), 0, 0}));
        const auto result = runtime.advance(0.01);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("velocity range"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_TRUE(Math::is_finite(body.get_component<TransformComponent>().translation));
        EXPECT_FALSE(physics.request_impulse(body, {1, 0, 0}));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(body.get_component<TransformComponent>().translation.x, 0);
        EXPECT_LT(body.get_component<TransformComponent>().translation.y, 10);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, ConfiguredMassControlsImpulseResponseIndependentlyOfShapeSize) {
        for(const auto shape : {ColliderShape::Box, ColliderShape::Sphere}) {
            Scene scene;
            PhysicsService physics;
            auto light = add_body(scene, "Light", BodyMotion::Dynamic, {0, 10, -10});
            auto heavy = add_body(scene, "Heavy", BodyMotion::Dynamic, {0, 10, 0});
            auto large = add_body(scene, "Large", BodyMotion::Dynamic, {0, 10, 10}, Math::Vec3(2));
            heavy.get_component<RigidBodyComponent>().mass = 2;
            for(auto entity : {light, heavy, large})
                entity.get_component<ColliderComponent>().shape = shape;
            SceneRuntime runtime;
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
            ASSERT_TRUE(runtime.start(scene));
            for(const auto entity : {light, heavy, large})
                ASSERT_TRUE(physics.request_impulse(entity, {10, 0, 0}));
            ASSERT_TRUE(runtime.advance(0.01));
            const auto distance = light.get_component<TransformComponent>().translation.x;
            EXPECT_NEAR(distance, 0.1f, 0.002f);
            EXPECT_NEAR(
                heavy.get_component<TransformComponent>().translation.x, distance / 2, 1e-5f);
            EXPECT_NEAR(large.get_component<TransformComponent>().translation.x, distance, 1e-5f);
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST(PhysicsSystemTest, MassChangesAtFixedBoundaryWithoutResettingVelocity) {
        Scene scene;
        PhysicsService physics;
        auto body = add_body(scene, "Body", BodyMotion::Dynamic, {0, 10, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(body, {10, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.01));
        const auto before_pause = body.get_component<TransformComponent>().translation.x;
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        body.get_component<RigidBodyComponent>().mass = 2;
        ASSERT_TRUE(physics.request_impulse(body, {10, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FLOAT_EQ(body.get_component<TransformComponent>().translation.x, before_pause);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        const auto after_step = body.get_component<TransformComponent>().translation.x;
        EXPECT_NEAR(after_step - before_pause, 0.15f, 0.002f);
        body.get_component<RigidBodyComponent>().mass = 4;
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_NEAR(body.get_component<TransformComponent>().translation.x - after_step,
            after_step - before_pause, 0.002f);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, ChangingRestingBodyMassPreservesContactIdentity) {
        Scene scene;
        PhysicsService physics;
        add_body(scene, "Floor", BodyMotion::Static, {0, -0.5f, 0}, {10, 1, 10});
        auto resting = add_body(scene, "Resting", BodyMotion::Dynamic, {0, 2, 0});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto probe = std::make_unique<ContactProbe>();
        auto* observed = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        for(int i = 0; i < 250; ++i)
            ASSERT_TRUE(runtime.advance(0.01));
        ASSERT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 1);
        resting.get_component<RigidBodyComponent>().mass = 2;
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionEnter), 1);
        EXPECT_EQ(observed->count(Scene::ContactEvent::Kind::CollisionExit), 0);
        EXPECT_NEAR(resting.get_component<TransformComponent>().translation.y, 0.5f, 0.03f);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, InvalidMassFailsStartOrFixedSynchronizationAndCleansRuntime) {
        for(const auto motion : {BodyMotion::Static, BodyMotion::Kinematic, BodyMotion::Dynamic}) {
            for(const auto mass : {0.0f, -1.0f, RigidBodyComponent::MIN_MASS / 2,
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()}) {
                SCOPED_TRACE(mass);
                Scene scene;
                PhysicsService physics;
                auto body = add_body(scene, "Body", motion, {0, 10, 0});
                auto& rigid = body.get_component<RigidBodyComponent>();
                rigid.mass = mass;
                SceneRuntime runtime;
                ASSERT_TRUE(runtime.set_services({.physics = &physics}));
                ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
                auto result = runtime.start(scene);
                ASSERT_FALSE(result);
                EXPECT_NE(result.error().message.find("mass"), std::string::npos);
                EXPECT_FALSE(runtime.is_active());
                rigid.mass = RigidBodyComponent::MIN_MASS;
                ASSERT_TRUE(runtime.start(scene));
                ASSERT_TRUE(runtime.advance(1.0 / 60.0));
                rigid.mass = mass;
                result = runtime.advance(1.0 / 60.0);
                ASSERT_FALSE(result);
                EXPECT_NE(result.error().message.find("mass"), std::string::npos);
                EXPECT_FALSE(runtime.is_active());
                EXPECT_FALSE(runtime.get_session().request_restart());
                rigid.mass = 1;
                ASSERT_TRUE(runtime.start(scene));
                ASSERT_TRUE(runtime.stop());
            }
        }
    }

    TEST(PhysicsSystemTest, FiniteMassWithUnrepresentableInertiaFailsBeforeJoltConsumesIt) {
        Scene scene;
        PhysicsService physics;
        auto body = add_body(scene, "Body", BodyMotion::Dynamic, {0, 10, 0}, Math::Vec3(100));
        auto& rigid = body.get_component<RigidBodyComponent>();
        rigid.mass = 1e36f;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto result = runtime.start(scene);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("inertia"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        rigid.mass = 1;
        ASSERT_TRUE(runtime.start(scene));
        rigid.mass = 1e36f;
        result = runtime.advance(1.0 / 60.0);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("inertia"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_TRUE(Math::is_finite(body.get_component<TransformComponent>().translation));
        rigid.mass = 1;
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsSystemTest, DemoSceneLoadsAndStartsItsPhysicsBodies) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto loaded = serializer.load(
            std::string(COMET_SAMPLE_PROJECT_DIRECTORY) + "/assets/scenes/default.scene");
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value()->component_count<RigidBodyComponent>(), 5u);
        EXPECT_EQ(loaded.value()->component_count<ColliderComponent>(), 5u);
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(*loaded.value()));
        ASSERT_TRUE(runtime.advance(1.0 / 60.0));
        ASSERT_TRUE(runtime.stop());
    }
}
