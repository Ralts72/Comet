#include "animation/animation_system.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/component_registry.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include "scripting/script_instance.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        AnimationController door_controller() {
            AnimationController data;
            data.parameters = {{"open", false}, {"speed", 0.f}};
            data.states = {{"idle", {{0, "idle"}}}, {"opening", {{0, "open"}}, {}, 0, false}};
            using Compare = AnimationController::Condition::Compare;
            data.transitions = {{"idle", "opening", 0.1f, false, {{"open", Compare::Equal, true}}},
                {"opening", "idle", 0.1f, true, {{"open", Compare::Equal, false}}}};
            return data;
        }
        std::shared_ptr<AnimationData> clips() {
            auto data = std::make_shared<AnimationData>();
            data->clips = {{.name = "idle", .duration = 1}, {.name = "open", .duration = .25f}};
            return data;
        }
    }
    TEST(AnimationControllerTest,
        SerializesConditionsAndRejectsInvalidReferencesAndBlendThresholds) {
        auto data = door_controller();
        const AnimationControllerSerializer serializer;
        const auto text = serializer.serialize(data);
        ASSERT_TRUE(text) << text.error();
        const auto restored = serializer.deserialize(text.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value(), data);
        data.transitions[0].conditions[0].value = 1.f;
        EXPECT_FALSE(data.validate());
        data = door_controller();
        data.transitions[0].to = "missing";
        EXPECT_FALSE(data.validate());
        data = door_controller();
        data.states[0].parameter = "speed";
        data.states[0].motions = {{2, "idle"}, {1, "open"}};
        EXPECT_FALSE(data.validate());
        EXPECT_FALSE(door_controller().validate_parameters({{"unknown", 0.f}}));
        EXPECT_FALSE(door_controller().validate_parameters({{"open", 1.f}}));
        EXPECT_FALSE(
            serializer.deserialize(R"({"parameters":{},"states":[],"transitions":[],"typo":1})"));
        for(const auto* value : {R"({"open":true,"open":false})", R"({"open":"yes"})",
                R"({"open":[1,2,3]})", R"({"open":{}})", R"({"open":1e100})"}) {
            auto invalid = text.value();
            const auto start = invalid.find('{', invalid.find("\"parameters\""));
            invalid.replace(start, invalid.find('}', start) - start + 1, value);
            EXPECT_FALSE(serializer.deserialize(invalid)) << value;
        }
    }
    TEST(AnimationControllerTest,
        RunsWithoutCharacterAndKeepsInstancesPauseAndSnapshotsIndependent) {
        const auto definition = std::make_shared<AnimationController>(door_controller());
        const auto data = clips();
        Scene scene;
        auto first = scene.create_entity("Door one");
        auto second = scene.create_entity("Door two");
        for(auto entity : {first, second}) {
            entity.add_component<MeshRendererComponent>();
            entity.add_component<AnimatorComponent>(
                AnimatorComponent{.controller = AssetHandle{42}});
        }
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; },
                [definition](AssetHandle) { return definition; })));
        ASSERT_TRUE(runtime.start(scene));
        const auto captured = scene.get_animation_playback(first);
        first.edit_component<AnimatorComponent>(
            [](auto& animator) { animator.parameters["open"] = true; });
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_EQ(scene.get_animation_playback(first)->current.clip, "open");
        EXPECT_EQ(scene.get_animation_playback(second)->current.clip, "idle");
        EXPECT_EQ(captured->current.clip, "idle");
        EXPECT_EQ(captured->state, "idle");
        EXPECT_EQ(scene.get_animation_playback(first)->state, "opening");
        first.edit_component<AnimatorComponent>(
            [](auto& animator) { animator.parameters["open"] = false; });
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_EQ(scene.get_animation_playback(first)->current.clip, "open");
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        const auto paused = scene.get_animation_playback(first);
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(scene.get_animation_playback(first), paused);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_GT(scene.get_animation_playback(first)->current.time, paused->current.time);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(.1));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.get_animation_playback(first)->current.clip, "idle");
        const auto components = create_scene_component_registry();
        auto cloned = SceneSerializer(components).clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_EQ(cloned.value()->find_entity(first.get_uuid()).get_component<AnimatorComponent>(),
            first.get_component<AnimatorComponent>());
        EXPECT_FALSE(
            cloned.value()->get_animation_playback(cloned.value()->find_entity(first.get_uuid())));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(first));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(scene.get_animation_playback(first)->current.clip, "idle");
        EXPECT_DOUBLE_EQ(scene.get_animation_playback(first)->current.time, 0);
    }
    TEST(AnimationControllerTest,
        CompletionUsesProgressAcrossLoopBoundaryAndTransitionsRespectPriority) {
        auto definition = std::make_shared<AnimationController>(door_controller());
        definition->states[1].loop = true;
        definition->transitions[1].conditions.clear();
        definition->transitions.push_back({"opening", "idle", 0, false, {}});
        EXPECT_EQ(definition->next_transition("opening", {}, true), &definition->transitions[1]);
        definition->transitions.pop_back();
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<MeshRendererComponent>();
        entity.add_component<AnimatorComponent>(
            AnimatorComponent{.controller = AssetHandle{42}, .parameters = {{"open", true}}});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([](AssetHandle) { return clips(); },
                [definition](AssetHandle) { return definition; })));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.2));
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_NEAR(scene.get_animation_playback(entity)->current.time, .05, .00001);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.get_animation_playback(entity)->current.clip, "idle");
    }
    TEST(AnimationControllerTest, LuaDrivesTypedParametersAndReportsUnknownNames) {
        AssetRegistry assets;
        const auto definition = std::make_shared<AnimationController>(door_controller());
        ASSERT_TRUE(assets.register_asset(AssetHandle{42}, definition));
        auto script = Script::create(R"(return {on_start = function(self)
            local entity = comet.self_entity()
            comet.set_animation_parameter(entity, "open", true)
        end})",
            "controller.lua");
        ASSERT_TRUE(script) << script.error().message;
        ASSERT_TRUE(assets.register_asset(AssetHandle{43}, script.value()));
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<MeshRendererComponent>();
        entity.add_component<AnimatorComponent>(AnimatorComponent{.controller = AssetHandle{42}});
        entity.add_component<ScriptComponent>().asset = AssetHandle{43};
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([](AssetHandle) { return clips(); },
                [definition](AssetHandle) { return definition; })));
        const auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        EXPECT_EQ(
            entity.get_component<AnimatorComponent>().parameters.at("open"), ParameterValue(true));
        EXPECT_EQ(scene.get_animation_playback(entity)->current.clip, "open");
        for(const auto* arguments : {R"("open", 1)", R"("typo", true)"}) {
            auto invalid = Script::create(
                std::string("return {on_start = function() ")
                    + "comet.set_animation_parameter(comet.self_entity(), " + arguments + ") end}",
                "invalid-controller.lua");
            ASSERT_TRUE(invalid) << invalid.error().message;
            auto instance = ScriptInstance::create(*invalid.value());
            ASSERT_TRUE(instance) << instance.error().message;
            const auto result = instance.value()->invoke(
                ScriptInstance::Phase::Start, entity, {}, {.scene = &scene, .assets = &assets});
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("Unknown or mistyped animation parameter"),
                std::string::npos);
        }
    }
}
