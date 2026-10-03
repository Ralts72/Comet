#include "input/input_actions.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace Comet::Tests {
    TEST(InputActionsTest, ButtonBindingsShareHeldStateAndKeepShortClicks) {
        auto actions = InputActions::create({{"jump", InputActions::Type::Button,
            {{Input::Key::Space}, {Input::MouseButton::Left}}}});
        ASSERT_TRUE(actions);
        Input input;
        InputState frame;
        input.focus_event(true);
        auto sample = [&] {
            actions.value().evaluate(input.publish_frame(), frame);
            return *frame.action("jump");
        };
        input.key_event(Input::Key::Space, true);
        EXPECT_TRUE(sample().pressed);
        input.mouse_button_event(Input::MouseButton::Left, true);
        EXPECT_FALSE(sample().pressed);
        input.key_event(Input::Key::Space, false);
        auto held = sample();
        EXPECT_TRUE(held.down);
        EXPECT_FALSE(held.released);
        input.mouse_button_event(Input::MouseButton::Left, false);
        EXPECT_TRUE(sample().released);
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        const auto click = sample();
        EXPECT_FALSE(click.down);
        EXPECT_TRUE(click.pressed);
        EXPECT_TRUE(click.released);
        EXPECT_FALSE(sample().pressed);
        input.key_event(Input::Key::Space, true);
        EXPECT_TRUE(sample().down);
        input.focus_event(false);
        EXPECT_TRUE(sample().released);
        EXPECT_FALSE(sample().released);
    }

    TEST(InputActionsTest, AxisDeadzoneAndDeltaHaveDifferentUnitsAndFocusRules) {
        auto actions = InputActions::create(
            {{"move", InputActions::Type::Axis,
                 {{Input::Key::D}, {Input::Key::A, -1}, {Input::GamepadAxis::LeftX, 1, 0.2f}}},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::CursorY, -2}}}});
        ASSERT_TRUE(actions);
        Input input;
        InputState frame;
        input.focus_event(true);
        input.cursor_event({0, 0});
        Input::GamepadSample pad;
        pad.axes[size_t(Input::GamepadAxis::LeftX)] = 0.6f;
        input.gamepad_sample(1, pad);
        input.cursor_event({0, 20});
        actions.value().evaluate(input.publish_frame(), frame);
        EXPECT_NEAR(frame.action("move")->value, 0.5f, 1e-6f);
        EXPECT_FLOAT_EQ(frame.action("look")->value, -40);
        actions.value().evaluate(input.publish_frame(), frame);
        EXPECT_FLOAT_EQ(frame.action("look")->value, 0);
        EXPECT_NEAR(frame.action("move")->value, 0.5f, 1e-6f);
        input.key_event(Input::Key::D, true);
        actions.value().evaluate(input.publish_frame(), frame);
        EXPECT_FLOAT_EQ(frame.action("move")->value, 1);
        input.gamepad_sample(1, std::nullopt);
        input.key_event(Input::Key::A, true);
        actions.value().evaluate(input.publish_frame(), frame);
        EXPECT_FLOAT_EQ(frame.action("move")->value, 0);
        input.key_event(Input::Key::A, false);
        input.focus_event(false);
        actions.value().evaluate(input.publish_frame(), frame);
        EXPECT_FLOAT_EQ(frame.action("move")->value, 0);
        EXPECT_EQ(frame.action("unknown"), nullptr);
    }

    TEST(InputActionsTest, RejectsAmbiguousAndInvalidBindingsBeforeRuntime) {
        using Type = InputActions::Type;
        EXPECT_FALSE(
            InputActions::create({{"jump", Type::Button, {}}, {"jump", Type::Button, {}}}));
        EXPECT_FALSE(InputActions::create({{"bad name", Type::Button, {}}}));
        EXPECT_FALSE(InputActions::create({{"jump", Type::Button, {{Input::GamepadAxis::LeftX}}}}));
        EXPECT_FALSE(
            InputActions::create({{"move", Type::Axis, {{InputActions::Motion::CursorX}}}}));
        EXPECT_FALSE(InputActions::create({{"look", Type::Delta, {{Input::Key::W}}}}));
        EXPECT_FALSE(InputActions::create({{"move", Type::Axis, {{Input::Key::Unknown}}}}));
        EXPECT_FALSE(
            InputActions::create({{"move", Type::Axis, {{Input::GamepadAxis::LeftX, 1, 1}}}}));
        EXPECT_FALSE(InputActions::create(
            {{"move", Type::Axis, {{Input::Key::A, std::numeric_limits<float>::infinity()}}}}));
        EXPECT_FALSE(InputActions::parse_binding("key", "Typo"));
        EXPECT_FALSE(InputActions::parse_binding("keyboard", "W"));
        EXPECT_TRUE(InputActions::parse_binding("key", "F25"));
        EXPECT_TRUE(InputActions::parse_binding("gamepad_button", "South"));
    }

    TEST(InputActionsTest, FormatsPersistedBindingsForRoundTrip) {
        using namespace std::literals;
        constexpr std::array controls{std::pair{"key"sv, "A"sv}, std::pair{"key"sv, "7"sv},
            std::pair{"key"sv, "F25"sv}, std::pair{"key"sv, "LeftShift"sv},
            std::pair{"mouse_button"sv, "Right"sv}, std::pair{"gamepad_button"sv, "South"sv},
            std::pair{"gamepad_axis"sv, "LeftX"sv}, std::pair{"motion"sv, "CursorY"sv}};
        for(const auto& [source, control] : controls) {
            SCOPED_TRACE(std::string(source) + "/" + std::string(control));
            auto parsed = InputActions::parse_binding(source, control);
            ASSERT_TRUE(parsed) << parsed.error();
            auto formatted = InputActions::format_binding(parsed.value());
            ASSERT_TRUE(formatted) << formatted.error();
            EXPECT_EQ(formatted.value().source, source);
            EXPECT_EQ(formatted.value().control, control);
        }
        EXPECT_FALSE(InputActions::format_binding({Input::Key::Keypad1}));
    }

    TEST(InputActionsTest, ConsumingPrioritiesAreIndependentOfDeclarationOrderAndKeepCommonInput) {
        using Type = InputActions::Type;
        std::vector<InputActions::Action> definitions{
            {"common", Type::Button, {{Input::Key::Space}}},
            {"common_g", Type::Button, {{Input::Key::G}}},
            {"low", Type::Button, {{Input::Key::Space}}, "low"},
            {"menu", Type::Button, {{Input::Key::Space}}, "menu"},
            {"peer", Type::Button, {{Input::Key::Space}}, "peer"},
            {"observer", Type::Button, {{Input::Key::G}}, "observer"},
            {"low_g", Type::Button, {{Input::Key::G}}, "low"},
            {"disabled", Type::Button, {{Input::Key::D}}, "disabled"},
            {"low_d", Type::Button, {{Input::Key::D}}, "low"},
            {"default_a", Type::Button, {{Input::Key::Enter}}, "default_a"},
            {"default_b", Type::Button, {{Input::Key::Enter}}, "default_b"}};
        std::vector<InputActions::Context> contexts{{.name = "low", .priority = -10},
            {.name = "menu", .priority = 20, .consume = true},
            {.name = "peer", .priority = 20, .consume = true}, {.name = "observer", .priority = 50},
            {.name = "disabled", .enabled = false, .priority = 100, .consume = true},
            {.name = "default_a"}, {.name = "default_b"}};
        EXPECT_EQ(contexts.back().priority, 0);
        EXPECT_FALSE(contexts.back().consume);
        Input input;
        input.focus_event(true);
        for(const auto key : {Input::Key::Space, Input::Key::G, Input::Key::D, Input::Key::Enter})
            input.key_event(key, true);
        const auto frame = input.publish_frame();
        for(const bool reverse : {false, true}) {
            SCOPED_TRACE(reverse);
            if(reverse) {
                std::ranges::reverse(definitions);
                std::ranges::reverse(contexts);
            }
            const auto actions = InputActions::create(definitions, contexts);
            ASSERT_TRUE(actions) << actions.error();
            InputState state;
            actions.value().evaluate(frame, state);
            for(const char* name : {"common", "common_g", "menu", "peer", "observer", "low_g",
                    "low_d", "default_a", "default_b"}) {
                SCOPED_TRACE(name);
                EXPECT_TRUE(state.action(name)->down);
                EXPECT_TRUE(state.action(name)->pressed);
            }
            for(const char* name : {"low", "disabled"}) {
                SCOPED_TRACE(name);
                EXPECT_FALSE(state.action(name)->down);
                EXPECT_FALSE(state.action(name)->pressed);
            }
        }
    }

    TEST(InputActionsTest, ConsumptionMasksIndividualControlsAcrossDevicesAndIgnoresBindingScale) {
        using Type = InputActions::Type;
        using Motion = InputActions::Motion;
        const auto actions = InputActions::create(
            {{"mouse", Type::Button, {{Input::MouseButton::Left}}, "menu"},
                {"pad", Type::Button, {{Input::GamepadButton::South}}, "menu"},
                {"axis", Type::Axis, {{Input::GamepadAxis::LeftX, -1, 0.9f}}, "menu"},
                {"scroll", Type::Delta, {{Motion::ScrollY, 3}}, "menu"},
                {"blocked_mouse", Type::Button, {{Input::MouseButton::Left}}, "gameplay"},
                {"mixed_mouse", Type::Button,
                    {{Input::MouseButton::Left}, {Input::MouseButton::Right}}, "gameplay"},
                {"blocked_pad", Type::Button, {{Input::GamepadButton::South}}, "gameplay"},
                {"mixed_pad", Type::Button,
                    {{Input::GamepadButton::South}, {Input::GamepadButton::East}}, "gameplay"},
                {"mixed_axis", Type::Axis,
                    {{Input::GamepadAxis::LeftX, 1, 0.1f}, {Input::GamepadAxis::RightX, 0.5f}},
                    "gameplay"},
                {"mixed_scroll", Type::Delta, {{Motion::ScrollX, -2}, {Motion::ScrollY, -4}},
                    "gameplay"}},
            {{.name = "gameplay"}, {.name = "menu", .priority = 1, .consume = true}});
        ASSERT_TRUE(actions) << actions.error();
        Input input;
        input.focus_event(true);
        input.mouse_button_event(Input::MouseButton::Left, true);
        input.mouse_button_event(Input::MouseButton::Right, true);
        Input::GamepadSample pad;
        pad.buttons[size_t(Input::GamepadButton::South)] = true;
        pad.buttons[size_t(Input::GamepadButton::East)] = true;
        pad.axes[size_t(Input::GamepadAxis::LeftX)] = 0.5f;
        pad.axes[size_t(Input::GamepadAxis::RightX)] = 0.4f;
        input.gamepad_sample(0, pad);
        input.scroll_event({2, 4});
        InputState state;
        actions.value().evaluate(input.publish_frame(), state);
        EXPECT_TRUE(state.action("mouse")->down);
        EXPECT_TRUE(state.action("pad")->down);
        EXPECT_FALSE(state.action("blocked_mouse")->down);
        EXPECT_FALSE(state.action("blocked_pad")->down);
        EXPECT_TRUE(state.action("mixed_mouse")->down);
        EXPECT_TRUE(state.action("mixed_pad")->down);
        EXPECT_FLOAT_EQ(state.action("axis")->value, 0);
        EXPECT_FLOAT_EQ(state.action("mixed_axis")->value, 0.2f);
        EXPECT_FLOAT_EQ(state.action("scroll")->value, 12);
        EXPECT_FLOAT_EQ(state.action("mixed_scroll")->value, -4);
    }

    TEST(InputActionsTest, ContextDefaultsFilterOnlyTheirOwnActions) {
        using Type = InputActions::Type;
        auto actions =
            InputActions::create({{"common", Type::Button, {{Input::Key::Space}}},
                                     {"player", Type::Button, {{Input::Key::Space}}, "gameplay"},
                                     {"camera", Type::Axis, {{Input::Key::Space}}, "camera"}},
                {{"gameplay", false}, {"camera"}});
        ASSERT_TRUE(actions);
        EXPECT_EQ(actions.value().contexts().size(), 2);
        EXPECT_FALSE(actions.value().contexts().front().enabled);
        Input input;
        InputState state;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        actions.value().evaluate(input.publish_frame(), state);
        EXPECT_TRUE(state.action("common")->pressed);
        EXPECT_FALSE(state.action("player")->down);
        EXPECT_FALSE(state.action("player")->pressed);
        EXPECT_FLOAT_EQ(state.action("camera")->value, 1);

        EXPECT_FALSE(InputActions::create({}, {{""}}));
        EXPECT_FALSE(InputActions::create({}, {{"bad name"}}));
        EXPECT_FALSE(InputActions::create({}, {{"gameplay"}, {"gameplay"}}));
        EXPECT_FALSE(InputActions::create({{"player", Type::Button, {}, "unknown"}}));
        EXPECT_FALSE(InputActions::create({}, {{std::string(65, 'a')}}));
        std::vector<InputActions::Context> contexts;
        for(size_t i = 0; i < InputActions::MAX_CONTEXTS; ++i)
            contexts.push_back({"group" + std::to_string(i)});
        EXPECT_TRUE(InputActions::create({}, contexts));
        contexts.push_back({"overflow"});
        EXPECT_FALSE(InputActions::create({}, contexts));
    }
}
