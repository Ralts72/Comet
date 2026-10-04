#ifdef COMET_TEST_EDITOR_UI
#include "player_input_panel.h"
#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace CometUi::Tests {
    namespace {
        using Actions = Comet::InputActions;
        using Overrides = Comet::InputOverrides;
        using Input = Comet::Input;
        using Type = Actions::Type;

        constexpr Comet::Uuid id(unsigned value) {
            Comet::Uuid::Bytes bytes{};
            bytes.back() = static_cast<std::uint8_t>(value);
            return Comet::Uuid(bytes);
        }
    }

    class PlayerInputPanelTest: public testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui{{1100, 800}};
        PlayerInputPanel panel;
        Actions defaults;
        Input physical;
        bool blocked = false;
        std::string rendered_text;

        void SetUp() override {
            const auto configured = Actions::create(
                {{"jump", Type::Button,
                     {{Input::Key::Space, 1, 0, id(2)}, {Input::GamepadButton::South, 1, 0, id(3)}},
                     "gameplay", id(1)},
                    {"move", Type::Axis, {{Input::GamepadAxis::LeftX, 1, 0.2f, id(5)}}, "gameplay",
                        id(4)}},
                {{"gameplay"}});
            ASSERT_TRUE(configured);
            defaults = configured.value();
            physical.focus_event(true);
        }

        void frame(const PlayerInputPanel::Text& translations = {}) {
            ImGui::NewFrame();
            ImGui::LogToBuffer(0);
            blocked = panel.render(physical.publish_frame(), translations);
            rendered_text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
            ImGui::Render();
        }

        void show(const Overrides& current = {}) {
            panel.open(defaults, current);
            frame();
            frame();
            ASSERT_TRUE(panel.is_open());
            ASSERT_TRUE(blocked);
        }

        ImGuiWindow* window() { return ImGui::FindWindowByName("###Player Input"); }

        ImGuiWindow* child(ImGuiWindow* parent, const char* name) {
            if(!parent)
                return nullptr;
            for(auto* candidate : ImGui::GetCurrentContext()->Windows)
                if(candidate->ParentWindow == parent && candidate->ChildId == parent->GetID(name))
                    return candidate;
            return nullptr;
        }

        ImGuiWindow* content() { return child(window(), "Content"); }
        ImGuiWindow* bindings() { return child(content(), "Bindings"); }

        ImGuiID binding_item(Comet::Uuid binding, const char* english) {
            const auto scope = ImHashStr(binding.to_string().c_str(), 0, bindings()->ID);
            return ImHashStr((std::string("###") + english).c_str(), 0, scope);
        }

        void activate(ImGuiWindow* owner, ImGuiID item) {
            ASSERT_NE(owner, nullptr);
            ImGui::FocusWindow(owner);
            ImGui::ActivateItemByID(item);
            frame();
        }

        void button(const char* english) {
            const std::string_view name = english;
            auto* owner = content();
            if(name == "Apply" || name == "Cancel" || name == "Restore All")
                owner = window();
            ASSERT_NE(owner, nullptr);
            activate(owner, owner->GetID((std::string("###") + english).c_str()));
        }

        void binding_button(const char* english, Comet::Uuid binding = id(2)) {
            ASSERT_NE(bindings(), nullptr);
            activate(bindings(), binding_item(binding, english));
        }

        void key(Input::Key value) {
            physical.key_event(value, true);
            frame();
            physical.key_event(value, false);
            frame();
        }

        void record(Input::Key value) {
            binding_button("Record Key");
            ASSERT_NE(
                rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
            key(value);
        }

        void select_action(const char* name, Comet::Uuid action) {
            button("Action");
            auto* combo = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(combo, nullptr);
            const auto scope = ImHashStr(action.to_string().c_str(), 0, combo->ID);
            activate(combo, ImHashStr(name, 0, scope));
        }

        void select_binding_choice(const char* field, const char* choice, Comet::Uuid binding) {
            binding_button(field, binding);
            auto* combo = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(combo, nullptr);
            ASSERT_TRUE(combo->Active);
            activate(combo, combo->GetID((std::string("###") + choice).c_str()));
        }

        void edit_number(const char* field, const char* value, Comet::Uuid binding) {
            binding_button(field, binding);
            const auto item = binding_item(binding, field);
            ASSERT_EQ(ImGui::GetActiveID(), item);
            auto& io = ImGui::GetIO();
            const auto modifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(modifier, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(modifier, false);
            frame();
            io.AddInputCharactersUTF8(value);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
        }

        std::optional<ImVec2> hover_point(ImGuiWindow* owner, ImGuiID item) {
            if(!owner)
                return std::nullopt;
            const auto* viewport = ImGui::GetMainViewport();
            const float left = std::max(owner->ClipRect.Min.x, viewport->WorkPos.x);
            const float right =
                std::min(owner->ClipRect.Max.x, viewport->WorkPos.x + viewport->WorkSize.x);
            const float top = std::max(owner->ClipRect.Min.y, viewport->WorkPos.y);
            const float bottom =
                std::min(owner->ClipRect.Max.y, viewport->WorkPos.y + viewport->WorkSize.y);
            for(float y = bottom - 3; y > top; y -= 6) {
                for(float x = left + 3; x < right; x += 6) {
                    ImGui::GetIO().AddMousePosEvent(x, y);
                    frame();
                    if(ImGui::GetCurrentContext()->HoveredId == item)
                        return ImVec2{x, y};
                }
            }
            return std::nullopt;
        }

        void mouse_click(ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            physical.mouse_button_event(Input::MouseButton::Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            physical.mouse_button_event(Input::MouseButton::Left, false);
            frame();
        }

        void click_footer(const char* english) {
            ASSERT_NE(window(), nullptr);
            const auto item = window()->GetID((std::string("###") + english).c_str());
            const auto point = hover_point(window(), item);
            ASSERT_TRUE(point) << english;
            mouse_click(*point);
        }

        void expect_footer_reachable() {
            ASSERT_NE(window(), nullptr);
            ASSERT_NE(content(), nullptr);
            const auto* viewport = ImGui::GetMainViewport();
            EXPECT_GE(window()->Pos.x, viewport->WorkPos.x);
            EXPECT_GE(window()->Pos.y, viewport->WorkPos.y);
            EXPECT_LE(
                window()->Pos.x + window()->Size.x, viewport->WorkPos.x + viewport->WorkSize.x);
            EXPECT_LE(
                window()->Pos.y + window()->Size.y, viewport->WorkPos.y + viewport->WorkSize.y);
            EXPECT_EQ(window()->ScrollMax.y, 0);
            EXPECT_GT(content()->ScrollMax.y, 0);
            for(const char* english : {"Apply", "Cancel", "Restore All"}) {
                const auto item = window()->GetID((std::string("###") + english).c_str());
                EXPECT_TRUE(hover_point(window(), item)) << english;
            }
        }
    };

    TEST_F(PlayerInputPanelTest, UnchangedApplyPreservesUnknownAndTypeChangedRecords) {
        const auto current = Overrides::create({{id(90), Type::Button, true, {}},
            {id(1), Type::Button, false, {{.id = id(91), .control = Input::Key::J}}},
            {id(4), Type::Button, true, {}}});
        ASSERT_TRUE(current);
        show(current.value());
        EXPECT_NE(rendered_text.find("Unknown input action ID"), std::string::npos);
        EXPECT_NE(rendered_text.find("Input action type changed"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(*request, current.value());
        EXPECT_FALSE(panel.take_request());
        EXPECT_TRUE(panel.is_open());
    }

    TEST_F(PlayerInputPanelTest, RecordingChangesOnlyControlAndKeepsExistingSparseFields) {
        const auto current = Overrides::create({{id(1), Type::Button, false,
            {{.id = id(2), .scale = 1, .deadzone = 0}, {.id = id(99), .control = Input::Key::J}}}});
        ASSERT_TRUE(current);
        show(current.value());
        record(Input::Key::K);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        auto expected = current.value().actions();
        expected[0].bindings[0].control = Input::Key::K;
        EXPECT_EQ(request->actions(), expected);
        const auto resolved = request->resolve(defaults);
        ASSERT_TRUE(resolved);
        EXPECT_EQ(
            resolved.value().actions.actions()[0].bindings[1], defaults.actions()[0].bindings[1]);
        EXPECT_EQ(resolved.value().actions.contexts(), defaults.contexts());
    }

    TEST_F(PlayerInputPanelTest, RestoreBindingDeletesEmptyActionAndInheritsFutureDefaults) {
        const auto current = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K}}}});
        ASSERT_TRUE(current);
        show(current.value());
        binding_button("Restore Binding");
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
        auto changed = defaults.actions();
        changed[0].bindings[0].control = Input::Key::J;
        const auto newer = Actions::create(std::move(changed), defaults.contexts());
        ASSERT_TRUE(newer);
        const auto resolved = request->resolve(newer.value());
        ASSERT_TRUE(resolved);
        EXPECT_EQ(resolved.value().actions, newer.value());
    }

    TEST_F(PlayerInputPanelTest, RestoreActionPreservesUnrelatedRecords) {
        const auto current = Overrides::create({{id(1), Type::Button, true, {}},
            {id(4), Type::Axis, false, {{.id = id(5), .scale = -1}}},
            {id(99), Type::Button, true, {}}});
        ASSERT_TRUE(current);
        show(current.value());
        button("Restore Action");
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        auto expected = current.value().actions();
        expected.erase(expected.begin());
        EXPECT_EQ(request->actions(), expected);
    }

    TEST_F(PlayerInputPanelTest, RestoreAllAlsoRemovesIncompatibleRecords) {
        const auto current =
            Overrides::create({{id(1), Type::Axis, true, {}}, {id(99), Type::Button, true, {}}});
        ASSERT_TRUE(current);
        show(current.value());
        EXPECT_NE(rendered_text.find("Restore this action before editing incompatible overrides."),
            std::string::npos);
        button("Restore All");
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, DisablesBindingWithoutChangingOtherBindings) {
        show();
        binding_button("Disable Binding");
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        const auto expected =
            Overrides::create({{id(1), Type::Button, false, {{.id = id(2), .disabled = true}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*request, expected.value());
        const auto resolved = request->resolve(defaults);
        ASSERT_TRUE(resolved);
        ASSERT_EQ(resolved.value().actions.actions()[0].bindings.size(), 1u);
        EXPECT_EQ(resolved.value().actions.actions()[0].bindings[0].id, id(3));
    }

    TEST_F(PlayerInputPanelTest, DisablesActionWithAnExplicitMarker) {
        show();
        button("Disable Action");
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        const auto expected = Overrides::create({{id(1), Type::Button, true, {}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*request, expected.value());
    }

    TEST_F(PlayerInputPanelTest, CancelDiscardsDraftAndBlocksTheClosingFrame) {
        show();
        record(Input::Key::K);
        button("Cancel");
        EXPECT_FALSE(panel.is_open());
        EXPECT_TRUE(blocked);
        EXPECT_FALSE(panel.take_request());
        frame();
        EXPECT_FALSE(blocked);
        show();
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, FailedApplyKeepsDraftAndSuccessClosesAtTheNextUiBoundary) {
        show();
        record(Input::Key::K);
        button("Apply");
        const auto first = panel.take_request();
        ASSERT_TRUE(first);
        panel.complete(Comet::Result<void>::failure("Settings changed externally"));
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_NE(rendered_text.find("Settings changed externally"), std::string::npos);
        button("Apply");
        const auto retried = panel.take_request();
        ASSERT_TRUE(retried);
        EXPECT_EQ(*retried, *first);
        panel.complete(Comet::Result<void>::success());
        EXPECT_FALSE(panel.is_open());
        frame();
        EXPECT_TRUE(blocked);
        frame();
        EXPECT_FALSE(blocked);
    }

    TEST_F(PlayerInputPanelTest, EscapeCancelsRecordingBeforeClosingTheModal) {
        show();
        binding_button("Record Key");
        key(Input::Key::Escape);
        EXPECT_TRUE(panel.is_open());
        EXPECT_EQ(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        physical.key_event(Input::Key::Escape, true);
        frame();
        EXPECT_FALSE(panel.is_open());
        EXPECT_TRUE(blocked);
        EXPECT_FALSE(panel.take_request());
        frame();
        EXPECT_FALSE(blocked);
    }

    TEST_F(PlayerInputPanelTest, FocusLossCancelsRecordingButKeepsTheModal) {
        show();
        binding_button("Record Key");
        physical.focus_event(false);
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_EQ(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        physical.focus_event(true);
        key(Input::Key::K);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, SamplingInterruptionCancelsRecordingWithoutAnUnfocusedUiFrame) {
        show();
        binding_button("Record Key");
        // 最小化期间宿主不绘制 UI；恢复后快照仍可 focused。
        physical.discard_pending();
        physical.key_event(Input::Key::K, true);
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_TRUE(blocked);
        EXPECT_EQ(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        physical.key_event(Input::Key::K, false);
        frame();
        key(Input::Key::J);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, RecordingIgnoresItsOpeningFrameAndUsesPhysicalModifierIdentity) {
        show();
        ImGui::GetIO().ConfigMacOSXBehaviors = true;
        physical.key_event(Input::Key::Enter, true);
        binding_button("Record Key");
        EXPECT_NE(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        physical.key_event(Input::Key::Enter, false);
        frame();
        key(Input::Key::LeftControl);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        ASSERT_EQ(request->actions().size(), 1u);
        ASSERT_EQ(request->actions()[0].bindings.size(), 1u);
        EXPECT_EQ(request->actions()[0].bindings[0].control,
            std::optional<Actions::Control>(Input::Key::LeftControl));
    }

    TEST_F(PlayerInputPanelTest, NonKeyboardChoicesAndNumbersProduceValidSparseOverrides) {
        show();
        select_action("move", id(4));
        select_binding_choice("Control", "RightX", id(5));
        edit_number("Multiplier", "-0.5", id(5));
        edit_number("Deadzone", "0.3", id(5));
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        const auto expected = Overrides::create({{id(4), Type::Axis, false,
            {{.id = id(5),
                .control = Input::GamepadAxis::RightX,
                .scale = -0.5f,
                .deadzone = 0.3f}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*request, expected.value());
    }

    TEST_F(PlayerInputPanelTest, ChangingAxisSourceClearsOnlyTheIncompatibleDeadzone) {
        show();
        select_action("move", id(4));
        select_binding_choice("Source", "mouse_button", id(5));
        select_binding_choice("Control", "Right", id(5));
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        const auto expected = Overrides::create({{id(4), Type::Axis, false,
            {{.id = id(5), .control = Input::MouseButton::Right, .deadzone = 0}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*request, expected.value());
        const auto resolved = request->resolve(defaults);
        ASSERT_TRUE(resolved);
        EXPECT_TRUE(resolved.value().issues.empty());
    }

    TEST_F(PlayerInputPanelTest, InvalidNumericEditReportsFailureWithoutReplacingValidDraft) {
        show();
        select_action("move", id(4));
        edit_number("Deadzone", "1.5", id(5));
        EXPECT_NE(rendered_text.find("Invalid binding for action"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, RecordedBindingImmediatelyUpdatesRelationshipsAndStillApplies) {
        auto actions = defaults.actions();
        actions.push_back(
            {"confirm", Type::Button, {{Input::Key::K, 1, 0, id(11)}}, "menu", id(10)});
        const auto configured =
            Actions::create(std::move(actions), {{"gameplay"}, {"menu", true, 10, true}});
        ASSERT_TRUE(configured);
        defaults = configured.value();
        show();
        EXPECT_NE(rendered_text.find("No overlapping bindings."), std::string::npos);

        binding_button("Record Key");
        physical.key_event(Input::Key::K, true);
        frame();
        EXPECT_NE(rendered_text.find("key/K: confirm [menu]"), std::string::npos);
        EXPECT_NE(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        EXPECT_EQ(rendered_text.find("No overlapping bindings."), std::string::npos);
        physical.key_event(Input::Key::K, false);
        frame();

        select_action("confirm", id(10));
        EXPECT_NE(rendered_text.find("key/K: jump [gameplay]"), std::string::npos);
        EXPECT_NE(
            rendered_text.find("Consumes this control from the other action"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        const auto expected = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*request, expected.value());
    }

    TEST_F(PlayerInputPanelTest, DisabledAndRestoredOverridesRecomputeInheritedRelationships) {
        auto actions = defaults.actions();
        actions.push_back(
            {"interact", Type::Button, {{Input::Key::Space, 1, 0, id(11)}}, "gameplay", id(10)});
        const auto configured = Actions::create(std::move(actions), defaults.contexts());
        ASSERT_TRUE(configured);
        defaults = configured.value();
        const auto current = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K}}}});
        ASSERT_TRUE(current);
        show(current.value());
        EXPECT_NE(rendered_text.find("No overlapping bindings."), std::string::npos);

        binding_button("Restore Binding");
        EXPECT_NE(rendered_text.find("key/Space: interact [gameplay]"), std::string::npos);
        binding_button("Disable Binding");
        EXPECT_NE(rendered_text.find("No overlapping bindings."), std::string::npos);
        EXPECT_EQ(rendered_text.find("key/Space: interact [gameplay]"), std::string::npos);
        binding_button("Restore Binding");
        EXPECT_NE(rendered_text.find("key/Space: interact [gameplay]"), std::string::npos);
        button("Disable Action");
        EXPECT_NE(rendered_text.find("No overlapping bindings."), std::string::npos);
        EXPECT_EQ(rendered_text.find("key/Space: interact [gameplay]"), std::string::npos);
        button("Restore Action");
        EXPECT_NE(rendered_text.find("key/Space: interact [gameplay]"), std::string::npos);

        record(Input::Key::K);
        EXPECT_NE(rendered_text.find("No overlapping bindings."), std::string::npos);
        button("Restore All");
        frame();
        EXPECT_NE(rendered_text.find("key/Space: interact [gameplay]"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(
        PlayerInputPanelTest, RelationshipsExplainSharedPriorityCommonAndInitiallyDisabledGroups) {
        const auto configured = Actions::create(
            {{"jump", Type::Button, {{Input::Key::Space, 1, 0, id(2)}}, "gameplay", id(1)},
                {"interact", Type::Button, {{Input::Key::Space, 1, 0, id(11)}}, "walk", id(10)},
                {"help", Type::Button, {{Input::Key::Space, 1, 0, id(21)}}, "", id(20)}},
            {{"gameplay", true, 10, true}, {"walk", false, 10, true}});
        ASSERT_TRUE(configured);
        defaults = configured.value();
        show();
        const PlayerInputPanel::Text translations{{"Shared", "Shared at equal priority"},
            {"Common (Always Enabled)", "Common group"}, {"Initially disabled:", "Starts off:"}};
        frame(translations);
        EXPECT_NE(rendered_text.find("Binding Relationships"), std::string::npos);
        EXPECT_NE(rendered_text.find(
                      "Pairwise rules when both contexts are enabled; not current runtime state."),
            std::string::npos);
        EXPECT_NE(rendered_text.find("key/Space: interact [walk]"), std::string::npos);
        EXPECT_NE(rendered_text.find("Shared at equal priority"), std::string::npos);
        EXPECT_NE(rendered_text.find("Starts off: walk"), std::string::npos);
        EXPECT_NE(rendered_text.find("key/Space: help [Common group]"), std::string::npos);
        EXPECT_NE(
            rendered_text.find("Shared (common action bypasses consumption)"), std::string::npos);
        EXPECT_EQ(
            rendered_text.find("Consumes this control from the other action"), std::string::npos);
        EXPECT_EQ(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        EXPECT_FALSE(panel.take_request());
    }

    TEST_F(PlayerInputPanelTest, RelationshipsUseValidFallbackAndPreserveIncompatibleRecords) {
        auto actions = defaults.actions();
        actions.push_back(
            {"confirm", Type::Button, {{Input::Key::Space, 1, 0, id(11)}}, "gameplay", id(10)});
        const auto configured = Actions::create(std::move(actions), defaults.contexts());
        ASSERT_TRUE(configured);
        defaults = configured.value();
        const auto current =
            Overrides::create({{id(1), Type::Button, false,
                                   {{.id = id(2), .control = Input::Key::J, .scale = 2},
                                       {.id = id(99), .control = Input::Key::P}}},
                {id(10), Type::Axis, true, {}}, {id(90), Type::Button, true, {}}});
        ASSERT_TRUE(current);
        show(current.value());
        EXPECT_NE(rendered_text.find("key/Space: confirm [gameplay]"), std::string::npos);
        EXPECT_EQ(rendered_text.find("key/J: confirm [gameplay]"), std::string::npos);
        EXPECT_NE(rendered_text.find("keeping the default binding"), std::string::npos);
        EXPECT_NE(rendered_text.find("Unknown input binding ID"), std::string::npos);
        EXPECT_NE(rendered_text.find("Input action type changed"), std::string::npos);
        EXPECT_NE(rendered_text.find("Unknown input action ID"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(*request, current.value());
    }

    TEST_F(
        PlayerInputPanelTest, SmallViewportKeepsRestoreAndApplyReachableOutsideScrollingContent) {
        ImGui::GetIO().DisplaySize = {480, 320};
        const auto current = Overrides::create({{id(1), Type::Button, false,
            {{.id = id(2), .control = Input::Key::K}, {.id = id(99), .control = Input::Key::J}}}});
        ASSERT_TRUE(current);
        show(current.value());
        expect_footer_reachable();
        click_footer("Restore All");
        EXPECT_TRUE(panel.is_open());
        EXPECT_FALSE(panel.take_request());
        click_footer("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
        EXPECT_TRUE(blocked);
    }

    TEST_F(PlayerInputPanelTest, ShrinkingAnOpenModalKeepsCancelVisibleAndBlocksItsClosingFrame) {
        show();
        record(Input::Key::K);
        ImGui::GetIO().DisplaySize = {480, 320};
        frame();
        frame();
        expect_footer_reachable();
        click_footer("Cancel");
        EXPECT_FALSE(panel.is_open());
        EXPECT_TRUE(blocked);
        EXPECT_FALSE(panel.take_request());
        frame();
        EXPECT_FALSE(blocked);
    }

    TEST_F(PlayerInputPanelTest, MousePressAndReleaseStartsRecordingUntilAPhysicalKeyArrives) {
        show();
        ASSERT_NE(bindings(), nullptr);
        const auto point = hover_point(bindings(), binding_item(id(2), "Record Key"));
        ASSERT_TRUE(point);
        mouse_click(*point);
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_TRUE(blocked);
        ASSERT_NE(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        key(Input::Key::K);
        EXPECT_EQ(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        const auto expected = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*request, expected.value());
    }

    TEST_F(PlayerInputPanelTest, BindingStatusDistinguishesInheritedPersonalAndRestoredValues) {
        const auto configured = Actions::create(
            {{"jump", Type::Button, {{Input::Key::Space, 1, 0, id(2)}}, "", id(1)}});
        ASSERT_TRUE(configured);
        defaults = configured.value();
        show();
        EXPECT_NE(rendered_text.find("Inherits project default"), std::string::npos);
        EXPECT_EQ(rendered_text.find("Personal override"), std::string::npos);
        record(Input::Key::K);
        EXPECT_NE(rendered_text.find("Personal override"), std::string::npos);
        EXPECT_EQ(rendered_text.find("Inherits project default"), std::string::npos);
        binding_button("Restore Binding");
        frame();
        EXPECT_NE(rendered_text.find("Inherits project default"), std::string::npos);
        EXPECT_EQ(rendered_text.find("Personal override"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, RejectedOverrideShowsEffectiveDefaultWithoutDiscardingTheRecord) {
        const auto configured = Actions::create(
            {{"jump", Type::Button, {{Input::Key::Space, 1, 0, id(2)}}, "", id(1)}});
        ASSERT_TRUE(configured);
        defaults = configured.value();
        const auto current = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K, .scale = 2}}}});
        ASSERT_TRUE(current);
        show(current.value());
        frame({{"Space", "Effective Space"}, {"K", "Rejected K"}});
        EXPECT_NE(rendered_text.find("Override ignored; using project default"), std::string::npos);
        EXPECT_NE(rendered_text.find("Effective Space"), std::string::npos);
        EXPECT_EQ(rendered_text.find("Rejected K"), std::string::npos);
        EXPECT_EQ(rendered_text.find("Personal override"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_EQ(*request, current.value());
    }

    TEST_F(PlayerInputPanelTest, TranslationsAreBorrowedPerFrameAndKeepWidgetIdentity) {
        show();
        const auto* original_window = window();
        {
            const PlayerInputPanel::Text translations{{"Player Input", "Personal Controls"},
                {"Apply", "Use bindings"}, {"Disable Action", "Turn off action"}};
            frame(translations);
            EXPECT_EQ(window(), original_window);
            EXPECT_NE(rendered_text.find("Use bindings"), std::string::npos);
        }
        button("Apply");
        EXPECT_EQ(window(), original_window);
        EXPECT_TRUE(panel.take_request());
    }
}
#endif
