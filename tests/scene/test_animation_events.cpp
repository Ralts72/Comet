#include "animation/animation_system.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    class AnimationEventsTest: public testing::Test {
    protected:
        AssetRegistry assets;
        Scene scene;
        SceneRuntime runtime;
        std::shared_ptr<AnimationData> data = std::make_shared<AnimationData>();
        std::shared_ptr<AnimationController> controller = std::make_shared<AnimationController>();
        Entity animated = scene.create_entity("Animated");
        void SetUp() override {
            data->clips = {{.name = "idle", .duration = 1}, {.name = "walk", .duration = 1}};
            controller->parameters = {{"speed", 0.f}};
            controller->states[0].events = {{0, "step", std::string("start")},
                {.25f, "step", std::string("left")}, {.75f, "step", std::string("right")}};
            animated.add_component<MeshRendererComponent>();
            animated.add_component<AnimatorComponent>(
                AnimatorComponent{.controller = AssetHandle{42}});
            ASSERT_TRUE(runtime.set_settings({.max_frame_delta = 1}));
        }
        void add_animation(bool listen = true) {
            if(listen) {
                auto observer = Script::create(R"(return {events = {step = "on_step"},
                    on_step = function(self, value)
                        comet.session_set("events", (comet.session_get("events") or "") .. value .. ",")
                    end})",
                    "observer.lua");
                ASSERT_TRUE(observer) << observer.error().message;
                ASSERT_TRUE(assets.register_asset(AssetHandle{43}, observer.value()));
                animated.add_component<ScriptComponent>().asset = AssetHandle{43};
                ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
            }
            ASSERT_TRUE(runtime.add_system(std::make_unique<AnimationSystem>(
                [this](AssetHandle) { return data; }, [this](AssetHandle) { return controller; })));
        }
        std::vector<std::string> events() {
            std::vector<std::string> values;
            const auto events = runtime.get_session().get_value("events");
            if(!events)
                return values;
            const auto& text = std::get<std::string>(*events);
            std::size_t begin = 0;
            for(auto end = text.find(','); end != std::string::npos; end = text.find(',', begin)) {
                values.push_back(text.substr(begin, end - begin));
                begin = end + 1;
            }
            EXPECT_TRUE(runtime.get_session().erase_value("events"));
            return values;
        }
    };
    TEST_F(AnimationEventsTest,
        CrossesMultipleLoopsInOrderWithoutLosingTheEndpointOrRefiringZeroDelta) {
        add_animation();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_TRUE(events().empty());
        animated.edit_component<AnimatorComponent>([](auto& value) { value.speed = 4; });
        ASSERT_TRUE(runtime.advance(.6));
        EXPECT_EQ(events(), (std::vector<std::string>{"start", "left", "right", "start", "left",
                                "right", "start", "left"}));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_TRUE(events().empty());
        ASSERT_TRUE(runtime.advance(.0875));
        EXPECT_EQ(events(), (std::vector<std::string>{"right"}));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        const auto before = scene.get_animation_playback(animated);
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_TRUE(events().empty());
        EXPECT_EQ(scene.get_animation_playback(animated), before);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_GT(scene.get_animation_playback(animated)->current.time, before->current.time);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.001));
        EXPECT_EQ(events(), (std::vector<std::string>{"start"}));
    }
    TEST_F(AnimationEventsTest, SharedBlendPhaseFiresOnceAndOutgoingStateStopsEmitting) {
        controller->states[0].motions = {{0, "idle"}, {1, "walk"}};
        controller->states[0].parameter = "speed";
        controller->states.push_back({"other", {{0, "walk"}}, {}, 0, false,
            {{.25f, "step", std::string("other")}, {1, "step", std::string("end")}}});
        controller->transitions = {{"idle", "other", 1, false,
            {{"speed", AnimationController::Condition::Compare::Greater, .9f}}}};
        animated.edit_component<AnimatorComponent>(
            [](auto& value) { value.parameters["speed"] = .5f; });
        add_animation();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.25));
        EXPECT_EQ(events(), (std::vector<std::string>{"start", "left"}));
        animated.edit_component<AnimatorComponent>(
            [](auto& value) { value.parameters["speed"] = 1.f; });
        ASSERT_TRUE(runtime.advance(.25));
        EXPECT_LT(scene.get_animation_playback(animated)->blend, 1);
        EXPECT_EQ(events(), (std::vector<std::string>{"other"}));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(events(), (std::vector<std::string>{"end"}));
        ASSERT_TRUE(runtime.advance(.5));
        EXPECT_TRUE(events().empty());
    }
    TEST_F(AnimationEventsTest,
        LuaReceivesOnlyItsOwnEventsInTheSameUpdateRegardlessOfRegistrationOrder) {
        ASSERT_TRUE(assets.register_asset(AssetHandle{42}, controller));
        auto script = Script::create(R"(local script = {events = {step = "on_step"}}
            function script:on_step(value)
                assert(comet.get_animation_parameter(comet.self_entity(), "speed") == 0)
                comet.translate(1, 0, 0)
            end
            return script)",
            "events.lua");
        ASSERT_TRUE(script) << script.error().message;
        ASSERT_TRUE(assets.register_asset(AssetHandle{43}, script.value()));
        animated.add_component<ScriptComponent>().asset = AssetHandle{43};
        auto unrelated = scene.create_entity("Unrelated");
        unrelated.add_component<ScriptComponent>().asset = AssetHandle{43};
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        add_animation(false);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.3));
        EXPECT_EQ(animated.get_component<TransformComponent>().translation.x, 2);
        EXPECT_EQ(unrelated.get_component<TransformComponent>().translation.x, 0);
    }
    TEST_F(AnimationEventsTest, RoundTripsTypedMarkersAndRejectsUnorderedOrInvalidEvents) {
        controller->states[0].events = {
            {.1f, "text", std::string("left")}, {.2f, "flag", true}, {.3f, "number", 2.f}};
        const AnimationControllerSerializer serializer;
        const auto text = serializer.serialize(*controller);
        ASSERT_TRUE(text) << text.error();
        auto restored = serializer.deserialize(text.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value(), *controller);
        controller->states[0].events[1].phase = .05f;
        EXPECT_FALSE(controller->validate());
        controller->states[0].events[1].phase = .2f;
        controller->states[0].events[1].value = Math::Vec3(1);
        EXPECT_FALSE(controller->validate());
        controller->states[0].events[1].value = true;
        controller->states[0].events[1].phase = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(controller->validate());
    }
    TEST_F(AnimationEventsTest, LoopEndAndStartAreDistinctAndZeroSpeedDoesNotDispatch) {
        controller->states[0].events = {
            {0, "step", std::string("start")}, {1, "step", std::string("end")}};
        add_animation();
        ASSERT_TRUE(runtime.start(scene));
        animated.edit_component<AnimatorComponent>([](auto& value) { value.speed = 0; });
        ASSERT_TRUE(runtime.advance(.2));
        EXPECT_TRUE(events().empty());
        animated.edit_component<AnimatorComponent>([](auto& value) { value.speed = 1; });
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(events(), (std::vector<std::string>{"start", "end", "start"}));
        ASSERT_TRUE(runtime.advance(.001));
        EXPECT_TRUE(events().empty());
    }
}
