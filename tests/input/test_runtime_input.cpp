#include "input/runtime_input.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

namespace Comet::Tests {
    namespace {
        Result<InputActions> persistent_actions(std::vector<InputActions::Action> actions,
            std::vector<InputActions::Context> contexts = {}) {
            for(auto& action : actions) {
                action.id = Uuid::generate();
                for(auto& binding : action.bindings)
                    binding.id = Uuid::generate();
            }
            return InputActions::create(std::move(actions), std::move(contexts));
        }
    }

    TEST(RuntimeInputTest, RebindingPreservesPendingByIdentityAcrossActionAndBindingReorder) {
        using Type = InputActions::Type;
        const auto actions =
            persistent_actions({{"jump", Type::Button, {{Input::Key::Space}, {Input::Key::R}}},
                {"look", Type::Delta,
                    {{InputActions::Motion::ScrollY}, {InputActions::Motion::ScrollX}}}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.key_event(Input::Key::R, true);
        input.scroll_event({2, 3});
        runtime.prepare(&input.publish_frame());
        auto reordered = actions.value().actions();
        std::ranges::reverse(reordered);
        for(auto& action : reordered)
            std::ranges::reverse(action.bindings);
        const auto replacement = InputActions::create(std::move(reordered));
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime.request_rebind(replacement.value()));
        EXPECT_TRUE(runtime.update().action("jump")->pressed);
        EXPECT_FLOAT_EQ(runtime.update().action("look")->value, 5);
        runtime.prepare(&input.publish_frame());
        const auto first = runtime.consume_fixed();
        EXPECT_TRUE(first.action("jump")->down);
        EXPECT_TRUE(first.action("jump")->pressed);
        EXPECT_FLOAT_EQ(first.action("look")->value, 5);
        EXPECT_FALSE(runtime.consume_fixed().action("jump")->pressed);
        EXPECT_FLOAT_EQ(runtime.consume_fixed().action("look")->value, 0);
        EXPECT_TRUE(runtime.update().action("jump")->down);
        EXPECT_FALSE(runtime.update().action("jump")->pressed);
        input.key_event(Input::Key::R, false);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("jump")->released);
        EXPECT_TRUE(runtime.consume_fixed().action("jump")->released);
    }

    TEST(RuntimeInputTest, RebindingChangesOnlyAffectedBindingHistoryAndBaselinesNewBindings) {
        using Type = InputActions::Type;
        const auto actions =
            persistent_actions({{"jump", Type::Button, {{Input::Key::Space}, {Input::Key::R}}},
                {"look", Type::Delta,
                    {{InputActions::Motion::ScrollY}, {InputActions::Motion::ScrollX}}}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        for(const auto key : {Input::Key::Space, Input::Key::R}) {
            input.key_event(key, true);
            input.key_event(key, false);
        }
        input.scroll_event({2, 3});
        runtime.prepare(&input.publish_frame());
        auto edited = actions.value().actions();
        edited[0].bindings[0].control = Input::Key::J;
        edited[0].bindings.push_back({Input::MouseButton::Left, 1, 0, Uuid::generate()});
        edited[1].bindings[0].scale = 2;
        const auto replacement = InputActions::create(std::move(edited));
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime.request_rebind(replacement.value()));
        input.key_event(Input::Key::J, true);
        input.mouse_button_event(Input::MouseButton::Left, true);
        input.scroll_event({4, 5});
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("jump")->down);
        EXPECT_FALSE(runtime.update().action("jump")->pressed);
        EXPECT_FLOAT_EQ(runtime.update().action("look")->value, 4);
        const auto first = runtime.consume_fixed();
        EXPECT_TRUE(first.action("jump")->pressed);
        EXPECT_FLOAT_EQ(first.action("look")->value, 6);
        EXPECT_FALSE(runtime.consume_fixed().action("jump")->pressed);
        input.key_event(Input::Key::J, false);
        input.mouse_button_event(Input::MouseButton::Left, false);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("jump")->released);
        EXPECT_TRUE(runtime.consume_fixed().action("jump")->released);
        input.key_event(Input::Key::J, true);
        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("jump")->pressed);
        EXPECT_FLOAT_EQ(runtime.consume_fixed().action("look")->value, 6);
    }

    TEST(RuntimeInputTest, RebindingReleasesOnlyStagesThatObservedTheRemovedHeldSource) {
        for(const bool fixed_observed : {false, true}) {
            for(const bool unbind : {false, true}) {
                SCOPED_TRACE(fixed_observed);
                SCOPED_TRACE(unbind);
                const auto actions = persistent_actions(
                    {{"jump", InputActions::Type::Button, {{Input::Key::Space}}}});
                ASSERT_TRUE(actions);
                RuntimeInput runtime;
                runtime.configure(actions.value());
                Input input;
                input.focus_event(true);
                input.key_event(Input::Key::Space, true);
                runtime.prepare(&input.publish_frame());
                if(fixed_observed)
                    EXPECT_TRUE(runtime.consume_fixed().action("jump")->down);
                auto edited = actions.value().actions();
                if(unbind)
                    edited[0].bindings.clear();
                else
                    edited[0].bindings[0].control = Input::Key::J;
                const auto replacement = InputActions::create(std::move(edited));
                ASSERT_TRUE(replacement);
                ASSERT_TRUE(runtime.request_rebind(replacement.value()));
                EXPECT_TRUE(runtime.update().action("jump")->down);
                runtime.prepare(&input.publish_frame());
                EXPECT_TRUE(runtime.update().action("jump")->released);
                const auto fixed = runtime.consume_fixed();
                EXPECT_FALSE(fixed.action("jump")->down);
                EXPECT_FALSE(fixed.action("jump")->pressed);
                EXPECT_EQ(fixed.action("jump")->released, fixed_observed);
                EXPECT_FALSE(runtime.consume_fixed().action("jump")->released);
            }
        }
    }

    TEST(RuntimeInputTest, RebindingRequestsAreLastWinsAndRoundTripsDoNotDisturbPendingInput) {
        const auto actions = persistent_actions(
            {{"jump", InputActions::Type::Button, {{Input::Key::Space}}, "player"},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}}},
            {{"player"}});
        ASSERT_TRUE(actions);
        auto edited = actions.value().actions();
        edited[0].bindings[0].control = Input::Key::J;
        const auto first = InputActions::create(edited, actions.value().contexts());
        edited[0].bindings[0].control = Input::Key::K;
        const auto last = InputActions::create(edited, actions.value().contexts());
        ASSERT_TRUE(first);
        ASSERT_TRUE(last);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.request_rebind(first.value()));
        ASSERT_TRUE(runtime.request_rebind(last.value()));
        ASSERT_TRUE(runtime.request_rebind(actions.value()));
        input.scroll_event({0, 2});
        runtime.prepare(&input.publish_frame());
        const auto round_trip = runtime.consume_fixed();
        EXPECT_TRUE(round_trip.action("jump")->pressed);
        EXPECT_TRUE(round_trip.action("jump")->released);
        EXPECT_FLOAT_EQ(round_trip.action("look")->value, 5);

        ASSERT_TRUE(runtime.request_rebind(first.value()));
        ASSERT_TRUE(runtime.request_rebind(last.value()));
        for(int change = 0; change < 8; ++change) {
            SCOPED_TRACE(change);
            auto invalid = actions.value().actions();
            auto contexts = actions.value().contexts();
            switch(change) {
                case 0:
                    invalid[0].id = Uuid::generate();
                    break;
                case 1:
                    invalid[0].name = "renamed";
                    break;
                case 2:
                    invalid[0].type = InputActions::Type::Axis;
                    break;
                case 3:
                    invalid[0].context.clear();
                    break;
                case 4:
                    contexts[0].priority = 10;
                    break;
                case 5:
                    invalid.pop_back();
                    break;
                case 6:
                    invalid[0].id = {};
                    break;
                case 7:
                    invalid[0].bindings[0].id = {};
                    break;
            }
            const auto candidate = InputActions::create(std::move(invalid), std::move(contexts));
            ASSERT_TRUE(candidate);
            EXPECT_FALSE(runtime.request_rebind(candidate.value()));
        }
        input.key_event(Input::Key::K, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("jump")->down);
        EXPECT_FALSE(runtime.update().action("jump")->pressed);
        input.key_event(Input::Key::K, false);
        runtime.prepare(&input.publish_frame());
        input.key_event(Input::Key::J, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_FALSE(runtime.update().action("jump")->down);
        input.key_event(Input::Key::K, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("jump")->pressed);

        const auto anonymous = InputActions::create(
            {{"jump", InputActions::Type::Button, {{Input::Key::Space}}, "player"},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}}},
            {{"player"}});
        ASSERT_TRUE(anonymous);
        runtime.configure(anonymous.value());
        EXPECT_FALSE(runtime.request_rebind(actions.value()));
    }

    TEST(RuntimeInputTest, RebindingBaselinesWaitForAvailableDevicesEvenWhenPausedOrReordered) {
        using Type = InputActions::Type;
        const auto actions = persistent_actions({{"key", Type::Button, {{Input::Key::B}}},
            {"mouse", Type::Button, {{Input::MouseButton::Right}}},
            {"pad", Type::Button, {{Input::GamepadButton::East}}},
            {"look", Type::Delta, {{InputActions::Motion::ScrollX}}}});
        ASSERT_TRUE(actions);
        auto edited = actions.value().actions();
        edited[0].bindings[0].control = Input::Key::J;
        edited[1].bindings[0].control = Input::MouseButton::Left;
        edited[2].bindings[0].control = Input::GamepadButton::South;
        edited[3].bindings[0].control = InputActions::Motion::ScrollY;
        const auto replacement = InputActions::create(edited);
        std::ranges::reverse(edited);
        const auto reordered = InputActions::create(std::move(edited));
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(reordered);
        for(const bool paused : {false, true}) {
            SCOPED_TRACE(paused);
            RuntimeInput runtime;
            runtime.configure(actions.value());
            ASSERT_TRUE(runtime.request_rebind(replacement.value()));
            runtime.prepare(nullptr, paused);
            ASSERT_TRUE(runtime.request_rebind(reordered.value()));
            Input::Frame frame{.serial = 1};
            frame.keys[size_t(Input::Key::J)] = {.down = true, .pressed = true};
            frame.mouse_buttons[size_t(Input::MouseButton::Left)] = {.down = true, .pressed = true};
            frame.gamepads[0].buttons[size_t(Input::GamepadButton::South)] = {
                .down = true, .pressed = true};
            frame.scroll.y = 5;
            runtime.prepare(&frame, paused);
            frame.serial++;
            frame.focused = true;
            frame.pointer_enabled = false;
            runtime.prepare(&frame, paused);
            EXPECT_TRUE(runtime.update().action("key")->down);
            EXPECT_FALSE(runtime.update().action("key")->pressed);
            EXPECT_FALSE(runtime.update().action("mouse")->down);
            EXPECT_FALSE(runtime.update().action("pad")->down);
            EXPECT_FALSE(runtime.consume_fixed().action("key")->pressed);
            frame.serial++;
            frame.pointer_enabled = true;
            frame.keys[size_t(Input::Key::J)].pressed = false;
            runtime.prepare(&frame, paused);
            EXPECT_TRUE(runtime.update().action("mouse")->down);
            EXPECT_FALSE(runtime.update().action("mouse")->pressed);
            EXPECT_FLOAT_EQ(runtime.update().action("look")->value, 0);
            EXPECT_FALSE(runtime.consume_fixed().action("mouse")->pressed);
            frame.serial++;
            frame.gamepads[0].connected = true;
            frame.mouse_buttons[size_t(Input::MouseButton::Left)].pressed = false;
            frame.scroll.y = 2;
            runtime.prepare(&frame, paused);
            EXPECT_TRUE(runtime.update().action("pad")->down);
            EXPECT_FALSE(runtime.update().action("pad")->pressed);
            EXPECT_FLOAT_EQ(runtime.update().action("look")->value, paused ? 0 : 2);
            EXPECT_FALSE(runtime.consume_fixed().action("pad")->pressed);

            frame.serial++;
            frame.keys[size_t(Input::Key::J)] = {.released = true};
            frame.mouse_buttons[size_t(Input::MouseButton::Left)] = {.released = true};
            frame.gamepads[0].buttons[size_t(Input::GamepadButton::South)] = {.released = true};
            frame.scroll = {};
            runtime.prepare(&frame);
            static_cast<void>(runtime.consume_fixed());
            frame.serial++;
            frame.keys[size_t(Input::Key::J)] = {.down = true, .pressed = true};
            frame.mouse_buttons[size_t(Input::MouseButton::Left)] = {.down = true, .pressed = true};
            frame.gamepads[0].buttons[size_t(Input::GamepadButton::South)] = {
                .down = true, .pressed = true};
            runtime.prepare(&frame);
            const auto fixed = runtime.consume_fixed();
            for(const auto name : {"key", "mouse", "pad"}) {
                EXPECT_TRUE(runtime.update().action(name)->pressed);
                EXPECT_TRUE(fixed.action(name)->pressed);
            }
        }
    }

    TEST(RuntimeInputTest, RebindingConsumerRoutesAffectsOtherActionsButPreservesDynamicContexts) {
        using Type = InputActions::Type;
        const auto actions = persistent_actions(
            {{"game", Type::Button, {{Input::Key::Space}}, "gameplay"},
                {"look", Type::Delta, {{InputActions::Motion::ScrollY}}, "gameplay"},
                {"menu", Type::Button, {{Input::Key::Enter}}, "menu"},
                {"menu_scroll", Type::Delta, {{InputActions::Motion::ScrollX}}, "menu"},
                {"common", Type::Button, {{Input::Key::Space}}},
                {"common_scroll", Type::Delta, {{InputActions::Motion::ScrollY}}},
                {"sleeping", Type::Button, {{Input::Key::R}}, "sleeping"}},
            {{"gameplay"}, {"menu", true, 10, true}, {"sleeping"}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        ASSERT_TRUE(runtime.set_context_enabled("sleeping", false));
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("game")->down);
        input.scroll_event({0, 4});
        runtime.prepare(&input.publish_frame());
        auto edited = actions.value().actions();
        edited[2].bindings[0].control = Input::Key::Space;
        edited[3].bindings[0].control = InputActions::Motion::ScrollY;
        const auto replacement =
            InputActions::create(std::move(edited), actions.value().contexts());
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime.request_rebind(replacement.value()));
        input.key_event(Input::Key::R, true);
        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame());
        const auto consumed = runtime.consume_fixed();
        for(const auto* state : {&runtime.update(), &consumed}) {
            EXPECT_TRUE(state->action("game")->released);
            EXPECT_FLOAT_EQ(state->action("look")->value, 0);
            EXPECT_TRUE(state->action("menu")->down);
            EXPECT_FALSE(state->action("menu")->pressed);
            EXPECT_FLOAT_EQ(state->action("menu_scroll")->value, 0);
            EXPECT_TRUE(state->action("common")->down);
            EXPECT_FALSE(state->action("common")->released);
            EXPECT_FALSE(state->action("sleeping")->down);
        }
        EXPECT_FLOAT_EQ(consumed.action("common_scroll")->value, 7);
        ASSERT_TRUE(runtime.request_rebind(actions.value()));
        input.scroll_event({0, 5});
        runtime.prepare(&input.publish_frame());
        const auto regained = runtime.consume_fixed();
        EXPECT_TRUE(regained.action("game")->down);
        EXPECT_FALSE(regained.action("game")->pressed);
        EXPECT_FLOAT_EQ(regained.action("look")->value, 0);
        EXPECT_TRUE(regained.action("menu")->released);
        EXPECT_FLOAT_EQ(regained.action("common_scroll")->value, 5);
        EXPECT_FALSE(regained.action("sleeping")->down);
        runtime.reset();
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("sleeping")->down);
    }

    TEST(RuntimeInputTest, IndirectRouteGainsFromRebindingWaitForAuthorizedInput) {
        using Type = InputActions::Type;
        const auto actions = persistent_actions(
            {{"look", Type::Delta, {{InputActions::Motion::ScrollY}}, "gameplay"},
                {"menu", Type::Delta, {{InputActions::Motion::ScrollY}}, "menu"}},
            {{"gameplay"}, {"menu", true, 10, true}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        auto edited = actions.value().actions();
        edited[1].bindings[0].control = InputActions::Motion::ScrollX;
        const auto replacement =
            InputActions::create(std::move(edited), actions.value().contexts());
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime.request_rebind(replacement.value()));
        runtime.prepare(nullptr);
        Input::Frame frame{.serial = 1, .focused = true, .pointer_enabled = false};
        frame.scroll.y = 5;
        runtime.prepare(&frame);
        frame.serial++;
        frame.pointer_enabled = true;
        frame.scroll.y = 7;
        runtime.prepare(&frame);
        EXPECT_FLOAT_EQ(runtime.update().action("look")->value, 0);
        EXPECT_FLOAT_EQ(runtime.consume_fixed().action("look")->value, 0);
        frame.serial++;
        frame.scroll.y = 8;
        runtime.prepare(&frame);
        EXPECT_FLOAT_EQ(runtime.update().action("look")->value, 8);
        EXPECT_FLOAT_EQ(runtime.consume_fixed().action("look")->value, 8);
    }

    TEST(RuntimeInputTest, RebindingCannotReviveRolledBackDiscardedOrDuplicateFrameHistory) {
        const auto actions =
            persistent_actions({{"jump", InputActions::Type::Button, {{Input::Key::Space}}},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input::Frame frame{.serial = 10, .focused = true};
        frame.keys[size_t(Input::Key::Space)] = {.pressed = true, .released = true};
        frame.scroll.y = 3;
        runtime.prepare(&frame);
        auto edited = actions.value().actions();
        edited[0].bindings[0].control = Input::Key::J;
        const auto replacement = InputActions::create(edited);
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime.request_rebind(replacement.value()));
        frame = {.serial = 1, .focused = true};
        frame.keys[size_t(Input::Key::J)] = {.down = true, .pressed = true};
        frame.scroll.y = 5;
        runtime.prepare(&frame);
        const auto restarted = runtime.consume_fixed();
        EXPECT_FALSE(restarted.action("jump")->pressed);
        EXPECT_FLOAT_EQ(restarted.action("look")->value, 5);
        runtime.prepare(&frame);
        EXPECT_FLOAT_EQ(runtime.consume_fixed().action("look")->value, 0);
        edited[0].bindings[0].control = Input::MouseButton::Left;
        const auto mouse = InputActions::create(std::move(edited));
        ASSERT_TRUE(mouse);
        ASSERT_TRUE(runtime.request_rebind(mouse.value()));
        runtime.discard();
        runtime.prepare(nullptr);
        frame = {.serial = 2, .focused = true};
        frame.mouse_buttons[size_t(Input::MouseButton::Left)] = {.down = true, .pressed = true};
        frame.scroll.y = 2;
        runtime.prepare(&frame);
        const auto after_discard = runtime.consume_fixed();
        EXPECT_TRUE(after_discard.action("jump")->down);
        EXPECT_FALSE(after_discard.action("jump")->pressed);
        EXPECT_FLOAT_EQ(after_discard.action("look")->value, 2);
    }

    TEST(RuntimeInputTest, ResetCancelsPendingRebindingRetainsAppliedBindingsAndConfigureWins) {
        const auto actions =
            persistent_actions({{"jump", InputActions::Type::Button, {{Input::Key::Space}}}});
        ASSERT_TRUE(actions);
        auto edited = actions.value().actions();
        edited[0].bindings[0].control = Input::Key::J;
        const auto replacement = InputActions::create(std::move(edited));
        ASSERT_TRUE(replacement);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        ASSERT_TRUE(runtime.request_rebind(replacement.value()));
        runtime.reset();
        Input::Frame frame{.serial = 1, .focused = true};
        frame.keys[size_t(Input::Key::Space)] = {.down = true, .pressed = true};
        runtime.prepare(&frame);
        EXPECT_TRUE(runtime.update().action("jump")->pressed);
        ASSERT_TRUE(runtime.request_rebind(replacement.value()));
        const auto before_prepare = runtime.consume_fixed();
        EXPECT_TRUE(before_prepare.action("jump")->pressed);
        EXPECT_TRUE(before_prepare.action("jump")->down);
        frame = {.serial = 2, .focused = true};
        frame.keys[size_t(Input::Key::J)] = {.down = true, .pressed = true};
        runtime.prepare(&frame);
        EXPECT_FALSE(runtime.update().action("jump")->pressed);
        runtime.reset();
        runtime.prepare(&frame);
        EXPECT_TRUE(runtime.update().action("jump")->pressed);
        ASSERT_TRUE(runtime.request_rebind(actions.value()));
        const auto unrelated =
            persistent_actions({{"other", InputActions::Type::Button, {{Input::Key::K}}}});
        ASSERT_TRUE(unrelated);
        runtime.configure(unrelated.value());
        frame = {.serial = 3, .focused = true};
        frame.keys[size_t(Input::Key::K)] = {.down = true, .pressed = true};
        runtime.prepare(&frame);
        EXPECT_EQ(runtime.update().action("jump"), nullptr);
        ASSERT_NE(runtime.update().action("other"), nullptr);
        EXPECT_TRUE(runtime.update().action("other")->pressed);
    }

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

    TEST(RuntimeInputTest, ConsumingContextHandoffsReleaseAndRebaseIndirectlyAffectedActions) {
        using Type = InputActions::Type;
        const auto actions =
            InputActions::create({{"game", Type::Button, {{Input::Key::Space}}, "gameplay"},
                                     {"menu", Type::Button, {{Input::Key::Space}}, "menu"},
                                     {"common", Type::Button, {{Input::Key::R}}}},
                {{.name = "gameplay"},
                    {.name = "menu", .enabled = false, .priority = 10, .consume = true}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.consume_fixed().action("game")->pressed);

        ASSERT_TRUE(runtime.set_context_enabled("menu", true));
        input.key_event(Input::Key::R, true);
        runtime.prepare(&input.publish_frame());
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_FALSE(state->action("game")->down);
            EXPECT_TRUE(state->action("game")->released);
            EXPECT_TRUE(state->action("menu")->down);
            EXPECT_FALSE(state->action("menu")->pressed);
            EXPECT_FALSE(state->action("menu")->released);
            EXPECT_TRUE(state->action("common")->pressed);
        }
        EXPECT_FALSE(runtime.consume_fixed().action("game")->released);
        ASSERT_TRUE(runtime.set_context_enabled("menu", false));
        runtime.prepare(&input.publish_frame());
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_TRUE(state->action("game")->down);
            EXPECT_FALSE(state->action("game")->pressed);
            EXPECT_FALSE(state->action("game")->released);
            EXPECT_FALSE(state->action("menu")->down);
            EXPECT_TRUE(state->action("menu")->released);
        }
        EXPECT_FALSE(runtime.consume_fixed().action("menu")->released);
        input.key_event(Input::Key::Space, false);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("game")->released);
        EXPECT_TRUE(runtime.consume_fixed().action("game")->released);
        EXPECT_FALSE(runtime.consume_fixed().action("game")->released);
        input.key_event(Input::Key::Space, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("game")->pressed);
    }

    TEST(RuntimeInputTest, GainedBindingsKeepRetainedShortPressAndDeltaAcrossZeroFixedSteps) {
        using Type = InputActions::Type;
        using Motion = InputActions::Motion;
        const auto actions = InputActions::create(
            {{"game", Type::Button, {{Input::Key::Space}, {Input::MouseButton::Left}}, "gameplay"},
                {"fresh", Type::Button, {{Input::Key::Space}, {Input::MouseButton::Right}},
                    "gameplay"},
                {"look", Type::Delta, {{Motion::ScrollX}, {Motion::ScrollY}}, "gameplay"},
                {"menu", Type::Button, {{Input::Key::Space}}, "menu"},
                {"menu_scroll", Type::Delta, {{Motion::ScrollY}}, "menu"}},
            {{.name = "gameplay"}, {.name = "menu", .priority = 10, .consume = true}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        input.mouse_button_event(Input::MouseButton::Left, true);
        input.mouse_button_event(Input::MouseButton::Left, false);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.scroll_event({2, 20});
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.update().action("game")->pressed);

        ASSERT_TRUE(runtime.set_context_enabled("menu", false));
        input.key_event(Input::Key::Space, true);
        input.mouse_button_event(Input::MouseButton::Right, true);
        input.mouse_button_event(Input::MouseButton::Right, false);
        input.scroll_event({3, 30});
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.update().action("game")->down);
        EXPECT_FALSE(runtime.update().action("game")->pressed);
        EXPECT_TRUE(runtime.update().action("fresh")->pressed);
        EXPECT_FLOAT_EQ(runtime.update().action("look")->value, 3);
        const auto first = runtime.consume_fixed();
        EXPECT_TRUE(first.action("game")->down);
        EXPECT_TRUE(first.action("game")->pressed);
        EXPECT_TRUE(first.action("fresh")->pressed);
        EXPECT_FLOAT_EQ(first.action("look")->value, 5);
        EXPECT_FALSE(first.action("menu")->pressed);
        EXPECT_FLOAT_EQ(first.action("menu_scroll")->value, 0);
        const auto& second = runtime.consume_fixed();
        EXPECT_FALSE(second.action("game")->pressed);
        EXPECT_FLOAT_EQ(second.action("look")->value, 0);

        input.key_event(Input::Key::Space, false);
        input.scroll_event({4, 5});
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("game")->released);
        EXPECT_FLOAT_EQ(runtime.update().action("look")->value, 9);
        input.key_event(Input::Key::Space, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("game")->pressed);
    }

    TEST(RuntimeInputTest, LostBindingsDiscardOnlyTheirOwnPendingEdgesAndDelta) {
        using Type = InputActions::Type;
        using Motion = InputActions::Motion;
        const auto actions = InputActions::create(
            {{"mixed", Type::Button, {{Input::Key::Space}, {Input::MouseButton::Left}}, "gameplay"},
                {"blocked", Type::Button, {{Input::Key::Space}}, "gameplay"},
                {"look", Type::Delta, {{Motion::ScrollX}, {Motion::ScrollY}}, "gameplay"},
                {"menu", Type::Button, {{Input::Key::Space}}, "menu"},
                {"menu_scroll", Type::Delta, {{Motion::ScrollY}}, "menu"}},
            {{.name = "gameplay"},
                {.name = "menu", .enabled = false, .priority = 10, .consume = true}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.mouse_button_event(Input::MouseButton::Left, true);
        input.scroll_event({2, 20});
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.set_context_enabled("menu", true));
        input.scroll_event({3, 30});
        runtime.prepare(&input.publish_frame());
        const auto first = runtime.consume_fixed();
        EXPECT_TRUE(first.action("mixed")->pressed);
        EXPECT_TRUE(first.action("mixed")->down);
        EXPECT_FALSE(first.action("mixed")->released);
        EXPECT_FALSE(first.action("blocked")->pressed);
        EXPECT_FALSE(first.action("menu")->pressed);
        EXPECT_FLOAT_EQ(first.action("look")->value, 5);
        EXPECT_FLOAT_EQ(first.action("menu_scroll")->value, 0);

        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.mouse_button_event(Input::MouseButton::Left, false);
        input.scroll_event({0, 4});
        runtime.prepare(&input.publish_frame());
        const auto next = runtime.consume_fixed();
        EXPECT_FALSE(next.action("mixed")->pressed);
        EXPECT_TRUE(next.action("mixed")->released);
        EXPECT_FALSE(next.action("blocked")->pressed);
        EXPECT_TRUE(next.action("menu")->pressed);
        EXPECT_TRUE(next.action("menu")->released);
        EXPECT_FLOAT_EQ(next.action("menu_scroll")->value, 4);
        const auto& repeated = runtime.consume_fixed();
        EXPECT_FALSE(repeated.action("menu")->pressed);
        EXPECT_FALSE(repeated.action("menu")->released);
        EXPECT_FLOAT_EQ(repeated.action("menu_scroll")->value, 0);
    }

    TEST(RuntimeInputTest, PausedConsumptionChangesAndResumeEstablishBothStageBaselines) {
        using Type = InputActions::Type;
        const auto actions = InputActions::create(
            {{"game", Type::Button, {{Input::Key::Space}}, "gameplay"},
                {"look", Type::Delta, {{InputActions::Motion::ScrollY}}, "gameplay"},
                {"menu", Type::Button, {{Input::Key::Space}}, "menu"},
                {"menu_scroll", Type::Delta, {{InputActions::Motion::ScrollY}}, "menu"}},
            {{.name = "gameplay"},
                {.name = "menu", .enabled = false, .priority = 10, .consume = true}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.scroll_event({0, 2});
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.set_context_enabled("menu", true));
        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame(), true);
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_FALSE(state->action("game")->down);
            EXPECT_FALSE(state->action("game")->pressed);
            EXPECT_FALSE(state->action("game")->released);
            EXPECT_TRUE(state->action("menu")->down);
            EXPECT_FALSE(state->action("menu")->pressed);
            EXPECT_FLOAT_EQ(state->action("look")->value, 0);
            EXPECT_FLOAT_EQ(state->action("menu_scroll")->value, 0);
        }
        ASSERT_TRUE(runtime.set_context_enabled("menu", false));
        runtime.prepare(&input.publish_frame(), true);
        EXPECT_TRUE(runtime.consume_fixed().action("game")->down);
        EXPECT_FALSE(runtime.update().action("game")->pressed);
        runtime.rebase();
        runtime.prepare(nullptr);
        input.scroll_event({0, 6});
        runtime.prepare(&input.publish_frame());
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_TRUE(state->action("game")->down);
            EXPECT_FALSE(state->action("game")->pressed);
            EXPECT_FALSE(state->action("game")->released);
            EXPECT_FLOAT_EQ(state->action("look")->value, 0);
        }
        input.key_event(Input::Key::Space, false);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("game")->released);
        input.key_event(Input::Key::Space, true);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("game")->pressed);
    }

    TEST(RuntimeInputTest, DeviceReplacementAndFocusLossCannotKeepOldRoutedPendingInput) {
        using Type = InputActions::Type;
        const auto actions = InputActions::create(
            {{"pad", Type::Button, {{Input::GamepadButton::South}}, "gameplay"},
                {"mixed", Type::Button, {{Input::Key::Space}, {Input::GamepadButton::South}},
                    "gameplay"},
                {"look", Type::Delta, {{InputActions::Motion::ScrollY}}, "gameplay"},
                {"menu", Type::Button, {{Input::GamepadButton::South}}, "menu"}},
            {{.name = "gameplay"}, {.name = "menu", .priority = 10, .consume = true}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        Input::GamepadSample pad;
        input.gamepad_sample(0, pad);
        input.gamepad_sample(1, pad);
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.set_context_enabled("menu", false));
        runtime.prepare(&input.publish_frame());
        static_cast<void>(runtime.consume_fixed());
        pad.buttons[size_t(Input::GamepadButton::South)] = true;
        input.gamepad_sample(0, pad);
        pad.buttons[size_t(Input::GamepadButton::South)] = false;
        input.gamepad_sample(0, pad);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.update().action("pad")->pressed);
        input.gamepad_sample(0, std::nullopt);
        runtime.prepare(&input.publish_frame());
        const auto replacement = runtime.consume_fixed();
        EXPECT_FALSE(replacement.action("pad")->pressed);
        EXPECT_FALSE(replacement.action("pad")->down);
        EXPECT_TRUE(replacement.action("mixed")->pressed);

        pad.buttons[size_t(Input::GamepadButton::South)] = true;
        input.gamepad_sample(1, pad);
        runtime.prepare(&input.publish_frame());
        const auto held = runtime.consume_fixed();
        EXPECT_TRUE(held.action("pad")->pressed);
        EXPECT_TRUE(held.action("pad")->down);
        input.gamepad_sample(1, std::nullopt);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("pad")->released);
        EXPECT_FALSE(runtime.consume_fixed().action("pad")->released);

        input.key_event(Input::Key::Space, true);
        input.scroll_event({0, 2});
        runtime.prepare(&input.publish_frame());
        input.focus_event(false);
        runtime.prepare(&input.publish_frame());
        const auto unfocused = runtime.consume_fixed();
        EXPECT_FALSE(unfocused.action("mixed")->pressed);
        EXPECT_FALSE(unfocused.action("mixed")->down);
        EXPECT_FLOAT_EQ(unfocused.action("look")->value, 0);
    }

    TEST(RuntimeInputTest, PointerRevocationDropsPendingMouseInputWithoutClearingOtherDevices) {
        using Type = InputActions::Type;
        const auto actions =
            InputActions::create({{"mouse", Type::Button, {{Input::MouseButton::Left}}},
                {"cursor", Type::Delta, {{InputActions::Motion::CursorX}}},
                {"scroll", Type::Delta, {{InputActions::Motion::ScrollY}}},
                {"keyboard", Type::Button, {{Input::Key::Space}}},
                {"pad", Type::Button, {{Input::GamepadButton::South}}},
                {"mixed", Type::Button, {{Input::MouseButton::Left}, {Input::Key::Space}}}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        Input::Gate gate;
        Input::GamepadSample pad;
        input.focus_event(true);
        input.gamepad_sample(0, pad);
        input.cursor_event({0, 0});
        runtime.prepare(&gate.read(input.publish_frame(), true, true));
        static_cast<void>(runtime.consume_fixed());

        input.mouse_button_event(Input::MouseButton::Left, true);
        input.mouse_button_event(Input::MouseButton::Left, false);
        input.cursor_event({5, 0});
        input.scroll_event({0, 2});
        input.key_event(Input::Key::Space, true);
        pad.buttons[size_t(Input::GamepadButton::South)] = true;
        input.gamepad_sample(0, pad);
        runtime.prepare(&gate.read(input.publish_frame(), true, true));
        ASSERT_TRUE(runtime.update().action("mouse")->pressed);
        ASSERT_FLOAT_EQ(runtime.update().action("cursor")->value, 5);
        ASSERT_FLOAT_EQ(runtime.update().action("scroll")->value, 2);

        // 鼠标离开画面时还没有固定步，不能把旧鼠标输入交给下一固定步。
        runtime.prepare(&gate.read(input.publish_frame(), true, false));
        const auto first = runtime.consume_fixed();
        EXPECT_TRUE(first.focused());
        EXPECT_FALSE(first.physical().mouse(Input::MouseButton::Left).pressed);
        EXPECT_EQ(first.physical().cursor_delta, Math::Vec2(0));
        EXPECT_EQ(first.physical().scroll, Math::Vec2(0));
        EXPECT_FALSE(first.action("mouse")->pressed);
        EXPECT_FALSE(first.action("mouse")->down);
        EXPECT_FLOAT_EQ(first.action("cursor")->value, 0);
        EXPECT_FLOAT_EQ(first.action("scroll")->value, 0);
        EXPECT_TRUE(first.physical().key(Input::Key::Space).pressed);
        EXPECT_TRUE(first.physical().gamepads[0].button(Input::GamepadButton::South).pressed);
        for(const auto name : {"keyboard", "pad", "mixed"}) {
            EXPECT_TRUE(first.action(name)->pressed) << name;
            EXPECT_TRUE(first.action(name)->down) << name;
        }

        const auto& second = runtime.consume_fixed();
        for(const auto name : {"mouse", "keyboard", "pad", "mixed"})
            EXPECT_FALSE(second.action(name)->pressed) << name;
        EXPECT_FLOAT_EQ(second.action("cursor")->value, 0);
        EXPECT_FLOAT_EQ(second.action("scroll")->value, 0);
    }

    TEST(RuntimeInputTest, PointerReacquisitionRequiresFreshPressAfterReleasingHeldAction) {
        using Type = InputActions::Type;
        const auto actions =
            InputActions::create({{"mouse", Type::Button, {{Input::MouseButton::Left}}},
                {"cursor", Type::Delta, {{InputActions::Motion::CursorX}}},
                {"scroll", Type::Delta, {{InputActions::Motion::ScrollY}}}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        Input::Gate gate;
        input.focus_event(true);
        input.cursor_event({0, 0});
        runtime.prepare(&gate.read(input.publish_frame(), true, true));
        static_cast<void>(runtime.consume_fixed());
        input.mouse_button_event(Input::MouseButton::Left, true);
        runtime.prepare(&gate.read(input.publish_frame(), true, true));
        ASSERT_TRUE(runtime.consume_fixed().action("mouse")->down);

        input.cursor_event({5, 0});
        input.scroll_event({0, 2});
        runtime.prepare(&gate.read(input.publish_frame(), true, false));
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_TRUE(state->physical().mouse(Input::MouseButton::Left).released);
            EXPECT_TRUE(state->action("mouse")->released);
            EXPECT_FALSE(state->action("mouse")->down);
            EXPECT_FALSE(state->action("mouse")->pressed);
            EXPECT_FLOAT_EQ(state->action("cursor")->value, 0);
            EXPECT_FLOAT_EQ(state->action("scroll")->value, 0);
        }
        EXPECT_FALSE(runtime.consume_fixed().action("mouse")->released);

        input.cursor_event({10, 0});
        input.scroll_event({0, 3});
        runtime.prepare(&gate.read(input.publish_frame(), true, true));
        for(const auto* state : {&runtime.update(), &runtime.consume_fixed()}) {
            EXPECT_FALSE(state->action("mouse")->down);
            EXPECT_FALSE(state->action("mouse")->pressed);
            EXPECT_FALSE(state->action("mouse")->released);
            EXPECT_EQ(state->physical().cursor_delta, Math::Vec2(0));
            EXPECT_EQ(state->physical().scroll, Math::Vec2(0));
            EXPECT_FLOAT_EQ(state->action("cursor")->value, 0);
            EXPECT_FLOAT_EQ(state->action("scroll")->value, 0);
        }
        input.mouse_button_event(Input::MouseButton::Left, false);
        runtime.prepare(&gate.read(input.publish_frame(), true, true));
        input.mouse_button_event(Input::MouseButton::Left, true);
        input.cursor_event({15, 0});
        input.scroll_event({0, 4});
        runtime.prepare(&gate.read(input.publish_frame(), true, true));
        const auto& fresh = runtime.consume_fixed();
        EXPECT_TRUE(fresh.action("mouse")->down);
        EXPECT_TRUE(fresh.action("mouse")->pressed);
        EXPECT_FLOAT_EQ(fresh.action("cursor")->value, 5);
        EXPECT_FLOAT_EQ(fresh.action("scroll")->value, 4);
    }

    TEST(RuntimeInputTest, SerialRollbackDropsPendingBindingsAndDuplicateFramesDoNotReplayThem) {
        using Type = InputActions::Type;
        const auto actions = InputActions::create(
            {{"game", Type::Button, {{Input::Key::Space}}, "gameplay"},
                {"look", Type::Delta, {{InputActions::Motion::ScrollY}}, "gameplay"},
                {"menu", Type::Button, {{Input::Key::Enter}}, "menu"}},
            {{.name = "gameplay"}, {.name = "menu", .priority = 10, .consume = true}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input::Frame old{.serial = 10, .focused = true};
        old.keys[size_t(Input::Key::Space)] = {.pressed = true, .released = true};
        old.scroll.y = 2;
        runtime.prepare(&old);
        Input::Frame restarted{.serial = 1, .focused = true};
        runtime.prepare(&restarted);
        const auto reset = runtime.consume_fixed();
        EXPECT_FALSE(reset.action("game")->pressed);
        EXPECT_FALSE(reset.action("game")->released);
        EXPECT_FLOAT_EQ(reset.action("look")->value, 0);
        restarted.serial = 2;
        restarted.keys[size_t(Input::Key::Space)] = {.down = true, .pressed = true};
        restarted.scroll.y = 3;
        runtime.prepare(&restarted);
        const auto first = runtime.consume_fixed();
        EXPECT_TRUE(first.action("game")->pressed);
        EXPECT_FLOAT_EQ(first.action("look")->value, 3);
        runtime.prepare(&restarted);
        const auto duplicate = runtime.consume_fixed();
        EXPECT_TRUE(duplicate.action("game")->down);
        EXPECT_FALSE(duplicate.action("game")->pressed);
        EXPECT_FLOAT_EQ(duplicate.action("look")->value, 0);
    }

    TEST(RuntimeInputTest, ContextRoutingCommitsAtPrepareAndRoundTripsKeepPendingInput) {
        using Type = InputActions::Type;
        const auto actions = InputActions::create(
            {{"game", Type::Button, {{Input::Key::Space}}, "gameplay"},
                {"look", Type::Delta, {{InputActions::Motion::ScrollY}}, "gameplay"},
                {"menu", Type::Button, {{Input::Key::Space}}, "menu"},
                {"menu_scroll", Type::Delta, {{InputActions::Motion::ScrollY}}, "menu"}},
            {{.name = "gameplay"},
                {.name = "menu", .enabled = false, .priority = 10, .consume = true}});
        ASSERT_TRUE(actions);
        RuntimeInput runtime;
        runtime.configure(actions.value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        input.scroll_event({0, 2});
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.set_context_enabled("menu", true));
        EXPECT_TRUE(runtime.update().action("game")->pressed);
        const auto before_enable = runtime.consume_fixed();
        EXPECT_TRUE(before_enable.action("game")->pressed);
        EXPECT_TRUE(before_enable.action("game")->down);
        EXPECT_FLOAT_EQ(before_enable.action("look")->value, 2);
        EXPECT_FALSE(before_enable.action("menu")->down);

        input.scroll_event({0, 3});
        runtime.prepare(&input.publish_frame());
        const auto enabled = runtime.consume_fixed();
        EXPECT_FALSE(enabled.action("game")->down);
        EXPECT_TRUE(enabled.action("game")->released);
        EXPECT_FLOAT_EQ(enabled.action("look")->value, 0);
        EXPECT_TRUE(enabled.action("menu")->down);
        EXPECT_FALSE(enabled.action("menu")->pressed);
        EXPECT_FLOAT_EQ(enabled.action("menu_scroll")->value, 0);

        ASSERT_TRUE(runtime.set_context_enabled("menu", false));
        ASSERT_TRUE(runtime.set_context_enabled("menu", false));
        const auto before_disable = runtime.consume_fixed();
        EXPECT_TRUE(before_disable.action("menu")->down);
        EXPECT_FALSE(before_disable.action("game")->down);
        runtime.prepare(&input.publish_frame());
        const auto disabled = runtime.consume_fixed();
        EXPECT_TRUE(disabled.action("game")->down);
        EXPECT_FALSE(disabled.action("game")->pressed);
        EXPECT_FALSE(disabled.action("menu")->down);
        EXPECT_TRUE(disabled.action("menu")->released);

        input.key_event(Input::Key::Space, false);
        runtime.prepare(&input.publish_frame());
        EXPECT_TRUE(runtime.consume_fixed().action("game")->released);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.scroll_event({0, 4});
        runtime.prepare(&input.publish_frame());
        ASSERT_TRUE(runtime.set_context_enabled("menu", true));
        ASSERT_TRUE(runtime.set_context_enabled("menu", false));
        input.scroll_event({0, 5});
        runtime.prepare(&input.publish_frame());
        const auto round_trip = runtime.consume_fixed();
        EXPECT_TRUE(round_trip.action("game")->pressed);
        EXPECT_TRUE(round_trip.action("game")->released);
        EXPECT_FALSE(round_trip.action("game")->down);
        EXPECT_FLOAT_EQ(round_trip.action("look")->value, 9);
        EXPECT_FALSE(round_trip.action("menu")->pressed);
        EXPECT_FLOAT_EQ(round_trip.action("menu_scroll")->value, 0);
        const auto& repeated = runtime.consume_fixed();
        EXPECT_FALSE(repeated.action("game")->pressed);
        EXPECT_FALSE(repeated.action("game")->released);
        EXPECT_FLOAT_EQ(repeated.action("look")->value, 0);
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
