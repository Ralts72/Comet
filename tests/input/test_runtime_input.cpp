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
}
