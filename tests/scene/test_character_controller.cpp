#include "support/demo_project.h"
#include "asset/data/game_data.h"
#include "gameplay/character_animation.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "asset/default_assets.h"
#include "animation/animation_system.h"
#include "physics/physics_service.h"
#include "particles/particle_system.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "asset/import/mesh_importer.h"
#include "audio/audio.h"
#include "audio/audio_service.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/prefab.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "scene/systems/audio_system.h"
#include "scene/systems/camera_controller.h"
#include "gameplay/character_controller.h"
#include "gameplay/game_session.h"
#include "gameplay/checkpoint_store.h"
#include "support/temporary_directory.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/script_system.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <tuple>

namespace Comet::Tests {
    namespace {
        class CharacterIntent final: public System {
        public:
            Entity player;
            Math::Vec2 direction{0};
            bool jump = false;
            bool run = false;
            bool crouch = false;
            Result<void, Error> fixed_update(Scene&, const Context& context) override {
                if(player.has_component<CharacterControllerComponent>()
                    && player.get_component<CharacterControllerComponent>().enabled)
                    EXPECT_TRUE(context.services.physics->request_character_move(
                        player, {.direction = direction,
                                    .jump = std::exchange(jump, false),
                                    .run = run,
                                    .crouch = crouch}));
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

    TEST_F(CharacterControllerTest,
        ActualSpeedDrivesContinuousAnimationAndKeepsPhaseAcrossThresholds) {
        auto imported = MeshImporter{}.import(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY).parent_path()
            / "engine/resources/characters/default.glb");
        ASSERT_TRUE(imported) << imported.error();
        const auto data = imported.value().animation;
        auto visual = scene.create_entity("Animated visual");
        visual.add_component<MeshRendererComponent>();
        visual.add_component<AnimatorComponent>(
            AnimatorComponent{.controller = DEFAULT_CHARACTER_ANIMATION});
        visual.add_component<CharacterAnimationComponent>(player.get_uuid());
        auto definition = AnimationControllerSerializer{}.load(
            default_character_source().parent_path() / "default.ani");
        ASSERT_TRUE(definition) << definition.error();
        auto controller = std::make_shared<AnimationController>(std::move(definition).value());
        for(auto& state : controller->states)
            state.smoothing = 0;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::CharacterAnimationSystem>()));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; },
                [controller](AssetHandle) { return controller; })));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step(12));
        auto idle = scene.get_animation_playback(visual);
        ASSERT_TRUE(idle);
        EXPECT_EQ(idle->current.clip, "idle");
        intent->direction = {.5f, 0};
        ASSERT_TRUE(step());
        auto walk = scene.get_animation_playback(visual);
        EXPECT_EQ(walk->current.clip, "idle");
        EXPECT_EQ(walk->current.blends.front().clip, "walk");
        EXPECT_NEAR(walk->current.blends.front().weight, .5f, .01f);
        EXPECT_GT(walk->current.time, idle->current.time);
        EXPECT_TRUE(walk->previous.clip.empty());
        intent->run = true;
        intent->direction = {5.f / 6, 0};
        ASSERT_TRUE(step());
        auto run = scene.get_animation_playback(visual);
        EXPECT_EQ(run->current.clip, "walk");
        EXPECT_EQ(run->current.blends.front().clip, "run");
        EXPECT_NEAR(run->current.blends.front().weight, .5f, .01f);
        EXPECT_GT(run->current.time, walk->current.time);
        EXPECT_TRUE(run->previous.clip.empty());
        for(auto& state : controller->states)
            state.smoothing = .15f;
        intent->direction = {0, 0};
        ASSERT_TRUE(step());
        const auto slowing = scene.get_animation_playback(visual);
        EXPECT_EQ(slowing->current.clip, "walk");
        EXPECT_EQ(slowing->current.blends.front().clip, "run");
        const float speed = 5 * std::exp(-1.f / 60 / .15f);
        EXPECT_NEAR(slowing->current.blends.front().weight, (speed - 4) / 2, .01f);
        EXPECT_GT(slowing->current.time, run->current.time);
        intent->crouch = true;
        intent->direction = {.5f, 0};
        ASSERT_TRUE(step());
        auto crouch = scene.get_animation_playback(visual);
        EXPECT_EQ(crouch->current.clip, "crouch");
        EXPECT_EQ(crouch->current.blends.front().clip, "crouch_walk");
        EXPECT_NEAR(crouch->current.blends.front().weight, .5f, .01f);
        EXPECT_EQ(crouch->previous.clip, "walk");
        EXPECT_EQ(crouch->previous.blends.front().clip, "run");
        EXPECT_LT(crouch->blend, 1);
        ASSERT_TRUE(sample_animation(*data, crouch.get()));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(step());
        EXPECT_EQ(scene.get_animation_playback(visual), crouch);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(step());
        EXPECT_GT(scene.get_animation_playback(visual)->current.time, crouch->current.time);
        EXPECT_NEAR(walk->current.blends.front().weight, .5f, .01f);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(visual));
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

    TEST_F(CharacterControllerTest, RunAndCrouchUseConfiguredSpeedsAndKeepFeetOnTheFloor) {
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 0};
        intent->run = true;
        ASSERT_TRUE(step(30));
        EXPECT_NEAR(position().x, 3, 0.05f);
        EXPECT_TRUE(physics.get_character_state(player)->running);
        EXPECT_EQ(Gameplay::CharacterController::get_state(player, physics).value().motion,
            Gameplay::CharacterController::Motion::Running);
        intent->crouch = true;
        ASSERT_TRUE(step(30));
        EXPECT_NEAR(position().x, 4, 0.05f);
        EXPECT_NEAR(position().y, 0, 0.03f);
        const auto crouched = physics.get_character_state(player);
        ASSERT_TRUE(crouched);
        EXPECT_TRUE(crouched->crouched);
        EXPECT_FALSE(crouched->running);
        EXPECT_NEAR(crouched->height_ratio, 0.6f, 0.001f);
        EXPECT_EQ(physics.get_statistics().characters, 1u);
        intent->run = false;
        intent->crouch = false;
        ASSERT_TRUE(step(30));
        EXPECT_NEAR(position().x, 6, 0.05f);
        EXPECT_FALSE(physics.get_character_state(player)->crouched);
        EXPECT_NEAR(physics.get_character_state(player)->height_ratio, 1, 0.001f);
    }

    TEST_F(CharacterControllerTest, LowRoofBlocksStandingAndKeepsCrouchUntilThereIsSpace) {
        box({0, 1.35f, -2}, {0.8f, 0.15f, 1});
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {0, -1};
        ASSERT_TRUE(step(30));
        EXPECT_GT(position().z, -0.8f);
        intent->crouch = true;
        ASSERT_TRUE(step(30));
        EXPECT_LT(position().z, -1.3f);
        EXPECT_TRUE(physics.get_character_state(player)->crouched);
        intent->crouch = false;
        intent->run = true;
        ASSERT_TRUE(step(15));
        EXPECT_TRUE(physics.get_character_state(player)->crouched);
        EXPECT_FALSE(physics.get_character_state(player)->running);
        ASSERT_TRUE(step(90));
        EXPECT_LT(position().z, -4);
        EXPECT_FALSE(physics.get_character_state(player)->crouched);
        EXPECT_NEAR(position().y, 0, 0.03f);
    }

    TEST_F(CharacterControllerTest, SensorRoofDoesNotPreventStanding) {
        auto sensor = box({0, 1.35f, 0}, {2, 0.15f, 2});
        sensor.edit_component<ColliderComponent>(
            [](auto& collider) { collider.is_trigger = true; });
        ASSERT_TRUE(runtime.start(scene));
        intent->crouch = true;
        ASSERT_TRUE(step());
        EXPECT_TRUE(physics.get_character_state(player)->crouched);
        intent->crouch = false;
        ASSERT_TRUE(step());
        EXPECT_FALSE(physics.get_character_state(player)->crouched);
    }

    TEST_F(CharacterControllerTest, AirControlPreservesMomentumWhenDisabledAndCanTurnWhenEnabled) {
        player.edit_component<CharacterControllerComponent>([](auto& settings) {
            settings.air_control = 0;
            settings.gravity_scale = 0;
        });
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 0};
        ASSERT_TRUE(step());
        intent->jump = true;
        ASSERT_TRUE(step());
        intent->direction = {-1, 0};
        ASSERT_TRUE(step(15));
        EXPECT_NEAR(physics.get_character_state(player)->velocity.x, 4, 0.01f);
        EXPECT_FALSE(physics.get_character_state(player)->grounded);
        player.edit_component<CharacterControllerComponent>(
            [](auto& settings) { settings.air_control = 1; });
        ASSERT_TRUE(step(20));
        EXPECT_LT(physics.get_character_state(player)->velocity.x, -3);
    }

    TEST_F(CharacterControllerTest, ActualMovementStateDoesNotReportWalkingAgainstAWall) {
        box({1.5f, 1, 0}, {0.2f, 1, 2});
        ASSERT_TRUE(runtime.start(scene));
        intent->direction = {1, 0};
        intent->run = true;
        ASSERT_TRUE(step(90));
        const auto state = Gameplay::CharacterController::get_state(player, physics);
        ASSERT_TRUE(state);
        EXPECT_EQ(state.value().motion, Gameplay::CharacterController::Motion::Idle);
        EXPECT_NEAR(state.value().speed, 0, 0.03f);
    }

    TEST_F(CharacterControllerTest, CharacterMovementRequiresMutualCollisionLayers) {
        auto wall = box({2, 2, 0}, {0.2f, 2, 4});
        wall.edit_component<ColliderComponent>([](auto& collider) {
            collider.collision_layer = 2;
            collider.collision_mask = 1;
        });
        intent->direction = {1, 0};
        for(const bool detect_wall : {true, false}) {
            player.edit_component<CharacterControllerComponent>(
                [&](auto& character) { character.collision_mask = detect_wall ? 3u : 1u; });
            player.edit_transform([](auto& transform) { transform.translation = {}; });
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(step(90));
            const auto x = player.get_component<TransformComponent>().translation.x;
            if(detect_wall)
                EXPECT_LT(x, 1.6f);
            else
                EXPECT_GT(x, 3);
            ASSERT_TRUE(runtime.stop());
        }
        player.edit_component<CharacterControllerComponent>(
            [](auto& character) { character.collision_mask = 3; });
        wall.edit_component<ColliderComponent>([](auto& collider) { collider.collision_mask = 0; });
        player.edit_transform([](auto& transform) { transform.translation = {}; });
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step(90));
        EXPECT_GT(player.get_component<TransformComponent>().translation.x, 3);
    }

    TEST_F(CharacterControllerTest, SphereSweepIgnoresSelfAndSensorsAndStopsBeforeSolidBodies) {
        box({0, 1, 3}, {2, 1, 0.2f});
        auto sensor = box({0, 1, 1}, {2, 1, 0.1f});
        sensor.edit_component<ColliderComponent>(
            [](auto& collider) { collider.is_trigger = true; });
        ASSERT_TRUE(runtime.start(scene));
        const auto hit = physics.sweep_sphere(player, {0, 1, 0}, {0, 0, 5}, 0.15f);
        ASSERT_TRUE(hit);
        EXPECT_NEAR(*hit, 0.53f, 0.01f);
        const auto self = physics.sweep_sphere({}, {0, 1, 0}, {0, 0, 5}, 0.15f);
        ASSERT_TRUE(self);
        EXPECT_EQ(*self, 0);
        EXPECT_FALSE(physics.sweep_sphere(player, {}, {}, -1));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(physics.sweep_sphere(player, {}, {0, 0, 1}, 0.15f));
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
        EXPECT_FALSE(physics.request_character_move(player, {.direction = {1, 0}}));
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
        std::optional<RuntimeSession::CheckpointRequest> checkpoint;

        void SetUp() override {
            const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
            ASSERT_TRUE(project) << project.error();
            for(const auto& [handle, path] :
                {std::pair{6729301842000000001ULL, "data/collection.csv"},
                    std::pair{6729301842000000002ULL, "data/feedback.data.json"}}) {
                auto data = GameData::load(project.value().paths().assets() / path);
                ASSERT_TRUE(data) << data.error();
                ASSERT_TRUE(assets.register_asset(
                    AssetHandle(handle), std::make_shared<GameData>(std::move(data).value())));
            }
            const std::array<std::filesystem::path, 3> paths{"scripts/character.lua",
                "scripts/character_goal.lua", "scripts/moving_platform.lua"};
            auto scripts = Script::load_group(project.value().paths().assets(), paths);
            ASSERT_TRUE(scripts) << scripts.error().message;
            for(std::size_t index = 0; index < paths.size(); ++index)
                ASSERT_TRUE(assets.register_asset(
                    AssetHandle{14309634625000312004ULL + index}, scripts.value()[index]));
            auto gate_script =
                Script::load(project.value().paths().assets() / "scripts/finish_gate.lua");
            ASSERT_TRUE(gate_script) << gate_script.error().message;
            ASSERT_TRUE(
                assets.register_asset(AssetHandle{14309634625000312008ULL}, gate_script.value()));
            auto gate = Prefab::load(
                project.value().paths().assets() / "prefabs/finish_gate.prefab", components);
            ASSERT_TRUE(gate) << gate.error();
            ASSERT_TRUE(assets.register_asset(AssetHandle{7863069821640201103ULL},
                std::make_shared<Prefab>(std::move(gate).value())));
            auto cue = AudioClip::load(project.value().paths().assets() / "audio/play_chime.wav");
            ASSERT_TRUE(cue) << cue.error().message;
            ASSERT_TRUE(
                assets.register_asset(AssetHandle{8247160951280394421ULL}, std::move(cue).value()));
            auto feedback =
                Script::load(project.value().paths().assets() / "scripts/character_feedback.lua");
            ASSERT_TRUE(feedback) << feedback.error().message;
            ASSERT_TRUE(
                assets.register_asset(AssetHandle{13857136248767905805ULL}, feedback.value()));
            auto step = AudioClip::load(project.value().paths().assets() / "audio/footstep.wav");
            ASSERT_TRUE(step) << step.error().message;
            ASSERT_TRUE(assets.register_asset(
                AssetHandle{13857136248767905806ULL}, std::move(step).value()));
            for(const auto& [handle, file, stream] :
                {std::tuple{13857136248767905807ULL, "audio/ambient_loop.wav", true},
                    std::tuple{13857136248767905808ULL, "audio/spatial_chime.wav", false}}) {
                auto clip = AudioClip::load(project.value().paths().assets() / file, stream);
                ASSERT_TRUE(clip) << clip.error().message;
                ASSERT_TRUE(assets.register_asset(AssetHandle{handle}, std::move(clip).value()));
            }
            ASSERT_TRUE(prepare_demo_journey(assets));
            auto loaded = load_demo_scene(components, project.value().startup_scene());
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
            ASSERT_TRUE(runtime.add_system(std::make_unique<CameraControllerSystem>()));
            ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
            auto mesh = MeshImporter{}.import(
                std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY).parent_path()
                / "engine/resources/characters/default.glb");
            ASSERT_TRUE(mesh) << mesh.error();
            auto definition = AnimationControllerSerializer{}.load(
                project.value().paths().assets() / "animations/character.ani");
            ASSERT_TRUE(definition) << definition.error();
            auto controller = std::make_shared<AnimationController>(std::move(definition).value());
            ASSERT_TRUE(assets.register_asset(AssetHandle{13857136248767905804ULL}, controller));
            auto layered = AnimationControllerSerializer{}.load(
                project.value().paths().assets() / "animations/layered.ani");
            ASSERT_TRUE(layered) << layered.error();
            ASSERT_TRUE(assets.register_asset(AssetHandle{6729301842000000012ULL},
                std::make_shared<AnimationController>(std::move(layered).value())));
            auto library = AnimationLibrary::load(
                project.value().paths().assets() / "animations/shared.anilib");
            ASSERT_TRUE(library) << library.error();
            library.value().sources.emplace(DEFAULT_CHARACTER_MESH, mesh.value().animation);
            ASSERT_TRUE(assets.register_asset(AssetHandle{6729301842000000011ULL},
                std::make_shared<AnimationLibrary>(std::move(library).value())));
            ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::CharacterAnimationSystem>()));
            ASSERT_TRUE(runtime.add_system(std::make_unique<AnimationSystem>(
                [data = mesh.value().animation](AssetHandle) { return data; },
                [this](AssetHandle handle) { return assets.resolve<AnimationController>(handle); },
                [this](AssetHandle handle) { return assets.resolve<AnimationLibrary>(handle); })));
            ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
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
                if(auto saved = runtime.take_checkpoint_request())
                    checkpoint = std::move(saved);
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
                if(Math::length(delta) < 0.09f) {
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
        const auto visual =
            playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000002"));
        const auto camera =
            playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000003"));
        ASSERT_TRUE(visual);
        ASSERT_TRUE(camera);
        (void)playing->get_world_matrix(visual);
        EXPECT_NEAR(
            visual.get_component<WorldTransformComponent>().pose_world_matrix[2].x, 1, 0.0001f);
        EXPECT_EQ(camera.get_component<TransformComponent>().rotation, Math::Vec3(-18, 0, 0));
        EXPECT_EQ(player.get_component<TransformComponent>().rotation, Math::Vec3(0));
        const auto paused_position = position();
        const auto paused_rotation = visual.get_component<TransformComponent>().rotation;
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        move(0, -1);
        ASSERT_TRUE(tick(3));
        EXPECT_EQ(position(), paused_position);
        EXPECT_EQ(visual.get_component<TransformComponent>().rotation, paused_rotation);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(tick());
        EXPECT_LT(position().z, paused_position.z);
        (void)playing->get_world_matrix(visual);
        EXPECT_NEAR(
            visual.get_component<WorldTransformComponent>().pose_world_matrix[2].z, 1, 0.0001f);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        move(0, 0);
        ASSERT_TRUE(tick());
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
        ASSERT_TRUE(tick(120));
        std::size_t gates = 0;
        for(auto candidate : playing->get_entities()) {
            if(candidate.get_component<NameComponent>().name != "终点门")
                continue;
            ++gates;
            EXPECT_FALSE(candidate.has_component<PrefabInstanceComponent>());
            const auto& parameters = candidate.get_component<ScriptComponent>().parameters;
            const auto panel = playing->find_entity(std::get<EntityUuid>(parameters.at("panel")));
            ASSERT_TRUE(panel);
            EXPECT_EQ(playing->get_parent(panel), candidate);
            EXPECT_NEAR(panel.get_component<TransformComponent>().translation.y, 3.2f, 0.01f);
        }
        EXPECT_EQ(gates, 1u);
        button(Input::Key::R, Input::GamepadButton::Start, true);
        ASSERT_TRUE(tick());
        EXPECT_TRUE(runtime.take_scene_request());
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(serializer.serialize(*edit_scene).value(), edit_before.value());
        EXPECT_EQ(physics.get_statistics().characters, 0u);
        auto restarted = serializer.clone(*edit_scene);
        ASSERT_TRUE(restarted) << restarted.error();
        const auto player_uuid = player.get_uuid();
        const auto visual_uuid = visual.get_uuid();
        playing = std::move(restarted).value();
        player = playing->find_entity(player_uuid);
        ASSERT_TRUE(runtime.start(
            *playing, SceneRuntime::State::Running, SceneRuntime::InputStart::Rebase));
        EXPECT_EQ(score(), 0);
        EXPECT_EQ(position(), Math::Vec3(0, 0, 5));
        EXPECT_EQ(playing->find_entity(visual_uuid).get_component<TransformComponent>().rotation,
            Math::Vec3(0));
        ASSERT_TRUE(tick(3));
        EXPECT_FALSE(runtime.take_scene_request());
        button(Input::Key::R, Input::GamepadButton::Start, false);
        ASSERT_TRUE(tick());
        ASSERT_TRUE(drive(-1, 5));
        EXPECT_TRUE(physics.get_character_state(player)->grounded);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_P(CharacterDemoTest, TwoLevelsReturnAndContinueSavedCollectionsFromActualProjectScripts) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project);
        const auto original = serializer.serialize(*edit_scene).value();
        TemporaryDirectory directory;
        const auto save_path = directory.path() / "checkpoint.json";
        CheckpointStore store(project.value().id(), save_path);
        GameSession game(serializer);
        game.begin(*edit_scene, "scenes/character.scene");
        const auto persistence = [&](const CheckpointStore& source) {
            return source.persistence(
                [](std::string_view path) {
                    return AssetHandle{path == "scenes/character.scene" ? 1u : 2u};
                },
                [](AssetHandle handle) -> std::optional<std::string> {
                    if(handle == AssetHandle{1})
                        return "scenes/character.scene";
                    if(handle == AssetHandle{2})
                        return "scenes/character_second.scene";
                    return std::nullopt;
                });
        };
        game.set_persistence(persistence(store));
        const auto transition = [&](std::string_view event) {
            ASSERT_TRUE(playing->emit_event(event));
            ASSERT_TRUE(tick(2));
            auto request = runtime.take_scene_request();
            ASSERT_TRUE(request);
            auto prepared = game.prepare(
                *request, [&](std::string_view path) { return load_demo_scene(components, path); });
            ASSERT_TRUE(prepared) << prepared.error();
            ASSERT_TRUE(runtime.stop());
            playing = std::move(prepared.value().scene);
            ASSERT_TRUE(runtime.start(*playing, SceneRuntime::State::Running,
                SceneRuntime::InputStart::Rebase, prepared.value().values,
                prepared.value().checkpoint));
            player =
                playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000001"));
            game.accept(std::move(prepared).value());
            ASSERT_TRUE(tick(2));
        };
        const auto collect = [&](const char* id) {
            const auto goal = playing->find_entity(*EntityUuid::parse(id));
            ASSERT_TRUE(goal);
            auto transform = player.get_component<TransformComponent>();
            transform.translation = goal.get_component<TransformComponent>().translation;
            ASSERT_TRUE(player.try_set_transform(transform));
            ASSERT_TRUE(tick(3));
            EXPECT_FALSE(playing->find_entity(*EntityUuid::parse(id)));
            ASSERT_TRUE(playing->get_particles());
            EXPECT_GE(playing->get_particles()->instances.size(), 24u);
            ASSERT_TRUE(checkpoint);
            ASSERT_TRUE(game.save_checkpoint(*checkpoint));
            checkpoint.reset();
        };
        ASSERT_TRUE(playing->emit_event("character.next"));
        ASSERT_TRUE(tick(2));
        EXPECT_FALSE(runtime.take_scene_request());
        ASSERT_NO_FATAL_FAILURE(collect("70a518f1-2c0e-4180-9ead-000000000032"));
        ASSERT_NO_FATAL_FAILURE(collect("70a518f1-2c0e-4180-9ead-000000000033"));
        ASSERT_NO_FATAL_FAILURE(collect("70a518f1-2c0e-4180-9ead-000000000034"));
        EXPECT_EQ(score(), 3);
        ASSERT_NO_FATAL_FAILURE(transition("character.next"));
        EXPECT_EQ(game.scene_path(), "scenes/character_second.scene");
        EXPECT_EQ(score(), 0);
        ASSERT_NO_FATAL_FAILURE(collect("70a518f1-2c0e-4180-9ead-000000000032"));
        EXPECT_EQ(score(), 1);
        // 返回不覆盖自动存档；第一关已经收集的目标不再生成。
        ASSERT_NO_FATAL_FAILURE(transition("character.return"));
        EXPECT_EQ(game.scene_path(), "scenes/character.scene");
        EXPECT_EQ(score(), 3);
        EXPECT_FALSE(
            playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000033")));
        CheckpointStore reopened(project.value().id(), save_path);
        game.set_persistence(persistence(reopened));
        ASSERT_NO_FATAL_FAILURE(transition("character.continue"));
        EXPECT_EQ(game.scene_path(), "scenes/character_second.scene");
        EXPECT_EQ(runtime.get_session().checkpoint(), "journey_start");
        EXPECT_EQ(score(), 1);
        EXPECT_NEAR(position().x, 0, 0.01f);
        EXPECT_NEAR(position().z, 5, 0.01f);
        EXPECT_FALSE(
            playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000032")));
        ASSERT_NO_FATAL_FAILURE(collect("70a518f1-2c0e-4180-9ead-000000000033"));
        ASSERT_NO_FATAL_FAILURE(collect("70a518f1-2c0e-4180-9ead-000000000034"));
        EXPECT_EQ(score(), 3);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(serializer.serialize(*edit_scene).value(), original);
    }

    TEST_P(CharacterDemoTest, ProjectBindingsRunCrouchAndPassTheTunnelWithoutStandingIntoItsRoof) {
        const auto source = serializer.serialize(*edit_scene);
        ASSERT_TRUE(source);
        const auto visual =
            playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000002"));
        ASSERT_TRUE(visual);
        button(Input::Key::LeftShift, Input::GamepadButton::LeftShoulder, true);
        move(1, 0);
        ASSERT_TRUE(tick(30));
        EXPECT_NEAR(position().x, 3, 0.05f);
        EXPECT_EQ(runtime.get_session().get_value("character.motion"),
            ParameterValue(std::string("run")));
        button(Input::Key::LeftShift, Input::GamepadButton::LeftShoulder, false);
        button(Input::Key::LeftControl, Input::GamepadButton::RightShoulder, true);
        move(-1, 0);
        ASSERT_TRUE(tick(30));
        EXPECT_NEAR(position().x, 2, 0.05f);
        EXPECT_NEAR(visual.get_component<TransformComponent>().scale.y, 1.0f, 0.001f);
        EXPECT_EQ(runtime.get_session().get_value("character.motion"),
            ParameterValue(std::string("crouch")));
        button(Input::Key::LeftControl, Input::GamepadButton::RightShoulder, false);
        move(0, 0);
        ASSERT_TRUE(tick());
        EXPECT_NEAR(visual.get_component<TransformComponent>().scale.y, 1.0f, 0.001f);
        ASSERT_TRUE(drive(0, 5));
        ASSERT_TRUE(drive(0, 3.6f));
        move(0, -1);
        ASSERT_TRUE(tick(30));
        EXPECT_GT(position().z, 2.75f);
        button(Input::Key::LeftControl, Input::GamepadButton::RightShoulder, true);
        ASSERT_TRUE(tick(60));
        EXPECT_LT(position().z, 1.2f);
        EXPECT_TRUE(physics.get_character_state(player)->crouched);
        button(Input::Key::LeftControl, Input::GamepadButton::RightShoulder, false);
        move(0, 0);
        ASSERT_TRUE(tick(3));
        EXPECT_TRUE(physics.get_character_state(player)->crouched);
        EXPECT_NEAR(visual.get_component<TransformComponent>().scale.y, 1.0f, 0.001f);
        move(0, -1);
        ASSERT_TRUE(tick(120));
        EXPECT_LT(position().z, -2);
        EXPECT_FALSE(physics.get_character_state(player)->crouched);
        EXPECT_NEAR(visual.get_component<TransformComponent>().scale.y, 1.0f, 0.001f);
        move(0, 0);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(serializer.serialize(*edit_scene).value(), source.value());
    }

    TEST_P(CharacterDemoTest, MouseOrGamepadTurnsTheViewAndMovementUsesTheNewCameraHeading) {
        const auto source = serializer.serialize(*edit_scene);
        ASSERT_TRUE(source);
        auto camera =
            playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000003"));
        ASSERT_TRUE(camera);
        if(GetParam()) {
            pad.axes[static_cast<size_t>(Input::GamepadAxis::RightX)] = 1;
            input.gamepad_sample(0, pad);
            ASSERT_TRUE(tick(45));
            EXPECT_TRUE(runtime.wants_cursor_capture());
            pad.axes[static_cast<size_t>(Input::GamepadAxis::RightX)] = 0;
            input.gamepad_sample(0, pad);
            ASSERT_TRUE(tick());
        } else {
            input.cursor_event({100, 0});
            input.mouse_button_event(Input::MouseButton::Right, true);
            ASSERT_TRUE(tick());
            EXPECT_TRUE(runtime.wants_cursor_capture());
            input.cursor_event({550, 0});
            ASSERT_TRUE(tick());
            input.mouse_button_event(Input::MouseButton::Right, false);
            ASSERT_TRUE(tick());
        }
        EXPECT_NEAR(camera.get_component<TransformComponent>().rotation.y, -90, 0.001f);
        move(0, -1);
        ASSERT_TRUE(tick(30));
        EXPECT_NEAR(position().x, 2, 0.05f);
        EXPECT_NEAR(position().z, 5, 0.05f);
        EXPECT_EQ(player.get_component<TransformComponent>().rotation, Math::Vec3(0));
        move(0, 0);
        if(GetParam()) {
            button(Input::Key::PageUp, Input::GamepadButton::DpadUp, true);
            ASSERT_TRUE(tick(30));
            button(Input::Key::PageUp, Input::GamepadButton::DpadUp, false);
        } else {
            input.scroll_event({0, 3});
            ASSERT_TRUE(tick());
        }
        EXPECT_NEAR(camera.get_component<CameraControllerComponent>().orbit_distance, 4.5f, 0.001f);
        const auto stopped = position();
        const auto rotation = camera.get_component<TransformComponent>().rotation;
        input.focus_event(false);
        input.cursor_event({20000, 20000});
        input.key_event(Input::Key::W, true);
        ASSERT_TRUE(tick(5));
        EXPECT_NEAR(position().x, stopped.x, 0.01f);
        EXPECT_NEAR(position().z, stopped.z, 0.01f);
        EXPECT_EQ(camera.get_component<TransformComponent>().rotation, rotation);
        EXPECT_FALSE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(serializer.serialize(*edit_scene).value(), source.value());
    }

    TEST_P(CharacterDemoTest, ReloadDuringCrouchDoesNotChangeTheFacingTransform) {
        button(Input::Key::LeftControl, Input::GamepadButton::RightShoulder, true);
        ASSERT_TRUE(tick());
        const auto visual =
            playing->find_entity(*EntityUuid::parse("70a518f1-2c0e-4180-9ead-000000000002"));
        ASSERT_TRUE(visual);
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project);
        const std::array<std::filesystem::path, 1> path{"scripts/character.lua"};
        const auto gate = runtime.get_session().get_value("character.gate");
        ASSERT_TRUE(gate);
        const auto gate_uuid = EntityUuid::parse(std::get<std::string>(*gate));
        ASSERT_TRUE(gate_uuid);
        ASSERT_TRUE(playing->find_entity(*gate_uuid));
        const auto entity_count = playing->entity_count();
        for(int reload = 0; reload < 3; ++reload) {
            const auto script = Script::load_group(project.value().paths().assets(), path);
            ASSERT_TRUE(script);
            ASSERT_TRUE(
                assets.replace_asset(AssetHandle{14309634625000312004ULL}, script.value()[0]));
            ASSERT_TRUE(tick(2));
            EXPECT_NEAR(visual.get_component<TransformComponent>().scale.y, 1.0f, 0.001f);
            EXPECT_EQ(runtime.get_session().get_value("character.gate"), gate);
            EXPECT_TRUE(playing->find_entity(*gate_uuid));
            EXPECT_EQ(playing->entity_count(), entity_count);
        }
        button(Input::Key::LeftControl, Input::GamepadButton::RightShoulder, false);
        ASSERT_TRUE(tick());
        EXPECT_NEAR(visual.get_component<TransformComponent>().scale.y, 1.0f, 0.001f);
        ASSERT_TRUE(runtime.stop());
    }

    INSTANTIATE_TEST_SUITE_P(KeyboardAndGamepad, CharacterDemoTest, testing::Values(false, true));
}
