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
#include <utility>

namespace Comet::Tests {
    namespace {
        void configure_body(Entity entity) {
            entity.add_component<RigidBodyComponent>();
            entity.add_component<ColliderComponent>();
        }

        class FailAfterImpulse final: public System {
        public:
            explicit FailAfterImpulse(Entity entity) : m_entity(entity) {}

            Result<void, Error> on_start(
                Scene&, RuntimeSession&, const RuntimeServices& services) override {
                if(!std::exchange(m_fail, false))
                    return Result<void, Error>::success();
                EXPECT_TRUE(services.physics->request_impulse(m_entity, {100, 0, 0}));
                return Result<void, Error>::failure({"Startup failed after impulse"});
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

    TEST_F(PhysicsServiceTest, FailedStartupBeforeOrAfterWorldCreationDoesNotReplayImpulses) {
        for(const bool before_world : {true, false}) {
            SCOPED_TRACE(before_world);
            ASSERT_TRUE(runtime.clear_systems());
            body.set_transform({});
            if(!before_world)
                add_physics_system();
            ASSERT_TRUE(runtime.add_system(std::make_unique<FailAfterImpulse>(body)));
            if(before_world)
                add_physics_system();
            EXPECT_FALSE(runtime.start(scene));
            EXPECT_FALSE(runtime.is_active());
            EXPECT_FALSE(physics.is_bound_to(scene));
            EXPECT_FALSE(physics.request_impulse(body, {100, 0, 0}));
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_FLOAT_EQ(position_x(), 0);
            ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_GT(position_x(), 0);
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, LuaCanQueueAnImpulseBeforePhysicsSystemStarts) {
        const auto script =
            Script::create("return {on_start = function() comet.apply_impulse(100, 0, 0) end}");
        ASSERT_TRUE(script) << script.error().message;
        constexpr AssetHandle handle{42};
        ASSERT_TRUE(assets.register_asset(handle, script.value()));
        body.add_component<ScriptComponent>().asset = handle;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(ScriptAssets{assets})));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FLOAT_EQ(position_x(), 0);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
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
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        Scene other_scene;
        SceneRuntime other_runtime;
        const auto other_body = other_scene.create_entity();
        configure_body(other_body);
        ASSERT_TRUE(other_runtime.start(other_scene));
        add_physics_system();
        const auto inactive = instance.value()->invoke(
            Script::Phase::Update, body, {}, {.scene = &scene, .physics = &physics});
        ASSERT_FALSE(inactive);
        EXPECT_NE(inactive.error().message.find("inactive"), std::string::npos);
        ASSERT_TRUE(runtime.start(scene));
        const auto foreign = instance.value()->invoke(
            Script::Phase::Update, other_body, {}, {.scene = &other_scene, .physics = &physics});
        ASSERT_FALSE(foreign);
        EXPECT_NE(foreign.error().message.find("another scene"), std::string::npos);
        ASSERT_TRUE(instance.value()->invoke(
            Script::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);

        const auto impulse_script =
            Script::create("return {update = function() comet.apply_impulse(100, 0, 0) end}");
        ASSERT_TRUE(impulse_script);
        auto impulse = impulse_script.value()->instantiate();
        ASSERT_TRUE(impulse);
        ASSERT_TRUE(impulse.value()->invoke(
            Script::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
        const auto missing =
            impulse.value()->invoke(Script::Phase::Update, body, {}, {.scene = &scene});
        ASSERT_FALSE(missing);
        EXPECT_NE(
            missing.error().message.find("Physics service is unavailable"), std::string::npos);
        EXPECT_FALSE(instance.value()->invoke(
            Script::Phase::Stop, body, {}, {.scene = &scene, .physics = &physics}));
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
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(ScriptAssets{assets})));
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
