#include "input/player_input_edit.h"

#include <gtest/gtest.h>

#include <limits>

namespace Comet {
    namespace {
        constexpr Uuid id(const unsigned value) {
            Uuid::Bytes bytes{};
            bytes[15] = static_cast<std::uint8_t>(value);
            return Uuid(bytes);
        }
    }

    class PlayerInputEditTest: public testing::Test {
    protected:
        PlayerInputEdit edit;
        InputActions defaults;
        Input physical;

        void SetUp() override {
            auto configured = InputActions::create(
                {{"jump", InputActions::Type::Button,
                     {{Input::Key::Space, 1, 0, id(2)}, {Input::GamepadButton::South, 1, 0, id(3)}},
                     "player", id(1)},
                    {"move", InputActions::Type::Axis,
                        {{Input::GamepadAxis::LeftX, 1, 0.2f, id(5)}}, "player", id(4)}},
                {{"player", true, 10, true}});
            ASSERT_TRUE(configured);
            defaults = std::move(configured).value();
            edit.reset(defaults, {});
            physical.focus_event(true);
            physical.publish_frame();
        }

        const Input::Frame& press_key(const Input::Key key) {
            physical.key_event(key, true);
            return physical.publish_frame();
        }

        void start_key_capture(const Input::Frame& opening) {
            edit.start_capture(id(1), id(2), opening, PlayerInputEdit::CaptureKind::Keyboard);
        }

        void start_gamepad_capture() {
            edit.start_capture(
                id(1), id(3), physical.get_frame(), PlayerInputEdit::CaptureKind::GamepadButton);
        }
    };

    TEST_F(PlayerInputEditTest, DraftKeepsSparseIdentityAndUnrelatedIncompatibleRecords) {
        const auto current = InputOverrides::create({{id(90), InputActions::Type::Button, true, {}},
            {id(1), InputActions::Type::Button, false,
                {{.id = id(2), .scale = 1}, {.id = id(99), .control = Input::Key::J}}}});
        ASSERT_TRUE(current);
        edit.reset(defaults, current.value());
        edit.change_control(id(1), id(2), Input::Key::K);
        auto expected = current.value().actions();
        expected[1].bindings[0].control = Input::Key::K;
        EXPECT_EQ(edit.draft().actions(), expected);
        ASSERT_TRUE(edit.resolution());
        EXPECT_EQ(edit.resolution().value().issues.size(), 2u);
        edit.restore_binding(id(1), id(99));
        EXPECT_EQ(edit.draft().actions().size(), 2u);
        edit.restore_action(id(90));
        ASSERT_TRUE(edit.resolution());
        EXPECT_TRUE(edit.resolution().value().issues.empty());
        edit.restore_binding(id(1), id(2));
        EXPECT_TRUE(edit.draft().actions().empty());
        EXPECT_EQ(edit.resolution().value().actions, defaults);
    }

    TEST_F(PlayerInputEditTest, InvalidCandidateAndUnknownIdentityKeepAcceptedDraft) {
        edit.change_scale(id(4), id(5), -0.5f);
        const auto accepted = edit.draft();
        edit.change_deadzone(id(4), id(5), 2);
        EXPECT_FALSE(edit.error().empty());
        EXPECT_EQ(edit.draft(), accepted);
        edit.change_scale(id(4), id(5), std::numeric_limits<float>::infinity());
        EXPECT_FALSE(edit.error().empty());
        EXPECT_EQ(edit.draft(), accepted);
        edit.change_control(id(4), id(99), Input::Key::K);
        EXPECT_FALSE(edit.error().empty());
        EXPECT_EQ(edit.draft(), accepted);
        edit.change_control(id(99), id(5), Input::Key::K);
        EXPECT_EQ(edit.draft(), accepted);
    }

    TEST_F(PlayerInputEditTest, ChangedActionTypeRequiresExplicitRestoreBeforeEditing) {
        const auto current = InputOverrides::create({{id(1), InputActions::Type::Axis, true, {}}});
        ASSERT_TRUE(current);
        edit.reset(defaults, current.value());
        edit.change_control(id(1), id(2), Input::Key::K);
        edit.disable_action(id(1), false);
        EXPECT_EQ(edit.draft(), current.value());
        EXPECT_FALSE(edit.error().empty());
        edit.restore_action(id(1));
        edit.change_control(id(1), id(2), Input::Key::K);
        EXPECT_TRUE(edit.error().empty());
        ASSERT_TRUE(edit.resolution());
        EXPECT_TRUE(edit.resolution().value().issues.empty());
    }

    TEST_F(PlayerInputEditTest, SourceRoundTripRestoresDeadzoneInheritanceAndPreservesScale) {
        edit.change_scale(id(4), id(5), -0.5f);
        edit.change_control(id(4), id(5), Input::Key::D);
        const auto keyboard = edit.binding_patch(id(4), id(5));
        EXPECT_EQ(keyboard.deadzone, 0);
        EXPECT_EQ(keyboard.scale, -0.5f);
        ASSERT_TRUE(edit.resolution());
        EXPECT_TRUE(edit.resolution().value().issues.empty());
        edit.change_control(id(4), id(5), Input::GamepadAxis::RightX);
        const auto axis = edit.binding_patch(id(4), id(5));
        EXPECT_FALSE(axis.deadzone);
        EXPECT_EQ(axis.scale, -0.5f);
        EXPECT_FLOAT_EQ(edit.resolution().value().actions.actions()[1].bindings[0].deadzone, 0.2f);
    }

    TEST_F(PlayerInputEditTest, DisabledBindingRetainsPersonalFieldsUntilExplicitRestore) {
        edit.change_control(id(1), id(2), Input::Key::K);
        edit.disable_binding(id(1), id(2), true);
        EXPECT_EQ(edit.binding_patch(id(1), id(2)).control,
            std::optional<InputActions::Control>(Input::Key::K));
        ASSERT_TRUE(edit.resolution());
        ASSERT_EQ(edit.resolution().value().actions.actions()[0].bindings.size(), 1u);
        edit.disable_binding(id(1), id(2), false);
        EXPECT_EQ(edit.resolution().value().actions.actions()[0].bindings[0].control,
            InputActions::Control(Input::Key::K));
        edit.restore_all();
        EXPECT_TRUE(edit.draft().actions().empty());
        EXPECT_EQ(edit.resolution().value().actions, defaults);
    }

    TEST_F(PlayerInputEditTest, ApplyFreezesCandidateAndFailureAllowsRetryWithoutUi) {
        edit.change_control(id(1), id(2), Input::Key::K);
        const auto accepted = edit.draft();
        edit.apply();
        EXPECT_TRUE(edit.waiting());
        const auto first = edit.take_request();
        ASSERT_TRUE(first);
        EXPECT_EQ(*first, accepted);
        EXPECT_FALSE(edit.take_request());
        edit.change_control(id(1), id(2), Input::Key::J);
        edit.restore_all();
        edit.apply();
        EXPECT_EQ(edit.draft(), accepted);
        EXPECT_FALSE(edit.take_request());
        EXPECT_FALSE(edit.complete(Result<void>::failure("Settings changed externally")));
        EXPECT_FALSE(edit.waiting());
        EXPECT_EQ(edit.draft(), accepted);
        EXPECT_EQ(edit.error(), "Settings changed externally");
        edit.apply();
        const auto retried = edit.take_request();
        ASSERT_TRUE(retried);
        EXPECT_EQ(*retried, *first);
        EXPECT_TRUE(edit.complete(Result<void>::success()));
        EXPECT_FALSE(edit.complete(Result<void>::success()));
    }

    TEST_F(PlayerInputEditTest, CaptureOwnsReservedPolicyAndIgnoresItsOpeningFrame) {
        std::vector<Input::Key> reserved{Input::Key::J};
        edit.reset(defaults, {}, reserved);
        reserved[0] = Input::Key::K;
        const auto& opening = press_key(Input::Key::Enter);
        start_key_capture(opening);
        edit.capture_input(opening, true);
        EXPECT_TRUE(edit.capture());
        EXPECT_TRUE(edit.draft().actions().empty());
        edit.capture_input(press_key(Input::Key::J), true);
        EXPECT_TRUE(edit.capture());
        EXPECT_FALSE(edit.error().empty());
        EXPECT_TRUE(edit.draft().actions().empty());
        edit.capture_input(press_key(Input::Key::LeftControl), true);
        EXPECT_FALSE(edit.capture());
        EXPECT_TRUE(edit.error().empty());
        EXPECT_EQ(edit.binding_patch(id(1), id(2)).control,
            std::optional<InputActions::Control>(Input::Key::LeftControl));
    }

    TEST_F(PlayerInputEditTest, CaptureCancelsWhenConsumerLosesOwnershipOrMissesInterruption) {
        start_key_capture(physical.get_frame());
        edit.capture_input(press_key(Input::Key::K), false);
        EXPECT_FALSE(edit.capture());
        EXPECT_TRUE(edit.draft().actions().empty());
        start_key_capture(physical.get_frame());
        physical.focus_event(false);
        physical.publish_frame();
        physical.focus_event(true);
        physical.publish_frame();
        edit.capture_input(press_key(Input::Key::J), true);
        EXPECT_FALSE(edit.capture());
        EXPECT_TRUE(edit.draft().actions().empty());
    }

    TEST_F(PlayerInputEditTest, EscapeCancelsCaptureAndClearDiscardsUnsubmittedDraft) {
        start_key_capture(physical.get_frame());
        edit.capture_input(press_key(Input::Key::Escape), true);
        EXPECT_FALSE(edit.capture());
        EXPECT_TRUE(edit.draft().actions().empty());
        edit.change_control(id(1), id(2), Input::Key::K);
        edit.apply();
        edit.clear();
        EXPECT_FALSE(edit.waiting());
        EXPECT_FALSE(edit.take_request());
        EXPECT_TRUE(edit.draft().actions().empty());
    }

    TEST_F(PlayerInputEditTest, GamepadCaptureLocksConnectionAndCancelsMissedReconnect) {
        Input::GamepadSample pad;
        physical.gamepad_sample(0, pad);
        physical.gamepad_sample(1, pad);
        physical.publish_frame();
        start_gamepad_capture();
        ASSERT_TRUE(edit.capture());
        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = true;
        physical.gamepad_sample(1, pad);
        edit.capture_input(physical.publish_frame(), true);
        EXPECT_TRUE(edit.capture());
        EXPECT_TRUE(edit.draft().actions().empty());
        physical.gamepad_sample(0, std::nullopt);
        physical.publish_frame();
        physical.gamepad_sample(0, Input::GamepadSample{});
        physical.publish_frame();
        physical.gamepad_sample(0, pad);
        edit.capture_input(physical.publish_frame(), true);
        EXPECT_FALSE(edit.capture());
        EXPECT_TRUE(edit.draft().actions().empty());
        start_gamepad_capture();
        physical.gamepad_sample(0, Input::GamepadSample{});
        physical.publish_frame();
        physical.gamepad_sample(0, pad);
        edit.capture_input(physical.publish_frame(), true);
        EXPECT_FALSE(edit.capture());
        EXPECT_EQ(edit.binding_patch(id(1), id(3)).control,
            std::optional<InputActions::Control>(Input::GamepadButton::East));
    }
}
