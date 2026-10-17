#include "animation/animation_system.h"
#include "asset/data/game_data.h"
#include "asset/data/navigation_data.h"
#include "asset/database.h"
#include "asset/import/mesh_importer.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "audio/audio_service.h"
#include "core/project.h"
#include "gameplay/character_animation.h"
#include "gameplay/navigation_system.h"
#include "particles/particle_system.h"
#include "physics/physics_service.h"
#include "scene/component_registry.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "scene/systems/audio_system.h"
#include "scene/systems/camera_controller.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/script_system.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace Comet::Tests {
    class CombatDemoTest: public testing::TestWithParam<bool> {
    protected:
        ComponentRegistry components = create_scene_component_registry();
        SceneSerializer serializer{components};
        AssetRegistry assets;
        PhysicsService physics;
        AudioService audio{assets, AudioPlayback::Mode::Offline};
        SceneRuntime runtime;
        Input input;
        Input::GamepadSample pad;
        std::unique_ptr<Scene> authored, playing;
        Entity player, guard, second;

        void SetUp() override {
            auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
            ASSERT_TRUE(project) << project.error();
            AssetDatabase database(project.value().paths());
            const auto scan = database.scan();
            ASSERT_TRUE(scan.succeeded())
                << (scan.issues.empty() ? "" : scan.issues.front().message);
            const auto handle = [&](const char* path) {
                const auto* record = database.find(std::filesystem::path(path));
                EXPECT_NE(record, nullptr) << path;
                return record ? record->handle : AssetHandle{};
            };
            const auto root = project.value().paths().assets();
            for(const auto* path : {"scripts/combat_actor.lua", "scripts/combat_feedback.lua",
                    "scripts/moving_platform.lua"}) {
                auto script = Script::load(root / path);
                ASSERT_TRUE(script) << script.error().message;
                ASSERT_TRUE(assets.register_asset(handle(path), std::move(script).value()));
            }
            auto data = GameData::load(root / "data/combat.data.json");
            ASSERT_TRUE(data) << data.error();
            ASSERT_TRUE(assets.register_asset(handle("data/combat.data.json"),
                std::make_shared<GameData>(std::move(data).value())));
            for(const auto* path : {"audio/footstep.wav", "audio/spatial_chime.wav"}) {
                auto clip = AudioClip::load(root / path);
                ASSERT_TRUE(clip) << clip.error().message;
                ASSERT_TRUE(assets.register_asset(handle(path), std::move(clip).value()));
            }
            auto controller = AnimationControllerSerializer{}.load(root / "animations/combat.ani");
            ASSERT_TRUE(controller) << controller.error();
            ASSERT_TRUE(assets.register_asset(handle("animations/combat.ani"),
                std::make_shared<AnimationController>(std::move(controller).value())));
            auto mesh = MeshImporter{}.import(
                root.parent_path().parent_path() / "engine/resources/characters/default.glb");
            ASSERT_TRUE(mesh) << mesh.error();
            auto loaded = serializer.load((root / "scenes/combat.scene").string());
            ASSERT_TRUE(loaded) << loaded.error();
            authored = std::move(loaded).value();
            authored->each<const NavigationRegionComponent>([&](Entity, const auto& region) {
                const auto* record = database.find(region.data);
                ASSERT_TRUE(record);
                auto navigation = NavigationData::load(root / record->path);
                ASSERT_TRUE(navigation) << navigation.error();
                ASSERT_TRUE(assets.register_asset(
                    region.data, std::make_shared<NavigationData>(std::move(navigation).value())));
            });
            auto navigation = std::make_unique<Gameplay::NavigationSystem>(
                [&](Math::Vec3 a, Math::Vec3 b) { return physics.static_triangles(a, b); },
                [&](AssetHandle id) { return assets.resolve<NavigationData>(id); });
            ASSERT_TRUE(runtime.set_input_actions(project.value().input_actions()));
            ASSERT_TRUE(runtime.set_services(
                {.audio = &audio, .physics = &physics, .navigation = navigation.get()}));
            ASSERT_TRUE(runtime.add_system(std::move(navigation)));
            ASSERT_TRUE(runtime.add_system(std::make_unique<CameraControllerSystem>()));
            ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
            ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::CharacterAnimationSystem>()));
            ASSERT_TRUE(runtime.add_system(std::make_unique<AnimationSystem>(
                [source = mesh.value().animation](AssetHandle) { return source; },
                [&](AssetHandle id) { return assets.resolve<AnimationController>(id); })));
            ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
            ASSERT_TRUE(runtime.add_system(std::make_unique<AudioSystem>(audio)));
            input.focus_event(true);
            if(GetParam())
                input.gamepad_sample(0, pad);
            ASSERT_NO_FATAL_FAILURE(start());
        }
        void TearDown() override { EXPECT_TRUE(runtime.stop()); }
        void start() {
            auto clone = serializer.clone(*authored);
            ASSERT_TRUE(clone) << clone.error();
            playing = std::move(clone).value();
            for(auto entity : playing->get_entities()) {
                const auto& name = entity.get_component<NameComponent>().name;
                if(name == "练习者")
                    player = entity;
                if(name == "守卫 1")
                    guard = entity;
                if(name == "守卫 2")
                    second = entity;
            }
            ASSERT_TRUE(player && guard && second);
            const auto started = runtime.start(
                *playing, SceneRuntime::State::Running, SceneRuntime::InputStart::Rebase);
            ASSERT_TRUE(started) << started.error().message;
            ASSERT_TRUE(tick());
        }
        bool tick(int count = 1) {
            for(int i = 0; i < count; ++i) {
                auto result = runtime.advance(1.0 / 60, &input.publish_frame());
                if(!result) {
                    ADD_FAILURE() << result.error().message;
                    return false;
                }
            }
            return true;
        }
        float value(const char* name) const {
            const auto result = runtime.get_session().get_value(name);
            return result ? std::get<float>(*result) : -1;
        }
        void button(bool pressed, bool restart = false) {
            if(GetParam()) {
                pad.buttons[static_cast<size_t>(
                    restart ? Input::GamepadButton::Start : Input::GamepadButton::West)] = pressed;
                input.gamepad_sample(0, pad);
            } else
                input.key_event(restart ? Input::Key::R : Input::Key::J, pressed);
        }
        void place(Entity entity, Math::Vec3 position) {
            entity.edit_transform([&](auto& transform) { transform.translation = position; });
        }
        void arrange() {
            place(player, {3, 0, 6});
            place(guard, {3, 0, 4.7f});
            place(second, {-9, 0, -9});
        }
    };

    TEST_P(CombatDemoTest, AttackUsesAnimationWindowAndDoesNotRepeatWhileHeld) {
        arrange();
        ASSERT_TRUE(tick(3));
        button(true);
        ASSERT_TRUE(tick(6));
        EXPECT_EQ(value("combat.health.guard1"), 90);
        ASSERT_TRUE(tick(20));
        EXPECT_EQ(value("combat.health.guard1"), 55);
        const auto snapshot = playing->get_particles();
        ASSERT_TRUE(snapshot);
        EXPECT_FALSE(snapshot->batches.empty());
        std::array<float, 2048> samples{};
        ASSERT_TRUE(audio.read_frames(samples));
        EXPECT_TRUE(std::ranges::any_of(samples, [](float x) { return std::abs(x) > .00001f; }));
        ASSERT_TRUE(tick(60));
        EXPECT_EQ(value("combat.health.guard1"), 55);
        EXPECT_LT(value("combat.health.player"), 120);
        ASSERT_TRUE(playing->emit_event("combat.damage", 120.f, player));
        ASSERT_TRUE(tick());
        EXPECT_EQ(value("combat.health.player"), 0);
        button(false);
        ASSERT_TRUE(tick());
        button(true);
        ASSERT_TRUE(tick(40));
        EXPECT_EQ(value("combat.health.guard1"), 55);
        button(true, true);
        ASSERT_TRUE(tick());
        EXPECT_TRUE(runtime.take_scene_request());
    }

    TEST_P(CombatDemoTest, GuardsNavigateAroundTheArenaWallBeforeAttacking) {
        place(player, {3, 0, 0});
        place(guard, {-3, 0, 0});
        place(second, {-10, 0, -10});
        ASSERT_TRUE(tick(360));
        const auto position = Math::Vec3(playing->get_world_matrix(guard)[3]);
        EXPECT_GT(position.x, 1.5f);
        EXPECT_LT(Math::distance(Math::Vec2(position.x, position.z), Math::Vec2(3, 0)), 2);
        EXPECT_LT(value("combat.health.player"), 120);
    }

    TEST_P(CombatDemoTest, HitsRejectActorsBehindThePlayerAndAcrossWalls) {
        arrange();
        place(guard, {3, 0, 7});
        button(true);
        ASSERT_TRUE(tick(45));
        EXPECT_EQ(value("combat.health.guard1"), 90);
        button(false);
        ASSERT_TRUE(tick(5));
        place(player, {-.91f, 0, 1});
        place(guard, {.91f, 0, 1});
        const auto visual = playing->find_entity(
            std::get<EntityUuid>(player.get_component<ScriptComponent>().parameters.at("visual")));
        visual.edit_transform([](auto& transform) { transform.rotation = {0, -90, 0}; });
        button(true);
        ASSERT_TRUE(tick(45));
        EXPECT_EQ(value("combat.health.guard1"), 90);
    }

    TEST_P(CombatDemoTest, PauseDefeatRestartAndStopKeepAuthoredContentUnchanged) {
        const auto original = serializer.serialize(*authored).value();
        arrange();
        ASSERT_TRUE(tick(3));
        button(true);
        ASSERT_TRUE(tick(6));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(tick(60));
        EXPECT_EQ(value("combat.health.guard1"), 90);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(tick());
        EXPECT_EQ(value("combat.health.guard1"), 90);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(tick(45));
        EXPECT_EQ(value("combat.health.guard1"), 55);
        for(int strike = 0; strike < 2; ++strike) {
            button(false);
            ASSERT_TRUE(tick());
            button(true);
            ASSERT_TRUE(tick(55));
        }
        EXPECT_EQ(value("combat.health.guard1"), 0);
        EXPECT_EQ(value("combat.defeated"), 1);
        place(second, {3, 0, 4.7f});
        for(int strike = 0; strike < 3; ++strike) {
            button(false);
            ASSERT_TRUE(tick());
            button(true);
            ASSERT_TRUE(tick(55));
        }
        EXPECT_EQ(value("combat.health.guard2"), 0);
        EXPECT_EQ(value("combat.defeated"), 2);
        button(false);
        button(true, true);
        ASSERT_TRUE(tick());
        EXPECT_TRUE(runtime.take_scene_request());
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(physics.get_statistics().characters, 0u);
        EXPECT_EQ(serializer.serialize(*authored).value(), original);
        ASSERT_NO_FATAL_FAILURE(start());
        EXPECT_EQ(value("combat.health.player"), 120);
        EXPECT_EQ(value("combat.health.guard1"), 90);
        EXPECT_EQ(value("combat.defeated"), 0);
        ASSERT_TRUE(tick(3));
        EXPECT_FALSE(runtime.take_scene_request());
    }

    INSTANTIATE_TEST_SUITE_P(KeyboardAndGamepad, CombatDemoTest, testing::Bool());
}
