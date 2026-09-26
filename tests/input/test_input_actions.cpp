#include "input/input_actions.h"

#include <gtest/gtest.h>
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
}
