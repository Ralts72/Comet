#include "input/runtime_input.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(RuntimeInputTest, PublishesCoherentStageSnapshotsWithoutConsumingOtherReaders) {
        auto actions =
            InputActions::create({{"jump", InputActions::Type::Button, {{Input::Key::Space}}},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(std::move(actions).value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.scroll_event({0, 3});
        const auto physical = input.publish_frame();
        runtime.prepare(&physical);
        const auto saved_update = runtime.update();
        const auto first_fixed = runtime.consume_fixed();
        for(const auto* state : {&saved_update, &first_fixed}) {
            EXPECT_EQ(state->physical().serial, physical.serial);
            EXPECT_TRUE(state->physical().key(Input::Key::Space).pressed);
            ASSERT_NE(state->action("jump"), nullptr);
            EXPECT_TRUE(state->action("jump")->pressed);
            EXPECT_EQ(state->physical().scroll.y, state->action("look")->value);
        }
        const auto& second_fixed = runtime.consume_fixed();
        EXPECT_FALSE(second_fixed.physical().key(Input::Key::Space).pressed);
        EXPECT_FALSE(second_fixed.action("jump")->pressed);
        EXPECT_EQ(second_fixed.physical().scroll.y, second_fixed.action("look")->value);
        EXPECT_FLOAT_EQ(second_fixed.action("look")->value, 0);
        EXPECT_TRUE(runtime.update().action("jump")->pressed);
        runtime.prepare(&physical);
        EXPECT_FALSE(runtime.update().physical().key(Input::Key::Space).pressed);
        EXPECT_FALSE(runtime.update().action("jump")->pressed);
        runtime.prepare(nullptr);
        EXPECT_FALSE(runtime.update().focused());
        EXPECT_TRUE(runtime.update().physical().key(Input::Key::Space).released);
        EXPECT_TRUE(runtime.update().action("jump")->released);
        EXPECT_TRUE(saved_update.physical().key(Input::Key::Space).pressed);
        EXPECT_TRUE(saved_update.action("jump")->pressed);
    }

    TEST(RuntimeInputTest, RebaseAndReconfigureDoNotLeaveHalfResetSnapshots) {
        auto actions =
            InputActions::create({{"jump", InputActions::Type::Button, {{Input::Key::Space}}},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(std::move(actions).value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.scroll_event({0, 2});
        runtime.rebase();
        runtime.prepare(&input.publish_frame(), true);
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_TRUE(state->physical().key(Input::Key::Space).down);
            EXPECT_TRUE(state->action("jump")->down);
            EXPECT_FALSE(state->physical().key(Input::Key::Space).pressed);
            EXPECT_FALSE(state->action("jump")->pressed);
            EXPECT_FLOAT_EQ(state->physical().scroll.y, 0);
            EXPECT_FLOAT_EQ(state->action("look")->value, 0);
        }
        runtime.discard();
        runtime.prepare(nullptr);
        EXPECT_TRUE(runtime.consume_fixed().action("jump")->released);
        runtime.configure({});
        EXPECT_EQ(runtime.update().action("jump"), nullptr);
        EXPECT_FALSE(runtime.update().focused());
        runtime.prepare(&input.publish_frame());
        EXPECT_EQ(runtime.update().action("jump"), nullptr);
        EXPECT_TRUE(runtime.update().physical().key(Input::Key::Space).down);
        runtime.reset();
        EXPECT_FALSE(runtime.update().physical().key(Input::Key::Space).down);
    }

    TEST(RuntimeInputTest, ContextTransitionsPreserveCommonInputAndRestoreDefaultsOnReset) {
        using Type = InputActions::Type;
        auto actions = InputActions::create(
            {{"jump", Type::Button, {{Input::Key::Space}}, "player"},
                {"look", Type::Delta, {{InputActions::Motion::ScrollY}}, "player"},
                {"restart", Type::Button, {{Input::Key::R}}},
                {"common_scroll", Type::Delta, {{InputActions::Motion::ScrollY}}}},
            {{"player"}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(std::move(actions).value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("jump")->pressed);

        ASSERT_TRUE(runtime.set_context_enabled("player", false));
        EXPECT_TRUE(runtime.update().action("jump")->down);
        input.key_event(Input::Key::R, true);
        input.scroll_event({0, 2});
        runtime.prepare(&input.publish_frame());
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_FALSE(state->action("jump")->down);
            EXPECT_TRUE(state->action("jump")->released);
            EXPECT_FLOAT_EQ(state->action("look")->value, 0);
            EXPECT_TRUE(state->action("restart")->pressed);
            EXPECT_FLOAT_EQ(state->action("common_scroll")->value, 2);
        }
        EXPECT_FALSE(runtime.consume_fixed().action("jump")->released);

        ASSERT_TRUE(runtime.set_context_enabled("player", true));
        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame());
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_TRUE(state->action("jump")->down);
            EXPECT_FALSE(state->action("jump")->pressed);
            EXPECT_FALSE(state->action("jump")->released);
            EXPECT_FLOAT_EQ(state->action("look")->value, 0);
            EXPECT_FLOAT_EQ(state->action("common_scroll")->value, 3);
        }
        EXPECT_FALSE(runtime.set_context_enabled("unknown", false));
        ASSERT_TRUE(runtime.set_context_enabled("player", true));
        input.scroll_event({0, 4});
        runtime.prepare(&input.publish_frame());
        EXPECT_FLOAT_EQ(runtime.update().action("look")->value, 4);
        ASSERT_TRUE(runtime.set_context_enabled("player", false));
        runtime.reset();
        input.key_event(Input::Key::Space, false);
        input.key_event(Input::Key::Space, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("jump")->pressed);
    }

    TEST(RuntimeInputTest, EnabledContextCollectsOnlyNewInputAcrossZeroAndMultipleFixedSteps) {
        using Type = InputActions::Type;
        auto actions = InputActions::create(
            {{"jump", Type::Button, {{Input::Key::Space}}, "player"},
                {"look", Type::Delta, {{InputActions::Motion::ScrollY}}, "player"},
                {"common_jump", Type::Button, {{Input::Key::Space}}},
                {"common_scroll", Type::Delta, {{InputActions::Motion::ScrollY}}}},
            {{"player", false}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(std::move(actions).value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.scroll_event({0, 2});
        runtime.prepare(&input.publish_frame());

        ASSERT_TRUE(runtime.set_context_enabled("player", true));
        input.key_event(Input::Key::Space, true);
        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("jump")->down);
        EXPECT_FALSE(runtime.update().action("jump")->pressed);
        input.key_event(Input::Key::Space, false);
        input.scroll_event({0, 4});
        runtime.prepare(&input.publish_frame());
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.scroll_event({0, 5});
        const auto physical = input.publish_frame();
        runtime.prepare(&physical);
        const auto& first = runtime.consume_fixed();
        EXPECT_TRUE(first.action("jump")->pressed);
        EXPECT_TRUE(first.action("jump")->released);
        EXPECT_FALSE(first.action("jump")->down);
        EXPECT_FLOAT_EQ(first.action("look")->value, 9);
        EXPECT_TRUE(first.action("common_jump")->pressed);
        EXPECT_FLOAT_EQ(first.action("common_scroll")->value, 14);
        EXPECT_TRUE(runtime.update().action("jump")->pressed);
        const auto& second = runtime.consume_fixed();
        EXPECT_FALSE(second.action("jump")->pressed);
        EXPECT_FALSE(second.action("jump")->released);
        EXPECT_FLOAT_EQ(second.action("look")->value, 0);
        runtime.prepare(&physical);
        EXPECT_FALSE(runtime.update().action("jump")->pressed);
        EXPECT_FLOAT_EQ(runtime.consume_fixed().action("look")->value, 0);
    }

    TEST(RuntimeInputTest, PausedContextChangesEstablishBaselinesWithoutDroppingFutureInput) {
        auto actions = InputActions::create(
            {{"jump", InputActions::Type::Button, {{Input::Key::Space}}, "player"},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}, "player"}},
            {{"player", false}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(std::move(actions).value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.scroll_event({0, 2});
        ASSERT_TRUE(runtime.set_context_enabled("player", true));
        runtime.prepare(&input.publish_frame(), true);
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_TRUE(state->action("jump")->down);
            EXPECT_FALSE(state->action("jump")->pressed);
            EXPECT_FLOAT_EQ(state->action("look")->value, 0);
        }
        runtime.rebase();
        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame());
        EXPECT_FLOAT_EQ(runtime.consume_fixed().action("look")->value, 0);
        input.key_event(Input::Key::Space, false);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("jump")->released);
        input.key_event(Input::Key::Space, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("jump")->pressed);
    }

    TEST(RuntimeInputTest, DiscardAndDeviceDisconnectCannotReplayPendingActionTransients) {
        auto actions = InputActions::create(
            {{"jump", InputActions::Type::Button, {{Input::GamepadButton::South}}},
                {"mixed", InputActions::Type::Button,
                    {{Input::Key::Space}, {Input::GamepadButton::South}}},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(std::move(actions).value());
        Input input;
        input.focus_event(true);
        Input::GamepadSample pad;
        pad.buttons[size_t(Input::GamepadButton::South)] = true;
        input.gamepad_sample(0, pad);
        input.scroll_event({0, 2});
        runtime.prepare(&input.publish_frame());
        input.gamepad_sample(0, std::nullopt);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().physical().key(Input::Key::Space).pressed);
        EXPECT_FALSE(runtime.update().action("mixed")->pressed);
        const auto& disconnected = runtime.consume_fixed();
        EXPECT_FALSE(disconnected.action("jump")->pressed);
        EXPECT_TRUE(disconnected.action("mixed")->pressed);
        input.gamepad_sample(0, pad);
        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame());
        runtime.discard();
        runtime.prepare(&input.publish_frame());
        const auto& regained = runtime.consume_fixed();
        EXPECT_FALSE(regained.action("jump")->pressed);
        EXPECT_FALSE(regained.action("mixed")->pressed);
        EXPECT_FLOAT_EQ(regained.action("look")->value, 0);
    }
}
