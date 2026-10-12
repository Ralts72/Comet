#include "scripting/script_instance.h"
#include "physics/physics_service.h"
#include "audio/audio_service.h"
#include "asset/registry.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/script_system.h"

#include <gtest/gtest.h>
#include <limits>
#include <utility>

namespace Comet::Tests {
    namespace {
        void configure_body(Entity entity) {
            entity.add_component<RigidBodyComponent>();
            entity.add_component<ColliderComponent>();
        }

        class FailAfterCommands final: public System {
        public:
            explicit FailAfterCommands(Entity entity) : m_entity(entity) {}

            Result<void, Error> on_start(
                Scene&, RuntimeSession&, const RuntimeServices& services) override {
                if(!std::exchange(m_fail, false))
                    return Result<void, Error>::success();
                EXPECT_TRUE(services.physics->request_impulse(m_entity, {100, 0, 0}));
                EXPECT_TRUE(services.physics->request_force(m_entity, {100, 0, 0}));
                EXPECT_TRUE(services.physics->request_torque(m_entity, {0, 0, 10}));
                return Result<void, Error>::failure({"Startup failed after physics commands"});
            }

            void on_stop(
                Scene& scene, RuntimeSession&, const RuntimeServices& services) noexcept override {
                EXPECT_TRUE(services.physics->is_bound_to(scene));
            }

        private:
            Entity m_entity;
            bool m_fail = true;
        };
    }

    class PhysicsServiceTest: public testing::Test {
    protected:
        AssetRegistry assets;
        Scene scene;
        PhysicsService physics;
        SceneRuntime runtime;
        Entity body;

        void SetUp() override {
            body = scene.create_entity("Body");
            configure_body(body);
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        }

        void add_physics_system() {
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        }

        float position_x() const { return body.get_component<TransformComponent>().translation.x; }
    };

    TEST_F(PhysicsServiceTest, RuntimeDomainsKeepBodiesImpulsesAndJoltLifetimeIndependent) {
        Scene other_scene;
        PhysicsService other_physics;
        SceneRuntime other_runtime;
        const auto other_body = other_scene.create_entity_with_uuid(body.get_uuid());
        ASSERT_TRUE(other_body);
        configure_body(other_body);
        EXPECT_EQ(body.get_id(), other_body.get_id());
        ASSERT_TRUE(other_runtime.set_services({.physics = &other_physics}));
        ASSERT_TRUE(other_runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(other_runtime.add_system(std::make_unique<PhysicsSystem>(other_physics)));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(other_runtime.start(other_scene));
        EXPECT_FALSE(physics.request_impulse(other_body, {100, 0, 0}));
        EXPECT_FALSE(other_physics.request_impulse(body, {100, 0, 0}));
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FLOAT_EQ(position_x(), 0);
        ASSERT_TRUE(other_runtime.advance(0.01));
        EXPECT_FLOAT_EQ(other_body.get_component<TransformComponent>().translation.x, 0);
        EXPECT_LT(other_body.get_component<TransformComponent>().translation.y, 0);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_GT(position_x(), 0);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(other_physics.request_impulse(other_body, {100, 0, 0}));
        ASSERT_TRUE(other_runtime.advance(0.01));
        EXPECT_GT(other_body.get_component<TransformComponent>().translation.x, 0);
        ASSERT_TRUE(runtime.start(scene));
        const auto stopped_x = position_x();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(position_x(), stopped_x);
    }

    TEST_F(PhysicsServiceTest, BusyPhysicsServiceRejectsAnotherOwnerWithoutClearingItsQueue) {
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        Scene other_scene;
        AudioService other_audio(assets, AudioPlayback::Mode::Offline);
        SceneRuntime other_runtime;
        ASSERT_TRUE(other_runtime.set_services({.audio = &other_audio, .physics = &physics}));
        EXPECT_FALSE(other_runtime.start(other_scene));
        EXPECT_FALSE(other_runtime.is_active());
        EXPECT_TRUE(physics.is_bound_to(scene));
        EXPECT_FALSE(other_audio.is_bound_to(other_scene));
        EXPECT_FALSE(runtime.set_services({}));
        ASSERT_TRUE(other_runtime.set_services({}));
        ASSERT_TRUE(other_runtime.start(other_scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
    }

    TEST_F(PhysicsServiceTest, BusyAudioServiceReleasesOnlyTheNewlyAcquiredPhysicsBinding) {
        Scene audio_scene;
        AudioService audio(assets, AudioPlayback::Mode::Offline);
        SceneRuntime audio_runtime;
        ASSERT_TRUE(audio_runtime.set_services({.audio = &audio}));
        ASSERT_TRUE(audio_runtime.start(audio_scene));
        ASSERT_TRUE(runtime.set_services({.audio = &audio, .physics = &physics}));
        EXPECT_FALSE(runtime.start(scene));
        EXPECT_FALSE(physics.is_bound_to(scene));
        EXPECT_FALSE(physics.request_impulse(body, {100, 0, 0}));
        EXPECT_TRUE(audio.is_bound_to(audio_scene));
        EXPECT_TRUE(audio_runtime.is_active());
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(audio_runtime.stop());
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
    }

    TEST_F(PhysicsServiceTest, FailedStartupBeforeOrAfterWorldCreationDoesNotReplayCommands) {
        for(const bool before_world : {true, false}) {
            SCOPED_TRACE(before_world);
            ASSERT_TRUE(runtime.clear_systems());
            body.set_transform({});
            if(!before_world)
                add_physics_system();
            ASSERT_TRUE(runtime.add_system(std::make_unique<FailAfterCommands>(body)));
            if(before_world)
                add_physics_system();
            EXPECT_FALSE(runtime.start(scene));
            EXPECT_FALSE(runtime.is_active());
            EXPECT_FALSE(physics.is_bound_to(scene));
            EXPECT_FALSE(physics.request_impulse(body, {100, 0, 0}));
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_FLOAT_EQ(position_x(), 0);
            EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
            ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_GT(position_x(), 0);
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, LuaCanQueueCommandsBeforePhysicsSystemStartsAndQueryAfterStep) {
        const auto script = Script::create(R"(
            return {on_start = function()
                comet.apply_impulse(100, 0, 0)
                comet.apply_force(0, 2000, 0)
                comet.apply_torque(0, 0, 10)
            end, update = function()
                local vx, vy, vz = comet.linear_velocity()
                local wx, wy, wz = comet.angular_velocity()
                assert(vx > 0 and vy > 0 and vz == 0)
                assert(wx == 0 and wy == 0 and wz > 0)
            end}
        )");
        ASSERT_TRUE(script) << script.error().message;
        constexpr AssetHandle handle{42};
        ASSERT_TRUE(assets.register_asset(handle, script.value()));
        body.add_component<ScriptComponent>().asset = handle;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FLOAT_EQ(position_x(), 0);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
    }

    TEST_F(PhysicsServiceTest, ForceAndTorqueActOnceAndRespectPauseSingleStepAndStop) {
        body.edit_component<RigidBodyComponent>([](auto& rigid) { rigid.mass = 2; });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_force(body, {200, 0, 0}));
        ASSERT_TRUE(physics.request_torque(body, {0, 0, 10}));
        ASSERT_TRUE(runtime.advance(0.005));
        ASSERT_TRUE(physics.get_velocity(body));
        EXPECT_FLOAT_EQ(physics.get_velocity(body)->linear.x, 0);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(0.2));
        EXPECT_FLOAT_EQ(physics.get_velocity(body)->angular.z, 0);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        const auto first = physics.get_velocity(body);
        ASSERT_TRUE(first);
        EXPECT_NEAR(first->linear.x, 1, 0.002f);
        EXPECT_NEAR(first->angular.z, 0.3f, 0.002f);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(0.03));
        const auto after = physics.get_velocity(body);
        ASSERT_TRUE(after);
        EXPECT_NEAR(after->linear.x, first->linear.x, 0.005f);
        EXPECT_NEAR(after->angular.z, first->angular.z, 0.005f);
        ASSERT_TRUE(physics.request_force(body, {200, 0, 0}));
        ASSERT_TRUE(physics.request_torque(body, {0, 0, 10}));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(physics.get_velocity(body));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(physics.get_velocity(body)->linear.x, 0);
        EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
    }

    TEST_F(PhysicsServiceTest, FixedStepLuaForcesProduceTheSameMotionAtDifferentDisplayRates) {
        const auto script = Script::create(R"(
            return {fixed_update = function()
                comet.apply_force(20, 0, 0)
                comet.apply_torque(0, 1, 0)
            end}
        )");
        ASSERT_TRUE(script);
        constexpr AssetHandle handle{42};
        ASSERT_TRUE(assets.register_asset(handle, script.value()));
        body.add_component<ScriptComponent>().asset = handle;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        add_physics_system();
        std::optional<PhysicsAccess::Velocity> reference;
        Math::Vec3 reference_position(0);
        for(const auto rate : {30, 60, 120}) {
            SCOPED_TRACE(rate);
            body.set_transform({});
            ASSERT_TRUE(runtime.start(scene));
            for(int frame = 0; frame < rate; ++frame)
                ASSERT_TRUE(runtime.advance(1.0 / rate));
            const auto velocity = physics.get_velocity(body);
            ASSERT_TRUE(velocity);
            EXPECT_GT(velocity->linear.x, 19);
            EXPECT_GT(velocity->angular.y, 5);
            const auto position = body.get_component<TransformComponent>().translation;
            if(reference) {
                EXPECT_NEAR(velocity->linear.x, reference->linear.x, 1e-5f);
                EXPECT_NEAR(velocity->angular.y, reference->angular.y, 1e-5f);
                EXPECT_NEAR(position.x, reference_position.x, 1e-5f);
                EXPECT_NEAR(position.y, reference_position.y, 1e-5f);
            }
            reference = velocity;
            reference_position = position;
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, VelocityRequiresRegisteredBodyAndDoesNotFollowRecycledEntities) {
        EXPECT_FALSE(physics.get_velocity(body));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        Scene foreign_scene;
        const auto foreign = foreign_scene.create_entity_with_uuid(body.get_uuid());
        configure_body(foreign);
        EXPECT_FALSE(physics.get_velocity(foreign));
        ASSERT_TRUE(physics.request_force(body, {200, 0, 0}));
        ASSERT_TRUE(physics.request_torque(body, {0, 0, 10}));
        const auto old = body;
        const auto uuid = body.get_uuid();
        scene.destroy_entity(body);
        EXPECT_FALSE(physics.get_velocity(old));
        body = scene.create_entity_with_uuid(uuid);
        configure_body(body);
        EXPECT_FALSE(physics.get_velocity(body));
        ASSERT_TRUE(runtime.advance(0.01));
        const auto velocity = physics.get_velocity(body);
        ASSERT_TRUE(velocity);
        EXPECT_FLOAT_EQ(velocity->linear.x, 0);
        EXPECT_EQ(velocity->angular, Math::Vec3(0));
        body.remove_component<ColliderComponent>();
        EXPECT_FALSE(physics.get_velocity(body));
    }

    TEST_F(PhysicsServiceTest, StaticAndKinematicVelocityUseTheSimulatedState) {
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Kinematic; });
        auto stationary = scene.create_entity();
        configure_body(stationary);
        stationary.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.get_velocity(stationary));
        EXPECT_EQ(physics.get_velocity(stationary)->linear, Math::Vec3(0));
        EXPECT_EQ(physics.get_velocity(stationary)->angular, Math::Vec3(0));
        body.edit_transform([](auto& transform) {
            transform.translation.x = 1;
            transform.rotation.y = 10;
        });
        EXPECT_FLOAT_EQ(physics.get_velocity(body)->linear.x, 0);
        ASSERT_TRUE(runtime.advance(0.01));
        const auto moving = physics.get_velocity(body);
        ASSERT_TRUE(moving);
        EXPECT_NEAR(moving->linear.x, 100, 0.001f);
        EXPECT_NEAR(moving->angular.y, glm::radians(10.0f) / 0.01f, 0.001f);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_velocity(body)->linear, Math::Vec3(0));
        EXPECT_LT(glm::length(physics.get_velocity(body)->angular), 1e-4f);
    }

    TEST_F(PhysicsServiceTest, LuaVelocityQueriesRequireRegistrationAndBorrowedAccess) {
        ASSERT_TRUE(runtime.start(scene));
        for(const char* name : {"linear_velocity", "angular_velocity"}) {
            const auto script = Script::create(std::string("local function query() comet.") + name
                                               + "() end return {update = query, on_stop = query}");
            ASSERT_TRUE(script);
            auto instance = ScriptInstance::create(*script.value());
            ASSERT_TRUE(instance);
            const auto unregistered = instance.value()->invoke(
                ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics});
            ASSERT_FALSE(unregistered);
            EXPECT_NE(unregistered.error().message.find("registered body"), std::string::npos);
        }
        ASSERT_TRUE(runtime.stop());
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        for(const char* name : {"linear_velocity", "angular_velocity"}) {
            const auto script = Script::create(std::string("local function query() comet.") + name
                                               + "() end return {update = query, on_stop = query}");
            ASSERT_TRUE(script);
            auto instance = ScriptInstance::create(*script.value());
            ASSERT_TRUE(instance);
            ASSERT_TRUE(instance.value()->invoke(
                ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
            EXPECT_FALSE(instance.value()->invoke(
                ScriptInstance::Phase::Update, body, {}, {.scene = &scene}));
            EXPECT_FALSE(instance.value()->invoke(
                ScriptInstance::Phase::Stop, body, {}, {.scene = &scene, .physics = &physics}));
            const auto invalid = Script::create(
                std::string("return {update = function() comet.") + name + "(1) end}");
            ASSERT_TRUE(invalid);
            auto invalid_instance = ScriptInstance::create(*invalid.value());
            ASSERT_TRUE(invalid_instance);
            EXPECT_FALSE(invalid_instance.value()->invoke(
                ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
        }
    }

    TEST_F(PhysicsServiceTest, NewCommandsWakeSleepingBodies) {
        auto floor = scene.create_entity();
        configure_body(floor);
        floor.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        floor.edit_transform([](auto& transform) {
            transform.translation.y = -1;
            transform.scale = {10, 1, 10};
        });
        add_physics_system();
        for(const auto request : {&PhysicsAccess::request_force, &PhysicsAccess::request_torque}) {
            body.set_transform({});
            ASSERT_TRUE(runtime.start(scene));
            for(int step = 0; step < 300; ++step)
                ASSERT_TRUE(runtime.advance(0.01));
            ASSERT_EQ(physics.get_statistics().active_bodies, 0u);
            ASSERT_TRUE((physics.*request)(body, {0, 100, 0}));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_GT(physics.get_statistics().active_bodies, 0u);
            const auto velocity = physics.get_velocity(body);
            ASSERT_TRUE(velocity);
            EXPECT_GT(glm::length(velocity->linear) + glm::length(velocity->angular), 0.1f);
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, NewCommandsRejectBackendOverflowAndDoNotReplayAfterFailure) {
        add_physics_system();
        for(const auto request : {&PhysicsAccess::request_force, &PhysicsAccess::request_torque}) {
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE((physics.*request)(body, {std::numeric_limits<float>::max(), 0, 0}));
            const auto result = runtime.advance(0.01);
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("velocity range"), std::string::npos);
            EXPECT_FALSE(runtime.is_active());
            EXPECT_FALSE(physics.get_velocity(body));
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_FLOAT_EQ(physics.get_velocity(body)->linear.x, 0);
            EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, ScriptInvocationCannotReuseOrBorrowAnotherScenesCapability) {
        const auto script = Script::create(R"(
            return {update = function(self)
                self.calls = (self.calls or 0) + 1
                assert(self.calls == 1)
                comet.apply_impulse(100, 0, 0)
            end, on_stop = function() comet.apply_impulse(100, 0, 0) end}
        )");
        ASSERT_TRUE(script);
        auto instance = ScriptInstance::create(*script.value());
        ASSERT_TRUE(instance);
        Scene other_scene;
        SceneRuntime other_runtime;
        const auto other_body = other_scene.create_entity();
        configure_body(other_body);
        ASSERT_TRUE(other_runtime.start(other_scene));
        add_physics_system();
        const auto inactive = instance.value()->invoke(
            ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics});
        ASSERT_FALSE(inactive);
        EXPECT_NE(inactive.error().message.find("inactive"), std::string::npos);
        ASSERT_TRUE(runtime.start(scene));
        const auto foreign = instance.value()->invoke(ScriptInstance::Phase::Update, other_body, {},
            {.scene = &other_scene, .physics = &physics});
        ASSERT_FALSE(foreign);
        EXPECT_NE(foreign.error().message.find("another scene"), std::string::npos);
        ASSERT_TRUE(instance.value()->invoke(
            ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);

        const auto impulse_script =
            Script::create("return {update = function() comet.apply_impulse(100, 0, 0) end}");
        ASSERT_TRUE(impulse_script);
        auto impulse = ScriptInstance::create(*impulse_script.value());
        ASSERT_TRUE(impulse);
        ASSERT_TRUE(impulse.value()->invoke(
            ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
        const auto missing =
            impulse.value()->invoke(ScriptInstance::Phase::Update, body, {}, {.scene = &scene});
        ASSERT_FALSE(missing);
        EXPECT_NE(
            missing.error().message.find("Physics service is unavailable"), std::string::npos);
        EXPECT_FALSE(instance.value()->invoke(
            ScriptInstance::Phase::Stop, body, {}, {.scene = &scene, .physics = &physics}));
        EXPECT_TRUE(physics.is_bound_to(scene));
    }

    TEST_F(PhysicsServiceTest, MissingServiceReportsScriptErrorAndKeepsAuthoredComponents) {
        const auto script =
            Script::create("return {on_start = function() comet.apply_impulse(100, 0, 0) end}");
        ASSERT_TRUE(script);
        constexpr AssetHandle handle{42};
        ASSERT_TRUE(assets.register_asset(handle, script.value()));
        body.add_component<ScriptComponent>().asset = handle;
        ASSERT_TRUE(runtime.set_services({}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        const auto started = runtime.start(scene);
        ASSERT_FALSE(started);
        EXPECT_NE(
            started.error().message.find("Physics service is unavailable"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_TRUE(body.has_component<RigidBodyComponent>());
        EXPECT_TRUE(body.has_component<ColliderComponent>());
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
    }

    TEST_F(PhysicsServiceTest, ActiveSceneCloneDoesNotCopyVelocityOrPendingImpulse) {
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.01));
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto clone = serializer.clone(scene);
        ASSERT_TRUE(clone) << clone.error();
        const auto cloned_body = clone.value()->find_entity(body.get_uuid());
        ASSERT_TRUE(cloned_body);
        const auto copied_x = cloned_body.get_component<TransformComponent>().translation.x;
        PhysicsService cloned_physics;
        SceneRuntime cloned_runtime;
        ASSERT_TRUE(cloned_runtime.set_services({.physics = &cloned_physics}));
        ASSERT_TRUE(cloned_runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(cloned_runtime.add_system(std::make_unique<PhysicsSystem>(cloned_physics)));
        ASSERT_TRUE(cloned_runtime.start(*clone.value()));
        ASSERT_TRUE(cloned_runtime.advance(0.01));
        EXPECT_FLOAT_EQ(cloned_body.get_component<TransformComponent>().translation.x, copied_x);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), copied_x);
        ASSERT_TRUE(cloned_runtime.stop());
        ASSERT_TRUE(runtime.advance(0.01));
    }
}
