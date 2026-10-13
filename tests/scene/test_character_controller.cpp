#include "physics/physics_service.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "audio/audio.h"
#include "audio/audio_service.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "scene/systems/audio_system.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/script_system.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace Comet::Tests {
    namespace {
        class CharacterIntent final: public System {
        public:
            Entity player;
            Math::Vec2 direction{0};
            bool jump = false;
            Result<void, Error> fixed_update(Scene&, const Context& context) override {
                if(player.has_component<CharacterControllerComponent>()
                    && player.get_component<CharacterControllerComponent>().enabled)
                    EXPECT_TRUE(context.services.physics->request_character_move(
                        player, direction, std::exchange(jump, false)));
                return Result<void, Error>::success();
            }
        };

        class CharacterControllerTest: public testing::Test {
        protected:
            Scene scene;
            PhysicsService physics;
            SceneRuntime runtime;
            CharacterIntent* intent = nullptr;
            Entity player;

            void SetUp() override {
                ASSERT_TRUE(runtime.set_services({.physics = &physics}));
                ASSERT_TRUE(runtime.set_settings({.fixed_delta = 1.0 / 60}));
                auto controls = std::make_unique<CharacterIntent>();
                intent = controls.get();
                ASSERT_TRUE(runtime.add_system(std::move(controls)));
                ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
                box({0, -0.5f, 0}, {20, 0.5f, 20});
                player = scene.create_entity("Player");
                player.add_component<CharacterControllerComponent>();
                intent->player = player;
            }

            Entity box(Math::Vec3 position, Math::Vec3 half_extents,
                BodyMotion motion = BodyMotion::Static, Math::Vec3 rotation = {}) {
                auto entity = scene.create_entity("Obstacle");
                entity.edit_transform([&](auto& transform) {
                    transform.translation = position;
                    transform.rotation = rotation;
                });
                entity.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = motion});
                entity.add_component<ColliderComponent>(
                    ColliderComponent{.half_extents = half_extents});
                return entity;
            }

            bool step(int count = 1) {
                for(int index = 0; index < count; ++index) {
                    const auto result = runtime.advance(1.0 / 60);
                    if(!result) {
                        ADD_FAILURE() << result.error().message;
                        return false;
                    }
                }
                return true;
            }

            Math::Vec3 position() const {
                return player.get_component<TransformComponent>().translation;
            }
        };
    }

    TEST_F(CharacterControllerTest, FallsLandsAndJumpsOnlyFromWalkableGround) {
        player.edit_transform([](auto& transform) { transform.translation.y = 3; });
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(physics.get_character_state(player)->grounded);
        ASSERT_TRUE(step(120));
        EXPECT_NEAR(position().y, 0, 0.03f);
        EXPECT_TRUE(physics.get_character_state(player)->grounded);
        intent->jump = true;
        ASSERT_TRUE(step());
        EXPECT_GT(position().y, 0.04f);
        EXPECT_FALSE(physics.get_character_state(player)->grounded);
        const auto first_velocity = physics.get_character_state(player)->velocity.y;
        intent->jump = true;
        ASSERT_TRUE(step());
        EXPECT_LT(physics.get_character_state(player)->velocity.y, first_velocity);
        ASSERT_TRUE(step(120));
        EXPECT_TRUE(physics.get_character_state(player)->grounded);
        EXPECT_NEAR(position().y, 0, 0.03f);
    }

    TEST_F(CharacterControllerTest, BlocksWallsAndSlidesWithoutDiagonalSpeedBoost) {
        box({2, 1, 0}, {0.2f, 1, 10});
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 1};
        ASSERT_TRUE(step(60));
        EXPECT_LT(position().x, 1.52f);
        EXPECT_GT(position().x, 1.3f);
        EXPECT_NEAR(position().z, 4 / std::sqrt(2.0f), 0.05f);
        EXPECT_NEAR(position().y, 0, 0.03f);
    }

    TEST_F(CharacterControllerTest, ClimbsSmallStepsAndRejectsTallLedges) {
        box({1.5f, 0.1f, 0}, {0.5f, 0.1f, 2});
        box({2.5f, 0.2f, 0}, {0.5f, 0.2f, 2});
        box({4, 0.8f, 0}, {0.5f, 0.8f, 2});
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 0};
        ASSERT_TRUE(step(36));
        EXPECT_GT(position().x, 2.0f);
        EXPECT_NEAR(position().y, 0.4f, 0.04f);
        ASSERT_TRUE(step(60));
        EXPECT_LT(position().x, 3.25f);
        EXPECT_LT(position().y, 0.5f);
    }

    TEST_F(CharacterControllerTest, WalksGentleSlopeAndRejectsSteepSlope) {
        box({2, 0.7f, 0}, {2, 0.1f, 2}, BodyMotion::Static, {0, 0, 20});
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 0};
        ASSERT_TRUE(step(45));
        EXPECT_GT(position().x, 2);
        EXPECT_GT(position().y, 0.7f);
        EXPECT_TRUE(physics.get_character_state(player)->grounded);
        ASSERT_TRUE(runtime.stop());

        for(const auto entity : scene.get_entities())
            if(entity != player)
                scene.destroy_entity(entity);
        box({0, -0.5f, 0}, {20, 0.5f, 20});
        box({2, 1.5f, 0}, {2, 0.1f, 2}, BodyMotion::Static, {0, 0, 65});
        player.edit_transform([](auto& transform) { transform.translation = {0, 0, 0}; });
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step(60));
        EXPECT_LT(position().x, 1.3f);
        EXPECT_LT(position().y, 0.3f);
    }

    TEST_F(CharacterControllerTest, RidesKinematicPlatformAndStopsWhenPlatformStops) {
        auto platform = box({0, 1, 0}, {2, 0.2f, 2}, BodyMotion::Kinematic);
        player.edit_transform([](auto& transform) { transform.translation.y = 1.2f; });
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step(3));
        for(int index = 0; index < 60; ++index) {
            platform.edit_transform([](auto& transform) { transform.translation.x += 0.02f; });
            ASSERT_TRUE(step());
        }
        EXPECT_NEAR(position().x, 1.2f, 0.06f);
        EXPECT_NEAR(position().y, 1.2f, 0.04f);
        const auto stopped = position();
        ASSERT_TRUE(step(30));
        EXPECT_NEAR(position().x, stopped.x, 0.04f);
    }

    TEST_F(CharacterControllerTest, PauseStepDisableReenableAndStopReleaseRuntimeState) {
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 0};
        ASSERT_TRUE(step());
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        const auto paused = position();
        ASSERT_TRUE(step(10));
        EXPECT_EQ(position(), paused);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_GT(position().x, paused.x);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        player.edit_component<CharacterControllerComponent>([](auto& c) { c.enabled = false; });
        ASSERT_TRUE(step());
        EXPECT_EQ(physics.get_statistics().characters, 0u);
        EXPECT_FALSE(physics.get_character_state(player));
        player.edit_component<CharacterControllerComponent>([](auto& c) { c.enabled = true; });
        ASSERT_TRUE(step());
        EXPECT_EQ(physics.get_statistics().characters, 1u);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(physics.get_character_state(player));
        EXPECT_FALSE(physics.request_character_move(player, {1, 0}, false));
        EXPECT_EQ(physics.get_statistics().characters, 0u);
    }

    TEST_F(CharacterControllerTest, RejectsConflictingTransformOwnersAndInvalidCapsules) {
        player.add_component<RigidBodyComponent>();
        player.add_component<ColliderComponent>();
        EXPECT_FALSE(runtime.start(scene));
        EXPECT_FALSE(runtime.is_active());
        player.remove_component<RigidBodyComponent>();
        player.edit_component<CharacterControllerComponent>([](auto& c) { c.height = c.radius; });
        EXPECT_FALSE(runtime.start(scene));
        EXPECT_EQ(physics.get_statistics().characters, 0u);
    }

    TEST_F(CharacterControllerTest, TriggerDoesNotBlockAndPublishesEnterAndExit) {
        auto trigger = box({1.5f, 1, 0}, {0.2f, 1, 1});
        trigger.edit_component<ColliderComponent>([](auto& c) { c.is_trigger = true; });
        class Contacts final: public System {
        public:
            std::vector<Scene::ContactEvent> events;
            Result<void, Error> post_fixed_update(Scene& scene, const Context&) override {
                const auto& batch = scene.get_contact_events();
                events.insert(events.end(), batch.begin(), batch.end());
                return Result<void, Error>::success();
            }
        };
        auto probe = std::make_unique<Contacts>();
        auto* contacts = probe.get();
        ASSERT_TRUE(runtime.add_system(std::move(probe)));
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 0};
        ASSERT_TRUE(step(60));
        EXPECT_GT(position().x, 3.8f);
        for(const auto kind :
            {Scene::ContactEvent::Kind::TriggerEnter, Scene::ContactEvent::Kind::TriggerExit}) {
            EXPECT_EQ(std::ranges::count_if(contacts->events,
                          [&](const auto& e) {
                              return e.kind == kind && (e.first == trigger || e.second == trigger);
                          }),
                1);
        }
    }

    TEST_F(CharacterControllerTest, CloneKeepsConfigurationAndEditStateButNoBackendState) {
        const auto components = create_scene_component_registry();
        const SceneSerializer serializer(components);
        player.edit_component<CharacterControllerComponent>([](auto& c) { c.step_height = 0.25f; });
        const auto serialized = serializer.serialize(scene);
        ASSERT_TRUE(serialized) << serialized.error();
        EXPECT_EQ(serialized.value().find("grounded"), std::string::npos);
        auto copy = serializer.deserialize(serialized.value(), "<character-scene>");
        ASSERT_TRUE(copy) << copy.error();
        const auto clone = copy.value()->find_entity(player.get_uuid());
        ASSERT_TRUE(clone);
        EXPECT_EQ(clone.get_component<CharacterControllerComponent>(),
            player.get_component<CharacterControllerComponent>());
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 0};
        ASSERT_TRUE(step(20));
        EXPECT_GT(position().x, 1);
        EXPECT_EQ(clone.get_component<TransformComponent>().translation, Math::Vec3(0));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(physics.get_character_state(player));
    }

    TEST_F(CharacterControllerTest, RemovingAndDestroyingCharactersReleaseTheirProxyBodies) {
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(physics.get_statistics().characters, 1u);
        player.remove_component<CharacterControllerComponent>();
        ASSERT_TRUE(step());
        EXPECT_EQ(physics.get_statistics().characters, 0u);
        player.add_component<CharacterControllerComponent>();
        ASSERT_TRUE(step());
        EXPECT_EQ(physics.get_statistics().characters, 1u);
        const auto uuid = player.get_uuid();
        scene.destroy_entity(player);
        ASSERT_TRUE(step());
        EXPECT_EQ(physics.get_statistics().characters, 0u);
        EXPECT_FALSE(physics.get_character_state(player));
        auto replacement = scene.create_entity_with_uuid(uuid);
        replacement.add_component<CharacterControllerComponent>();
        ASSERT_TRUE(step());
        EXPECT_EQ(physics.get_statistics().characters, 1u);
        EXPECT_TRUE(physics.get_character_state(replacement));
    }

    class CharacterDemoTest: public testing::TestWithParam<bool> {
    protected:
        ComponentRegistry components = create_scene_component_registry();
        SceneSerializer serializer{components};
        std::unique_ptr<Scene> edit_scene;
        std::unique_ptr<Scene> playing;
        AssetRegistry assets;
        PhysicsService physics;
        AudioService audio{assets, AudioPlayback::Mode::Offline};
        SceneRuntime runtime;
        Input input;
        Input::GamepadSample pad;
        Entity player;

        void SetUp() override {
            const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
            ASSERT_TRUE(project) << project.error();
            const std::array<std::filesystem::path, 3> paths{"scripts/character.lua",
                "scripts/character_goal.lua", "scripts/moving_platform.lua"};
            auto scripts = Script::load_group(project.value().paths().assets(), paths);
            ASSERT_TRUE(scripts) << scripts.error().message;
            for(std::size_t index = 0; index < paths.size(); ++index)
                ASSERT_TRUE(assets.register_asset(
                    AssetHandle{14309634625000312004ULL + index}, scripts.value()[index]));
            auto cue = AudioClip::load(project.value().paths().assets() / "audio/play_chime.wav");
            ASSERT_TRUE(cue) << cue.error().message;
            ASSERT_TRUE(
                assets.register_asset(AssetHandle{8247160951280394421ULL}, std::move(cue).value()));
            auto loaded = serializer.load(
                (project.value().paths().assets() / project.value().startup_scene()).string());
            ASSERT_TRUE(loaded) << loaded.error();
            edit_scene = std::move(loaded).value();
            auto clone = serializer.clone(*edit_scene);
            ASSERT_TRUE(clone) << clone.error();
            playing = std::move(clone).value();
            player =
                playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000001"));
            ASSERT_TRUE(player);
            ASSERT_TRUE(runtime.set_input_actions(project.value().input_actions()));
            ASSERT_TRUE(runtime.set_services({.audio = &audio, .physics = &physics}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
            ASSERT_TRUE(runtime.add_system(std::make_unique<AudioSystem>(audio)));
            input.focus_event(true);
            if(GetParam())
                input.gamepad_sample(0, pad);
            ASSERT_TRUE(runtime.start(*playing));
            ASSERT_TRUE(tick());
        }

        bool tick(int count = 1) {
            for(int index = 0; index < count; ++index) {
                const auto result = runtime.advance(1.0 / 60, &input.publish_frame());
                if(!result) {
                    ADD_FAILURE() << result.error().message;
                    return false;
                }
            }
            return true;
        }

        void move(int x, int z) {
            if(GetParam()) {
                pad.axes[static_cast<std::size_t>(Input::GamepadAxis::LeftX)] =
                    static_cast<float>(x);
                pad.axes[static_cast<std::size_t>(Input::GamepadAxis::LeftY)] =
                    static_cast<float>(z);
                input.gamepad_sample(0, pad);
            } else {
                input.key_event(Input::Key::A, x < 0);
                input.key_event(Input::Key::D, x > 0);
                input.key_event(Input::Key::W, z < 0);
                input.key_event(Input::Key::S, z > 0);
            }
        }

        void button(Input::Key key, Input::GamepadButton control, bool down) {
            if(GetParam()) {
                pad.buttons[static_cast<std::size_t>(control)] = down;
                input.gamepad_sample(0, pad);
            } else
                input.key_event(key, down);
        }

        Math::Vec3 position() const {
            return player.get_component<TransformComponent>().translation;
        }

        bool drive(float x, float z, int steps = 300) {
            for(int step = 0; step < steps; ++step) {
                const auto delta = Math::Vec2(x - position().x, z - position().z);
                if(glm::length(delta) < 0.09f) {
                    move(0, 0);
                    return tick();
                }
                const auto axis = [](float v) { return v > 0.05f ? 1 : (v < -0.05f ? -1 : 0); };
                move(axis(delta.x), axis(delta.y));
                if(!tick())
                    return false;
            }
            ADD_FAILURE() << "Cannot reach (" << x << ", " << z << "), actual (" << position().x
                          << ", " << position().y << ", " << position().z << ")";
            return false;
        }

        float score() const {
            const auto value = runtime.get_session().get_value("character.score");
            return value ? std::get<float>(*value) : -1;
        }
    };

    TEST_P(
        CharacterDemoTest, RealProjectInputClimbsCollectsFinishesAndRestartsWithoutEditingSource) {
        const auto edit_before = serializer.serialize(*edit_scene);
        ASSERT_TRUE(edit_before) << edit_before.error();
        ASSERT_TRUE(drive(-3, 5));
        ASSERT_TRUE(drive(-3, -1.5f));
        EXPECT_GT(position().y, 0.7f);
        EXPECT_EQ(score(), 1);
        // 返回起点后走坡道，避开中间阻挡墙。
        ASSERT_TRUE(drive(-3, 5));
        ASSERT_TRUE(drive(4, 5));
        ASSERT_TRUE(drive(4, -6.8f));
        EXPECT_GT(position().y, 1.9f);
        EXPECT_EQ(score(), 2);
        ASSERT_TRUE(drive(4, -9));
        ASSERT_TRUE(drive(-3, -9));
        ASSERT_TRUE(drive(-3, -6.8f));
        button(Input::Key::Space, Input::GamepadButton::South, true);
        ASSERT_TRUE(tick());
        button(Input::Key::Space, Input::GamepadButton::South, false);
        move(0, 1);
        ASSERT_TRUE(tick(30));
        move(0, 0);
        ASSERT_TRUE(tick(90));
        EXPECT_EQ(score(), 3);
        const auto finished = runtime.get_session().get_value("character.finished");
        ASSERT_TRUE(finished);
        EXPECT_TRUE(std::get<bool>(*finished));
        const auto platform =
            playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-00000000001e"));
        ASSERT_TRUE(platform);
        const auto offset =
            position().x - platform.get_component<TransformComponent>().translation.x;
        move(1, 0);
        ASSERT_TRUE(tick(15));
        EXPECT_NEAR(position().x - platform.get_component<TransformComponent>().translation.x,
            offset, 0.1f);
        move(0, 0);
        button(Input::Key::R, Input::GamepadButton::Start, true);
        ASSERT_TRUE(tick());
        EXPECT_TRUE(runtime.take_restart_request());
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(serializer.serialize(*edit_scene).value(), edit_before.value());
        EXPECT_EQ(physics.get_statistics().characters, 0u);
        auto restarted = serializer.clone(*edit_scene);
        ASSERT_TRUE(restarted) << restarted.error();
        const auto player_uuid = player.get_uuid();
        playing = std::move(restarted).value();
        player = playing->find_entity(player_uuid);
        ASSERT_TRUE(runtime.start(
            *playing, SceneRuntime::State::Running, SceneRuntime::InputStart::Rebase));
        EXPECT_EQ(score(), 0);
        EXPECT_EQ(position(), Math::Vec3(0, 0, 5));
        ASSERT_TRUE(tick(3));
        EXPECT_FALSE(runtime.take_restart_request());
        button(Input::Key::R, Input::GamepadButton::Start, false);
        ASSERT_TRUE(tick());
        ASSERT_TRUE(drive(-1, 5));
        EXPECT_TRUE(physics.get_character_state(player)->grounded);
        ASSERT_TRUE(runtime.stop());
    }

    INSTANTIATE_TEST_SUITE_P(KeyboardAndGamepad, CharacterDemoTest, testing::Values(false, true));
}
