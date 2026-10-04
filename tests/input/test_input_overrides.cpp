#include "input/input_overrides.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <utility>

namespace Comet::Tests {
    namespace {
        using Type = InputActions::Type;
        using Action = InputOverrides::Action;
        using Binding = InputOverrides::Binding;

        constexpr Uuid id(const unsigned value) {
            Uuid::Bytes bytes{};
            bytes[14] = static_cast<std::uint8_t>(value >> 8);
            bytes[15] = static_cast<std::uint8_t>(value);
            return Uuid(bytes);
        }
    }

    TEST(InputOverridesTest, EmptyOverridesInheritAllDefaults) {
        const auto defaults = InputActions::create(
            {{"jump", Type::Button, {{Input::Key::Space, 1, 0, id(2)}}, "player", id(1)}},
            {{"player", false, 10, true}});
        ASSERT_TRUE(defaults);
        const auto empty = InputOverrides::create({});
        ASSERT_TRUE(empty);
        EXPECT_EQ(empty.value(), InputOverrides{});
        const auto resolved = empty.value().resolve(defaults.value());
        ASSERT_TRUE(resolved) << resolved.error();
        EXPECT_EQ(resolved.value().actions, defaults.value());
        EXPECT_TRUE(resolved.value().issues.empty());
        EXPECT_TRUE(InputOverrides{}.resolve(InputActions{}));
    }

    TEST(InputOverridesTest, SparseFieldsFollowChangedDefaultsAndNewActionsAndBindings) {
        const auto overrides = InputOverrides::create({{id(1), Type::Axis, false,
            {{.id = id(2), .control = Input::GamepadAxis::RightX}, {.id = id(3), .scale = -0.5f},
                {.id = id(4), .deadzone = 0.3f}}}});
        ASSERT_TRUE(overrides) << overrides.error();
        const auto original = overrides.value();
        const auto first_defaults = InputActions::create({{"move", Type::Axis,
            {{Input::GamepadAxis::LeftX, 1, 0.1f, id(2)},
                {Input::GamepadAxis::LeftY, 1, 0.1f, id(3)},
                {Input::GamepadAxis::RightY, 1, 0.1f, id(4)}},
            {}, id(1)}});
        ASSERT_TRUE(first_defaults);
        const auto first = overrides.value().resolve(first_defaults.value());
        ASSERT_TRUE(first) << first.error();
        EXPECT_TRUE(first.value().issues.empty());
        EXPECT_EQ(first.value().actions.actions()[0].bindings[0].control,
            InputActions::Control(Input::GamepadAxis::RightX));
        EXPECT_FLOAT_EQ(first.value().actions.actions()[0].bindings[0].scale, 1);
        EXPECT_FLOAT_EQ(first.value().actions.actions()[0].bindings[0].deadzone, 0.1f);

        const auto upgraded_defaults = InputActions::create(
            {{"new_action", Type::Button, {{Input::Key::R, 1, 0, id(7)}}, {}, id(6)},
                {"renamed_move", Type::Axis,
                    {{Input::GamepadAxis::RightY, 0.7f, 0.25f, id(4)},
                        {Input::GamepadAxis::LeftX, 0.8f, 0.2f, id(2)},
                        {Input::GamepadAxis::RightX, 0.6f, 0.15f, id(3)},
                        {Input::Key::D, 1, 0, id(5)}},
                    "new_context", id(1)}},
            {{"new_context", false, 20, true}});
        ASSERT_TRUE(upgraded_defaults);
        const auto upgraded = overrides.value().resolve(upgraded_defaults.value());
        ASSERT_TRUE(upgraded) << upgraded.error();
        EXPECT_TRUE(upgraded.value().issues.empty());
        auto expected_actions = upgraded_defaults.value().actions();
        expected_actions[1].bindings[0].deadzone = 0.3f;
        expected_actions[1].bindings[1].control = Input::GamepadAxis::RightX;
        expected_actions[1].bindings[2].scale = -0.5f;
        const auto expected =
            InputActions::create(std::move(expected_actions), upgraded_defaults.value().contexts());
        ASSERT_TRUE(expected);
        EXPECT_EQ(upgraded.value().actions, expected.value());
        EXPECT_EQ(overrides.value(), original);
    }

    TEST(InputOverridesTest, ExplicitZeroFieldsAreAppliedRatherThanInherited) {
        const auto defaults = InputActions::create(
            {{"move", Type::Axis, {{Input::GamepadAxis::LeftX, -1, 0.2f, id(2)}}, {}, id(1)}});
        ASSERT_TRUE(defaults);
        const auto overrides = InputOverrides::create(
            {{id(1), Type::Axis, false, {{.id = id(2), .scale = 0, .deadzone = 0}}}});
        ASSERT_TRUE(overrides);
        const auto result = overrides.value().resolve(defaults.value());
        ASSERT_TRUE(result);
        EXPECT_TRUE(result.value().issues.empty());
        EXPECT_FLOAT_EQ(result.value().actions.actions()[0].bindings[0].scale, 0);
        EXPECT_FLOAT_EQ(result.value().actions.actions()[0].bindings[0].deadzone, 0);
    }

    TEST(InputOverridesTest, DisablingActionsAndBindingsAlsoHandlesNewDefaultsAndCanBeRemoved) {
        const auto defaults = InputActions::create(
            {{"jump", Type::Button,
                 {{Input::Key::Space, 1, 0, id(2)}, {Input::GamepadButton::South, 1, 0, id(3)}}, {},
                 id(1)},
                {"move", Type::Axis, {{Input::Key::D, 1, 0, id(5)}, {Input::Key::A, -1, 0, id(6)}},
                    {}, id(4)}});
        ASSERT_TRUE(defaults);
        const auto overrides = InputOverrides::create({{id(1), Type::Button, true, {}},
            {id(4), Type::Axis, false, {{.id = id(5), .disabled = true}}}});
        ASSERT_TRUE(overrides);
        const auto first = overrides.value().resolve(defaults.value());
        ASSERT_TRUE(first);
        EXPECT_TRUE(first.value().actions.actions()[0].bindings.empty());
        ASSERT_EQ(first.value().actions.actions()[1].bindings.size(), 1u);
        EXPECT_EQ(first.value().actions.actions()[1].bindings[0].id, id(6));

        auto upgraded_actions = defaults.value().actions();
        upgraded_actions[0].bindings.push_back({Input::Key::J, 1, 0, id(7)});
        upgraded_actions[1].bindings.push_back({Input::Key::Right, 1, 0, id(8)});
        const auto upgraded_defaults = InputActions::create(std::move(upgraded_actions));
        ASSERT_TRUE(upgraded_defaults);
        const auto upgraded = overrides.value().resolve(upgraded_defaults.value());
        ASSERT_TRUE(upgraded);
        EXPECT_TRUE(upgraded.value().issues.empty());
        EXPECT_TRUE(upgraded.value().actions.actions()[0].bindings.empty());
        ASSERT_EQ(upgraded.value().actions.actions()[1].bindings.size(), 2u);
        EXPECT_EQ(upgraded.value().actions.actions()[1].bindings[1].id, id(8));

        auto remaining = overrides.value().actions();
        remaining.erase(remaining.begin());
        const auto restored_action = InputOverrides::create(std::move(remaining));
        ASSERT_TRUE(restored_action);
        const auto partial = restored_action.value().resolve(upgraded_defaults.value());
        ASSERT_TRUE(partial);
        EXPECT_EQ(partial.value().actions.actions()[0], upgraded_defaults.value().actions()[0]);
        EXPECT_EQ(partial.value().actions.actions()[1], upgraded.value().actions.actions()[1]);
        const auto restored_all = InputOverrides{}.resolve(upgraded_defaults.value());
        ASSERT_TRUE(restored_all);
        EXPECT_EQ(restored_all.value().actions, upgraded_defaults.value());
    }

    TEST(InputOverridesTest, UnknownIdsAndChangedTypesOnlySkipAffectedRecords) {
        const auto defaults = InputActions::create(
            {{"jump", Type::Button, {{Input::Key::Space, 1, 0, id(2)}}, {}, id(1)},
                {"move", Type::Axis, {{Input::GamepadAxis::LeftX, 1, 0.1f, id(4)}}, {}, id(3)}});
        ASSERT_TRUE(defaults);
        const auto overrides =
            InputOverrides::create({{id(9), Type::Button, true, {}}, {id(1), Type::Axis, true, {}},
                {id(3), Type::Axis, false,
                    {{.id = id(8), .disabled = true},
                        {.id = id(4), .control = Input::GamepadAxis::RightX}}}});
        ASSERT_TRUE(overrides);
        const auto original = overrides.value();
        const auto result = overrides.value().resolve(defaults.value());
        ASSERT_TRUE(result);
        ASSERT_EQ(result.value().issues.size(), 3u);
        EXPECT_EQ(result.value().issues[0].action, id(9));
        EXPECT_FALSE(result.value().issues[0].binding);
        EXPECT_EQ(result.value().issues[1].action, id(1));
        EXPECT_FALSE(result.value().issues[1].binding);
        EXPECT_EQ(result.value().issues[2].action, id(3));
        EXPECT_EQ(result.value().issues[2].binding, id(8));
        for(const auto& issue : result.value().issues)
            EXPECT_FALSE(issue.message.empty());
        EXPECT_EQ(result.value().actions.actions()[0], defaults.value().actions()[0]);
        EXPECT_EQ(result.value().actions.actions()[1].bindings[0].control,
            InputActions::Control(Input::GamepadAxis::RightX));
        EXPECT_EQ(overrides.value(), original);
    }

    TEST(InputOverridesTest, IncompatibleRecordsCanApplyAgainWhenDefaultsBecomeCompatible) {
        const auto overrides = InputOverrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::J}}}});
        ASSERT_TRUE(overrides);
        const auto missing_action = overrides.value().resolve(InputActions{});
        ASSERT_TRUE(missing_action);
        ASSERT_EQ(missing_action.value().issues.size(), 1u);
        const auto defaults = InputActions::create(
            {{"renamed_jump", Type::Button, {{Input::Key::Space, 1, 0, id(2)}}, {}, id(1)}});
        ASSERT_TRUE(defaults);
        const auto compatible = overrides.value().resolve(defaults.value());
        ASSERT_TRUE(compatible);
        EXPECT_TRUE(compatible.value().issues.empty());
        EXPECT_EQ(compatible.value().actions.actions()[0].bindings[0].control,
            InputActions::Control(Input::Key::J));
    }

    TEST(InputOverridesTest, InvalidCompositionKeepsTheWholeDefaultBindingAndOtherValidOverrides) {
        const auto defaults = InputActions::create({{"move", Type::Axis,
            {{Input::GamepadAxis::LeftX, 0.7f, 0.2f, id(2)},
                {Input::GamepadAxis::LeftY, 0.8f, 0.1f, id(3)}},
            {}, id(1)}});
        ASSERT_TRUE(defaults);
        const auto overrides = InputOverrides::create({{id(1), Type::Axis, false,
            {{.id = id(2), .control = Input::Key::J, .scale = -1},
                {.id = id(3), .control = Input::GamepadAxis::RightY}}}});
        ASSERT_TRUE(overrides);
        const auto original = overrides.value();
        const auto result = overrides.value().resolve(defaults.value());
        ASSERT_TRUE(result);
        ASSERT_EQ(result.value().issues.size(), 1u);
        EXPECT_EQ(result.value().issues[0].action, id(1));
        EXPECT_EQ(result.value().issues[0].binding, id(2));
        EXPECT_EQ(result.value().actions.actions()[0].bindings[0],
            defaults.value().actions()[0].bindings[0]);
        EXPECT_EQ(result.value().actions.actions()[0].bindings[1].control,
            InputActions::Control(Input::GamepadAxis::RightY));
        EXPECT_EQ(overrides.value(), original);
    }

    TEST(InputOverridesTest, RuntimeBindingConstraintsAreAppliedDuringResolution) {
        const auto defaults = InputActions::create(
            {{"jump", Type::Button, {{Input::Key::Space, 1, 0, id(2)}}, {}, id(1)}});
        ASSERT_TRUE(defaults);
        const std::array cases{Binding{.id = id(2), .control = Input::GamepadAxis::LeftX},
            Binding{.id = id(2), .control = InputActions::Motion::ScrollY},
            Binding{.id = id(2), .scale = 2}, Binding{.id = id(2), .scale = 101},
            Binding{.id = id(2), .deadzone = -1}, Binding{.id = id(2), .deadzone = 1}};
        for(const auto& binding : cases) {
            const auto overrides =
                InputOverrides::create({{id(1), Type::Button, false, {binding}}});
            ASSERT_TRUE(overrides) << overrides.error();
            const auto result = overrides.value().resolve(defaults.value());
            ASSERT_TRUE(result) << result.error();
            EXPECT_EQ(result.value().actions, defaults.value());
            ASSERT_EQ(result.value().issues.size(), 1u);
            EXPECT_EQ(result.value().issues[0].binding, id(2));
        }
    }

    TEST(InputOverridesTest, ResolutionRejectsAnonymousDefaultsEvenWithNoOverrides) {
        const auto anonymous_action =
            InputActions::create({{"jump", Type::Button, {{Input::Key::Space, 1, 0, id(2)}}}});
        ASSERT_TRUE(anonymous_action);
        EXPECT_FALSE(InputOverrides{}.resolve(anonymous_action.value()));
        const auto anonymous_binding =
            InputActions::create({{"jump", Type::Button, {{Input::Key::Space}}, {}, id(1)}});
        ASSERT_TRUE(anonymous_binding);
        EXPECT_FALSE(InputOverrides{}.resolve(anonymous_binding.value()));
    }

    TEST(InputOverridesTest, RejectsMissingAndDuplicateIdentitiesButScopesBindingIdsPerAction) {
        EXPECT_FALSE(InputOverrides::create({{{}, Type::Button, true, {}}}));
        EXPECT_FALSE(InputOverrides::create(
            {{id(1), Type::Button, true, {}}, {id(1), Type::Axis, true, {}}}));
        EXPECT_FALSE(
            InputOverrides::create({{id(1), Type::Button, false, {{.control = Input::Key::J}}}}));
        const Binding binding{.id = id(2), .disabled = true};
        EXPECT_FALSE(InputOverrides::create({{id(1), Type::Button, false, {binding, binding}}}));
        EXPECT_TRUE(InputOverrides::create(
            {{id(1), Type::Button, false, {binding}}, {id(3), Type::Button, false, {binding}}}));
    }

    TEST(InputOverridesTest, RejectsEmptyAndContradictoryRecords) {
        EXPECT_FALSE(InputOverrides::create({{id(1), Type::Button, false, {}}}));
        EXPECT_FALSE(InputOverrides::create(
            {{id(1), Type::Button, true, {{.id = id(2), .disabled = true}}}}));
        const std::array bindings{Binding{.id = id(2)},
            Binding{.id = id(2), .control = Input::Key::J, .disabled = true},
            Binding{.id = id(2), .scale = 1, .disabled = true},
            Binding{.id = id(2), .deadzone = 0, .disabled = true}};
        for(const auto& binding : bindings)
            EXPECT_FALSE(InputOverrides::create({{id(1), Type::Button, false, {binding}}}));
    }

    TEST(InputOverridesTest, RejectsInvalidTypesControlsAndNonfiniteFields) {
        EXPECT_FALSE(InputOverrides::create({{id(1), static_cast<Type>(99), true, {}}}));
        const std::array<InputActions::Control, 7> controls{Input::Key::Unknown, Input::Key::Count,
            Input::MouseButton::Count, Input::GamepadButton::Count, Input::GamepadAxis::Count,
            static_cast<InputActions::Motion>(-1), static_cast<InputActions::Motion>(4)};
        for(const auto& control : controls)
            EXPECT_FALSE(InputOverrides::create(
                {{id(1), Type::Button, false, {{.id = id(2), .control = control}}}}));
        for(const float value : {std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            EXPECT_FALSE(InputOverrides::create(
                {{id(1), Type::Axis, false, {{.id = id(2), .scale = value}}}}));
            EXPECT_FALSE(InputOverrides::create(
                {{id(1), Type::Axis, false, {{.id = id(2), .deadzone = value}}}}));
        }
    }

    TEST(InputOverridesTest, EnforcesActionAndBindingRecordCapacity) {
        std::vector<Action> actions;
        for(unsigned index = 0; index < InputActions::MAX_ACTIONS; ++index)
            actions.push_back({id(index + 1), Type::Button, true, {}});
        EXPECT_TRUE(InputOverrides::create(actions));
        actions.push_back({id(InputActions::MAX_ACTIONS + 1), Type::Button, true, {}});
        EXPECT_FALSE(InputOverrides::create(std::move(actions)));

        Action action{.id = id(1)};
        for(unsigned index = 0; index < InputActions::MAX_BINDINGS; ++index)
            action.bindings.push_back({.id = id(index + 1), .disabled = true});
        EXPECT_TRUE(InputOverrides::create({action}));
        action.bindings.push_back({.id = id(InputActions::MAX_BINDINGS + 1), .disabled = true});
        EXPECT_FALSE(InputOverrides::create({std::move(action)}));
    }
}
