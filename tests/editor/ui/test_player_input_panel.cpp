#ifdef COMET_TEST_EDITOR_UI
#include "common/file_io.h"
#include "input/player_input_settings.h"
#include "input_widgets.h"
#include "player_input_panel.h"
#include "support/imgui_context.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

        ImGuiWindow* find_child(ImGuiWindow* parent, const char* name) {
            if(!parent)
                return nullptr;
            for(auto* candidate : ImGui::GetCurrentContext()->Windows)
                if(candidate->ParentWindow == parent && candidate->ChildId == parent->GetID(name))
                    return candidate;
            return nullptr;
        }

        bool has_visible_text(const ImGuiWindow& window) {
            const auto color = ImGui::GetColorU32(ImGuiCol_Text);
            const auto& draw = *window.DrawList;
            for(const auto& command : draw.CmdBuffer) {
                const ImRect clip(command.ClipRect);
                for(unsigned index = command.IdxOffset;
                    index < command.IdxOffset + command.ElemCount; ++index) {
                    const auto& vertex = draw.VtxBuffer[command.VtxOffset + draw.IdxBuffer[index]];
                    if(vertex.col == color && clip.Contains(vertex.pos)
                        && window.InnerClipRect.Contains(vertex.pos))
                        return true;
                }
            }
            return false;
        }

        template<typename Frame>
        std::optional<ImVec2> find_hover_point(ImGuiWindow* owner, ImGuiID item, Frame frame) {
            if(!owner)
                return std::nullopt;
            const auto* viewport = ImGui::GetMainViewport();
            const float left = std::max(owner->InnerClipRect.Min.x, viewport->WorkPos.x);
            const float right =
                std::min(owner->InnerClipRect.Max.x, viewport->WorkPos.x + viewport->WorkSize.x);
            const float top = std::max(owner->InnerClipRect.Min.y, viewport->WorkPos.y);
            const float bottom =
                std::min(owner->InnerClipRect.Max.y, viewport->WorkPos.y + viewport->WorkSize.y);
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

        void show(const Overrides& current = {}, std::span<const Input::Key> reserved_keys = {}) {
            panel.open(defaults, current, reserved_keys);
            frame();
            frame();
            ASSERT_TRUE(panel.is_open());
            ASSERT_TRUE(blocked);
        }

        ImGuiWindow* window() { return ImGui::FindWindowByName("###Player Input"); }

        ImGuiWindow* child(ImGuiWindow* parent, const char* name) {
            return find_child(parent, name);
        }

        ImGuiWindow* content() { return child(window(), "Content"); }
        ImGuiWindow* bindings() { return child(content(), "Bindings"); }
        ImGuiWindow* diagnostics() { return child(content(), "Diagnostics"); }

        ImGuiID binding_item(Comet::Uuid binding, const char* english) {
            const auto scope = ImHashStr(binding.to_string().c_str(), 0, bindings()->ID);
            return ImHashStr((std::string("###") + english).c_str(), 0, scope);
        }

        ImGuiID diagnostic_item(Comet::Uuid action, Comet::Uuid binding = {}) {
            const auto action_scope = ImHashStr(action.to_string().c_str(), 0, diagnostics()->ID);
            const auto binding_scope = ImHashStr(binding.to_string().c_str(), 0, action_scope);
            return ImHashStr("###Remove Override", 0, binding_scope);
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

        void remove_override(Comet::Uuid action, Comet::Uuid binding = {}) {
            ASSERT_NE(diagnostics(), nullptr);
            ImGui::SetScrollY(content(), content()->ScrollMax.y);
            ImGui::SetScrollY(diagnostics(), 0);
            frame();
            activate(diagnostics(), diagnostic_item(action, binding));
            frame();
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

        std::optional<ImVec2> hover_point(
            ImGuiWindow* owner, ImGuiID item, const PlayerInputPanel::Text& translations = {}) {
            return find_hover_point(owner, item, [this, &translations] { frame(translations); });
        }

        void mouse_click(ImVec2 point, const PlayerInputPanel::Text& translations = {}) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame(translations);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            physical.mouse_button_event(Input::MouseButton::Left, true);
            frame(translations);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            physical.mouse_button_event(Input::MouseButton::Left, false);
            frame(translations);
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

    TEST_F(PlayerInputPanelTest, RemovingUnknownActionPreservesOtherRecordsThroughCancelAndReopen) {
        const auto current = Overrides::create({{id(90), Type::Button, true, {}},
            {id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K}}},
            {id(4), Type::Axis, false, {{.id = id(5), .scale = -1}}},
            {id(91), Type::Button, true, {}}});
        ASSERT_TRUE(current);
        Comet::Tests::TemporaryDirectory directory;
        const auto file = directory.path() / "input.json";
        auto settings = Comet::PlayerInputSettings::load(id(20), file);
        ASSERT_TRUE(settings);
        ASSERT_TRUE(settings.value().save(current.value()));
        const auto original = Comet::read_text_file(file);
        ASSERT_TRUE(original);
        const auto timestamp = std::filesystem::last_write_time(file);

        show(settings.value().overrides());
        remove_override(id(90));
        EXPECT_EQ(rendered_text.find(id(90).to_string()), std::string::npos);
        EXPECT_NE(rendered_text.find(id(91).to_string()), std::string::npos);
        EXPECT_FALSE(panel.take_request());
        button("Cancel");
        EXPECT_FALSE(panel.is_open());
        EXPECT_TRUE(blocked);
        EXPECT_FALSE(panel.take_request());
        EXPECT_EQ(Comet::read_text_file(file).value(), original.value());
        EXPECT_EQ(std::filesystem::last_write_time(file), timestamp);
        frame();

        show(settings.value().overrides());
        EXPECT_NE(rendered_text.find(id(90).to_string()), std::string::npos);
        remove_override(id(90));
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        auto expected = current.value().actions();
        expected.erase(expected.begin());
        EXPECT_EQ(request->actions(), expected);
        const auto saved = settings.value().save(*request);
        ASSERT_TRUE(saved) << saved.error();
        panel.complete(saved);
        frame();
        const auto reopened = Comet::PlayerInputSettings::load(id(20), file);
        ASSERT_TRUE(reopened);
        EXPECT_EQ(reopened.value().overrides().actions(), expected);
        show(reopened.value().overrides());
        EXPECT_EQ(rendered_text.find(id(90).to_string()), std::string::npos);
        EXPECT_NE(rendered_text.find(id(91).to_string()), std::string::npos);
    }

    TEST_F(PlayerInputPanelTest, RemovingUnknownBindingsScopesIdsAndPrunesOnlyTheEmptyAction) {
        auto actions = defaults.actions();
        actions.push_back({"fire", Type::Button, {{Input::Key::F, 1, 0, id(11)}}, "", id(10)});
        const auto configured = Actions::create(std::move(actions), defaults.contexts());
        ASSERT_TRUE(configured);
        defaults = configured.value();
        const auto current = Overrides::create({{id(1), Type::Button, false,
                                                    {{.id = id(2), .control = Input::Key::K},
                                                        {.id = id(99), .control = Input::Key::P}}},
            {id(4), Type::Axis, false, {{.id = id(99), .scale = -1}}},
            {id(10), Type::Button, false,
                {{.id = id(11), .control = Input::Key::R, .disabled = true}}}});
        ASSERT_TRUE(current);
        show(current.value());
        remove_override(id(1), id(99));
        button("Apply");
        const auto first = panel.take_request();
        ASSERT_TRUE(first);
        auto expected = current.value().actions();
        expected[0].bindings.pop_back();
        EXPECT_EQ(first->actions(), expected);
        panel.complete(Comet::Result<void>::success());
        frame();

        show(*first);
        remove_override(id(4), id(99));
        button("Apply");
        const auto second = panel.take_request();
        ASSERT_TRUE(second);
        expected.erase(expected.begin() + 1);
        EXPECT_EQ(second->actions(), expected);
        const auto resolved = second->resolve(defaults);
        ASSERT_TRUE(resolved);
        EXPECT_TRUE(resolved.value().issues.empty());
        EXPECT_EQ(resolved.value().actions.actions()[0].bindings[0].control,
            Actions::Control(Input::Key::K));
        EXPECT_EQ(resolved.value().actions.actions()[1], defaults.actions()[1]);
        EXPECT_TRUE(resolved.value().actions.actions()[2].bindings.empty());
    }

    TEST_F(PlayerInputPanelTest, EmptyDefaultsKeepTranslatedDiagnosticRemovalMouseReachable) {
        defaults = {};
        ImGui::GetIO().DisplaySize = {480, 320};
        const auto current = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K}}},
                {id(4), Type::Axis, true, {}}});
        ASSERT_TRUE(current);
        show(current.value());
        EXPECT_NE(rendered_text.find("No input actions."), std::string::npos);
        ASSERT_NE(diagnostics(), nullptr);
        const auto item = diagnostic_item(id(1));
        const PlayerInputPanel::Text translations{{"Remove Override", "Remove this record"}};
        ImGui::SetScrollY(content(), content()->ScrollMax.y);
        frame(translations);
        frame(translations);
        EXPECT_GT(content()->Scroll.y, 0);
        EXPECT_GT(diagnostics()->ScrollMax.y, 0);
        EXPECT_EQ(diagnostic_item(id(1)), item);
        EXPECT_NE(rendered_text.find("Remove this record"), std::string::npos);
        const auto point = hover_point(diagnostics(), item, translations);
        ASSERT_TRUE(point);
        mouse_click(*point, translations);
        frame(translations);
        EXPECT_EQ(rendered_text.find(id(1).to_string()), std::string::npos);
        EXPECT_NE(rendered_text.find(id(4).to_string()), std::string::npos);
        EXPECT_TRUE(panel.is_open());
        EXPECT_TRUE(blocked);
        EXPECT_FALSE(panel.take_request());
        remove_override(id(4));
        EXPECT_EQ(rendered_text.find("Unknown input action ID"), std::string::npos);
        click_footer("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
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

    TEST_F(PlayerInputPanelTest, ReservedKeyPolicyIsOwnedAndReopeningWithoutItAllowsTheKey) {
        {
            std::vector<Input::Key> reserved{Input::Key::J};
            show({}, reserved);
            reserved[0] = Input::Key::K;
            record(Input::Key::J);
            EXPECT_NE(
                rendered_text.find("This key is reserved. Use another key."), std::string::npos);
            EXPECT_NE(
                rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        }
        key(Input::Key::K);
        EXPECT_EQ(rendered_text.find("This key is reserved. Use another key."), std::string::npos);
        button("Apply");
        const auto first = panel.take_request();
        ASSERT_TRUE(first);
        const auto expected = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*first, expected.value());
        panel.complete(Comet::Result<void>::success());
        frame();

        show();
        EXPECT_EQ(rendered_text.find("Reserved keys:"), std::string::npos);
        record(Input::Key::J);
        button("Apply");
        const auto reopened = panel.take_request();
        ASSERT_TRUE(reopened);
        auto allowed = expected.value().actions();
        allowed[0].bindings[0].control = Input::Key::J;
        EXPECT_EQ(reopened->actions(), allowed);
    }

    TEST_F(PlayerInputPanelTest, ReservedDropdownChoiceIsDisabledAndRecordingCanContinue) {
        const Input::Key reserved[]{Input::Key::J, Input::Key::Escape};
        show({}, reserved);
        EXPECT_NE(rendered_text.find("Reserved keys:"), std::string::npos);
        binding_button("Control");
        auto* combo = ImGui::FindWindowByName("##Combo_00");
        ASSERT_NE(combo, nullptr);
        ASSERT_TRUE(combo->Active);
        ImGui::SetScrollY(combo, ImGui::GetTextLineHeightWithSpacing() * 8);
        frame();
        activate(combo, combo->GetID("###J"));
        frame();
        EXPECT_TRUE(combo->Active);
        activate(combo, combo->GetID("###K"));
        frame();
        EXPECT_FALSE(combo->Active);
        button("Apply");
        const auto selected = panel.take_request();
        ASSERT_TRUE(selected);
        const auto expected = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(2), .control = Input::Key::K}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*selected, expected.value());
        panel.complete(Comet::Result<void>::success());
        frame();

        show({}, reserved);
        record(Input::Key::J);
        EXPECT_NE(rendered_text.find("This key is reserved. Use another key."), std::string::npos);
        EXPECT_NE(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        key(Input::Key::Space);
        EXPECT_EQ(rendered_text.find("This key is reserved. Use another key."), std::string::npos);
        EXPECT_EQ(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        binding_button("Record Key");
        key(Input::Key::Escape);
        EXPECT_TRUE(panel.is_open());
        EXPECT_EQ(rendered_text.find("Press a key; Escape cancels recording."), std::string::npos);
        EXPECT_EQ(rendered_text.find("This key is reserved. Use another key."), std::string::npos);
        button("Apply");
        const auto recorded = panel.take_request();
        ASSERT_TRUE(recorded);
        EXPECT_TRUE(recorded->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, ExistingReservedDefaultsAndOverridesRemainRestorableAndSavable) {
        const Input::Key reserved[]{Input::Key::Space, Input::Key::J};
        const auto current = Overrides::create({{id(1), Type::Button, false,
            {{.id = id(3), .control = Input::Key::J, .scale = 1, .deadzone = 0}}}});
        ASSERT_TRUE(current);
        const std::string warning = "This binding uses a reserved key and will not reach the game.";
        show(current.value(), reserved);
        const auto first_warning = rendered_text.find(warning);
        ASSERT_NE(first_warning, std::string::npos);
        EXPECT_NE(rendered_text.find(warning, first_warning + warning.size()), std::string::npos);
        button("Apply");
        const auto unchanged = panel.take_request();
        ASSERT_TRUE(unchanged);
        EXPECT_EQ(*unchanged, current.value());
        panel.complete(Comet::Result<void>::success());
        frame();

        show(*unchanged, reserved);
        binding_button("Disable Binding", id(3));
        button("Apply");
        const auto disabled = panel.take_request();
        ASSERT_TRUE(disabled);
        auto expected = current.value().actions();
        expected[0].bindings[0].disabled = true;
        EXPECT_EQ(disabled->actions(), expected);
        panel.complete(Comet::Result<void>::success());
        frame();

        show(*disabled, reserved);
        binding_button("Restore Binding", id(3));
        frame();
        const auto remaining_warning = rendered_text.find(warning);
        ASSERT_NE(remaining_warning, std::string::npos);
        EXPECT_EQ(
            rendered_text.find(warning, remaining_warning + warning.size()), std::string::npos);
        button("Apply");
        const auto restored = panel.take_request();
        ASSERT_TRUE(restored);
        EXPECT_TRUE(restored->actions().empty());
        const auto effective = restored->resolve(defaults);
        ASSERT_TRUE(effective);
        EXPECT_EQ(effective.value().actions, defaults);
        panel.complete(Comet::Result<void>::success());
        frame();

        show({}, reserved);
        select_binding_choice("Source", "key", id(3));
        button("Apply");
        const auto changed_source = panel.take_request();
        ASSERT_TRUE(changed_source);
        const auto available_key = Overrides::create(
            {{id(1), Type::Button, false, {{.id = id(3), .control = Input::Key::A}}}});
        ASSERT_TRUE(available_key);
        EXPECT_EQ(*changed_source, available_key.value());
    }

    TEST_F(PlayerInputPanelTest, RestoreBindingDeletesEmptyActionAndInheritsFutureDefaults) {
        const auto current = Overrides::create({{id(1), Type::Button, false,
            {{.id = id(2), .control = Input::Key::K, .disabled = true}}}});
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
        const auto current = Overrides::create(
            {{id(1), Type::Button, true, {{.id = id(2), .control = Input::Key::K}}},
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
        const auto current = Overrides::create(
            {{id(1), Type::Axis, true, {{.id = id(2), .control = Input::Key::K, .disabled = true}}},
                {id(99), Type::Button, true, {}}});
        ASSERT_TRUE(current);
        show(current.value());
        EXPECT_NE(rendered_text.find("Restore this action before editing incompatible overrides."),
            std::string::npos);
        EXPECT_EQ(
            rendered_text.find("Disabled; personal overrides are preserved."), std::string::npos);
        button("Restore All");
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, DisablingAndReenablingBindingPreservesRecordedControl) {
        show();
        record(Input::Key::K);
        binding_button("Disable Binding");
        frame({{"Space", "Hidden default Space"}, {"K", "Hidden personal K"}});
        EXPECT_NE(
            rendered_text.find("Disabled; personal overrides are preserved."), std::string::npos);
        EXPECT_EQ(rendered_text.find("Hidden default Space"), std::string::npos);
        EXPECT_EQ(rendered_text.find("Hidden personal K"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        const auto expected = Overrides::create({{id(1), Type::Button, false,
            {{.id = id(2), .control = Input::Key::K, .disabled = true}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*request, expected.value());
        const auto resolved = request->resolve(defaults);
        ASSERT_TRUE(resolved);
        ASSERT_EQ(resolved.value().actions.actions()[0].bindings.size(), 1u);
        EXPECT_EQ(resolved.value().actions.actions()[0].bindings[0].id, id(3));

        panel.complete(Comet::Result<void>::success());
        frame();
        show(*request);
        binding_button("Disable Binding");
        button("Apply");
        const auto enabled = panel.take_request();
        ASSERT_TRUE(enabled);
        auto restored = request->actions();
        restored[0].bindings[0].disabled = false;
        EXPECT_EQ(enabled->actions(), restored);
        const auto effective = enabled->resolve(defaults);
        ASSERT_TRUE(effective);
        EXPECT_TRUE(effective.value().issues.empty());
        EXPECT_EQ(effective.value().actions.actions()[0].bindings[0].control,
            Actions::Control(Input::Key::K));
        EXPECT_EQ(
            effective.value().actions.actions()[0].bindings[1], defaults.actions()[0].bindings[1]);
    }

    TEST_F(PlayerInputPanelTest, ActionDisablePreservesKeyboardAxisTuningAndBindingDisabledFlags) {
        const auto configured = Actions::create({{"move", Type::Axis,
            {{Input::Key::A, -1, 0, id(2)}, {Input::GamepadAxis::LeftX, 1, 0.2f, id(3)}}, "",
            id(1)}});
        ASSERT_TRUE(configured);
        defaults = configured.value();
        const auto current = Overrides::create({{id(1), Type::Axis, false,
            {{.id = id(2), .control = Input::Key::K, .scale = -0.5f},
                {.id = id(3),
                    .control = Input::GamepadAxis::RightX,
                    .scale = 1.5f,
                    .deadzone = 0.35f,
                    .disabled = true}}}});
        ASSERT_TRUE(current);
        show(current.value());
        button("Disable Action");
        EXPECT_NE(
            rendered_text.find("Disabled; personal overrides are preserved."), std::string::npos);
        EXPECT_EQ(rendered_text.find("Multiplier"), std::string::npos);
        EXPECT_EQ(rendered_text.find("Record Key"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        auto disabled = current.value().actions();
        disabled[0].disabled = true;
        EXPECT_EQ(request->actions(), disabled);
        const auto resolved = request->resolve(defaults);
        ASSERT_TRUE(resolved);
        EXPECT_TRUE(resolved.value().actions.actions()[0].bindings.empty());

        panel.complete(Comet::Result<void>::success());
        frame();
        show(*request);
        button("Disable Action");
        button("Apply");
        const auto enabled = panel.take_request();
        ASSERT_TRUE(enabled);
        EXPECT_EQ(*enabled, current.value());
    }

    TEST_F(PlayerInputPanelTest, EnablingPureDisabledRecordsReturnsToInheritedDefaults) {
        const auto current = Overrides::create({{id(1), Type::Button, true, {}},
            {id(4), Type::Axis, false, {{.id = id(5), .disabled = true}}}});
        ASSERT_TRUE(current);
        show(current.value());
        button("Disable Action");
        EXPECT_NE(rendered_text.find("Inherits project default"), std::string::npos);
        select_action("move", id(4));
        binding_button("Disable Binding", id(5));
        EXPECT_NE(rendered_text.find("Inherits project default"), std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, ReenablingAfterDefaultSourceChangesPreservesRejectedFields) {
        auto newer = defaults.actions();
        newer[1].bindings[0].control = Input::Key::Space;
        newer[1].bindings[0].deadzone = 0;
        const auto configured = Actions::create(std::move(newer), defaults.contexts());
        ASSERT_TRUE(configured);
        defaults = configured.value();
        const auto current = Overrides::create({{id(4), Type::Axis, false,
            {{.id = id(5), .scale = -0.5f, .deadzone = 0.3f, .disabled = true}}}});
        ASSERT_TRUE(current);
        show(current.value());
        select_action("move", id(4));
        EXPECT_NE(
            rendered_text.find("Disabled; personal overrides are preserved."), std::string::npos);
        binding_button("Disable Binding", id(5));
        frame({{"Space", "Effective Space"}});
        EXPECT_NE(rendered_text.find("Override ignored; using project default"), std::string::npos);
        EXPECT_NE(rendered_text.find("Effective Space"), std::string::npos);
        button("Apply");
        const auto enabled = panel.take_request();
        ASSERT_TRUE(enabled);
        auto expected = current.value().actions();
        expected[0].bindings[0].disabled = false;
        EXPECT_EQ(enabled->actions(), expected);
        const auto fallback = enabled->resolve(defaults);
        ASSERT_TRUE(fallback);
        ASSERT_EQ(fallback.value().issues.size(), 1u);
        EXPECT_EQ(fallback.value().actions, defaults);

        panel.complete(Comet::Result<void>::success());
        frame();
        show(*enabled);
        select_action("move", id(4));
        binding_button("Disable Binding", id(5));
        button("Apply");
        const auto disabled = panel.take_request();
        ASSERT_TRUE(disabled);
        EXPECT_EQ(*disabled, current.value());
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
        ImGui::GetIO().DisplaySize = {480, 320};
        frame();
        frame();
        click_footer("Apply");
        const auto first = panel.take_request();
        ASSERT_TRUE(first);
        std::string error = "Settings changed externally\n";
        for(int line = 0; line < 20; ++line)
            error += "The player settings file was changed by another application.\n";
        panel.complete(Comet::Result<void>::failure(error));
        frame();
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_NE(rendered_text.find("Settings changed externally"), std::string::npos);
        auto* errors = child(window(), "Error");
        ASSERT_NE(errors, nullptr);
        EXPECT_TRUE(errors->Active);
        EXPECT_GT(errors->ScrollMax.y, 0);
        EXPECT_LE(errors->Size.y, 96);
        EXPECT_GT(errors->InnerClipRect.GetHeight(), ImGui::GetFontSize());
        EXPECT_LE(errors->Pos.y + errors->Size.y, content()->Pos.y);
        EXPECT_TRUE(has_visible_text(*errors));
        ImGui::SetScrollY(content(), content()->ScrollMax.y);
        frame();
        EXPECT_GT(content()->Scroll.y, 0);
        EXPECT_TRUE(has_visible_text(*errors));
        expect_footer_reachable();
        click_footer("Apply");
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

    TEST_F(PlayerInputPanelTest, PublishedFocusLossCancelsCaptureWhenUiMissesTheUnfocusedFrames) {
        const auto current =
            Overrides::create({{id(1), Type::Button, false,
                                   {{.id = id(2), .control = Input::Key::J},
                                       {.id = id(3), .control = Input::GamepadButton::West}}},
                {id(4), Type::Axis, false, {{.id = id(5), .scale = -1}}}});
        ASSERT_TRUE(current);
        for(const bool gamepad : {false, true}) {
            SCOPED_TRACE(gamepad);
            physical = {};
            physical.focus_event(true);
            Input::GamepadSample pad;
            physical.gamepad_sample(0, pad);
            show(current.value());
            const char* prompt = "Press a key; Escape cancels recording.";
            if(gamepad) {
                binding_button("Record Button", id(3));
                prompt = "Press a gamepad button; Escape cancels recording.";
            } else {
                binding_button("Record Key");
            }
            ASSERT_NE(rendered_text.find(prompt), std::string::npos);

            // 物理快照持续发布，但 UI 跳过失焦与恢复首帧。
            physical.focus_event(false);
            EXPECT_FALSE(physical.publish_frame().focused);
            physical.focus_event(true);
            physical.gamepad_sample(0, pad);
            EXPECT_TRUE(physical.publish_frame().focused);
            if(gamepad) {
                pad.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = true;
                physical.gamepad_sample(0, pad);
            } else {
                physical.key_event(Input::Key::K, true);
            }
            frame();
            if(gamepad)
                EXPECT_TRUE(
                    physical.get_frame().gamepads[0].button(Input::GamepadButton::East).pressed);
            else
                EXPECT_TRUE(physical.get_frame().key(Input::Key::K).pressed);
            EXPECT_TRUE(panel.is_open());
            EXPECT_TRUE(blocked);
            EXPECT_EQ(rendered_text.find(prompt), std::string::npos);
            EXPECT_FALSE(panel.take_request());
            button("Apply");
            const auto request = panel.take_request();
            ASSERT_TRUE(request);
            EXPECT_EQ(*request, current.value());
            panel.complete(Comet::Result<void>::success());
            frame();
            frame();
            EXPECT_FALSE(panel.is_open());
            EXPECT_FALSE(blocked);
        }
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

    TEST_F(
        PlayerInputPanelTest, RecordedGamepadButtonPreservesSparseFieldsAndCannotLeakThroughGate) {
        auto actions = defaults.actions();
        actions.push_back({"confirm", Type::Button, {{Input::GamepadButton::East, 1, 0, id(11)}},
            "menu", id(10)});
        const auto configured =
            Actions::create(std::move(actions), {{"gameplay"}, {"menu", true, 10, true}});
        ASSERT_TRUE(configured);
        defaults = configured.value();
        Input::GamepadSample pad;
        physical.gamepad_sample(2, pad);
        frame();
        Input::Gate gate;
        gate.read(physical.get_frame(), !blocked);
        const auto current = Overrides::create({{id(1), Type::Button, false,
            {{.id = id(3), .scale = 1, .deadzone = 0}, {.id = id(99), .control = Input::Key::J}}}});
        ASSERT_TRUE(current);
        show(current.value());
        EXPECT_FALSE(gate.read(physical.get_frame(), !blocked).focused);
        binding_button("Record Button", id(3));
        ASSERT_NE(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);

        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = true;
        physical.gamepad_sample(2, pad);
        frame();
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        EXPECT_NE(rendered_text.find("gamepad_button/East: confirm [menu]"), std::string::npos);
        EXPECT_NE(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        const auto captured = gate.read(physical.get_frame(), !blocked);
        EXPECT_FALSE(captured.gamepads[2].button(Input::GamepadButton::East).down);
        EXPECT_FALSE(captured.gamepads[2].button(Input::GamepadButton::East).pressed);

        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        auto expected = current.value().actions();
        expected[0].bindings[0].control = Input::GamepadButton::East;
        EXPECT_EQ(request->actions(), expected);
        panel.complete(Comet::Result<void>::success());
        frame();
        EXPECT_TRUE(blocked);
        EXPECT_FALSE(gate.read(physical.get_frame(), !blocked)
                .gamepads[2]
                .button(Input::GamepadButton::East)
                .pressed);
        frame();
        EXPECT_FALSE(blocked);
        EXPECT_FALSE(gate.read(physical.get_frame(), !blocked)
                .gamepads[2]
                .button(Input::GamepadButton::East)
                .down);
        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = false;
        physical.gamepad_sample(2, pad);
        frame();
        EXPECT_FALSE(gate.read(physical.get_frame(), !blocked)
                .gamepads[2]
                .button(Input::GamepadButton::East)
                .released);
        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = true;
        physical.gamepad_sample(2, pad);
        frame();
        EXPECT_TRUE(gate.read(physical.get_frame(), !blocked)
                .gamepads[2]
                .button(Input::GamepadButton::East)
                .pressed);
    }

    TEST_F(PlayerInputPanelTest, GamepadRecordingIgnoresHeldOpeningFrameAndOtherDevices) {
        Input::GamepadSample first;
        first.buttons[static_cast<std::size_t>(Input::GamepadButton::North)] = true;
        Input::GamepadSample other;
        physical.gamepad_sample(4, first);
        physical.gamepad_sample(7, other);
        show();
        first.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = true;
        physical.gamepad_sample(4, first);
        binding_button("Record Button", id(3));
        frame();
        key(Input::Key::K);
        other.buttons[static_cast<std::size_t>(Input::GamepadButton::West)] = true;
        physical.gamepad_sample(7, other);
        frame();
        EXPECT_NE(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);

        first.buttons.fill(false);
        physical.gamepad_sample(4, first);
        frame();
        first.buttons[static_cast<std::size_t>(Input::GamepadButton::North)] = true;
        physical.gamepad_sample(4, first);
        frame();
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        const auto expected = Overrides::create({{id(1), Type::Button, false,
            {{.id = id(3), .control = Input::GamepadButton::North}}}});
        ASSERT_TRUE(expected);
        EXPECT_EQ(*request, expected.value());
    }

    TEST_F(PlayerInputPanelTest, MissingGamepadDoesNotArmAndEscapeCancelsWithoutClosingThePanel) {
        show();
        binding_button("Record Button", id(3));
        frame();
        EXPECT_NE(rendered_text.find("No gamepad connected."), std::string::npos);
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        Input::GamepadSample pad;
        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::North)] = true;
        physical.gamepad_sample(0, pad);
        frame();
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        binding_button("Record Button", id(3));
        frame();
        ASSERT_NE(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        key(Input::Key::Escape);
        EXPECT_TRUE(panel.is_open());
        EXPECT_TRUE(blocked);
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = true;
        physical.gamepad_sample(0, pad);
        frame();
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, GamepadDisconnectAndEarlierSlotConnectionCancelRecording) {
        Input::GamepadSample pad;
        physical.gamepad_sample(4, pad);
        show();
        binding_button("Record Button", id(3));
        physical.gamepad_sample(4, std::nullopt);
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);

        physical.gamepad_sample(6, pad);
        frame();
        binding_button("Record Button", id(3));
        ASSERT_NE(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        physical.gamepad_sample(2, pad);
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = true;
        physical.gamepad_sample(6, pad);
        physical.gamepad_sample(2, pad);
        frame();
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
    }

    TEST_F(PlayerInputPanelTest, FocusLossAndSamplingInterruptionCancelGamepadRecording) {
        Input::GamepadSample pad;
        physical.gamepad_sample(0, pad);
        show();
        binding_button("Record Button", id(3));
        physical.focus_event(false);
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);

        physical.focus_event(true);
        physical.gamepad_sample(0, pad);
        frame();
        binding_button("Record Button", id(3));
        ASSERT_NE(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        physical.discard_pending();
        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::East)] = true;
        physical.gamepad_sample(0, pad);
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_TRUE(physical.get_frame().focused);
        EXPECT_EQ(rendered_text.find("Press a gamepad button; Escape cancels recording."),
            std::string::npos);
        pad.buttons[static_cast<std::size_t>(Input::GamepadButton::North)] = true;
        physical.gamepad_sample(0, pad);
        frame();
        button("Apply");
        const auto request = panel.take_request();
        ASSERT_TRUE(request);
        EXPECT_TRUE(request->actions().empty());
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

    class PlayerInputErrorTest: public testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui{{800, 600}};
        std::string error;
        std::string rendered_text;
        bool blocked = false;

        ImGuiWindow* window() { return ImGui::FindWindowByName("###Input Settings Error"); }

        void frame(bool close_requested = false, const Translations& translations = {}) {
            ImGui::NewFrame();
            ImGui::LogToBuffer(0);
            blocked = render_player_input_error(error, close_requested, translations);
            rendered_text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
            ImGui::Render();
        }

        std::optional<ImVec2> close_point() {
            auto* modal = window();
            if(!modal)
                return std::nullopt;
            return find_hover_point(modal, modal->GetID("###Close"), [this] { frame(); });
        }

        void expect_visible() {
            ASSERT_NE(window(), nullptr);
            EXPECT_TRUE(blocked);
            const auto* viewport = ImGui::GetMainViewport();
            EXPECT_GE(window()->Pos.x, viewport->WorkPos.x);
            EXPECT_GE(window()->Pos.y, viewport->WorkPos.y);
            EXPECT_LE(
                window()->Pos.x + window()->Size.x, viewport->WorkPos.x + viewport->WorkSize.x);
            EXPECT_LE(
                window()->Pos.y + window()->Size.y, viewport->WorkPos.y + viewport->WorkSize.y);
            EXPECT_EQ(window()->ScrollMax.y, 0);
            auto* content = find_child(window(), "Content");
            ASSERT_NE(content, nullptr);
            EXPECT_GT(content->InnerClipRect.GetHeight(), ImGui::GetFontSize());
            EXPECT_TRUE(has_visible_text(*content));
            const auto point = close_point();
            ASSERT_TRUE(point);
            EXPECT_GE(point->y, content->Pos.y + content->Size.y);
        }

        void click_close() {
            const auto point = close_point();
            ASSERT_TRUE(point);
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point->x, point->y);
            frame();
            ASSERT_EQ(ImGui::GetCurrentContext()->HoveredId, window()->GetID("###Close"));
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
        }
    };

    TEST_F(PlayerInputErrorTest, LongErrorKeepsTextAndMouseCloseVisibleAfterViewportShrinks) {
        for(int line = 0; line < 30; ++line)
            error += "Cannot read the player settings file in this project configuration.\n";
        frame();
        frame();
        ImGui::GetIO().DisplaySize = {480, 320};
        frame();
        frame();
        expect_visible();
        EXPECT_GT(find_child(window(), "Content")->ScrollMax.y, 0);
        click_close();
        EXPECT_TRUE(error.empty());
        EXPECT_TRUE(blocked);
        EXPECT_FALSE(ImGui::IsPopupOpen("###Input Settings Error", ImGuiPopupFlags_AnyPopupId));
        frame();
        EXPECT_FALSE(blocked);
    }

    TEST_F(PlayerInputErrorTest, TranslationsKeepIdentityAndExternalClearClosesThePopup) {
        frame();
        EXPECT_FALSE(blocked);
        error = "Original file diagnostic";
        frame();
        frame();
        const auto* original_window = window();
        {
            const Translations translations{{"Input Settings Error", "Personal settings error"},
                {"Close", "Dismiss"}, {error, "This replacement must not appear"}};
            frame(false, translations);
            EXPECT_EQ(window(), original_window);
            EXPECT_NE(rendered_text.find("Dismiss"), std::string::npos);
            EXPECT_NE(rendered_text.find(error), std::string::npos);
            EXPECT_EQ(rendered_text.find("This replacement must not appear"), std::string::npos);
            expect_visible();
        }
        error.clear();
        frame();
        EXPECT_TRUE(blocked);
        EXPECT_FALSE(ImGui::IsPopupOpen("###Input Settings Error", ImGuiPopupFlags_AnyPopupId));
        frame();
        EXPECT_FALSE(blocked);
    }

    TEST_F(PlayerInputErrorTest, CorruptPlayerFileStaysUntouchedThroughErrorAndCloseRequest) {
        Comet::Tests::TemporaryDirectory directory;
        const auto file = directory.path() / "input.json";
        const std::string contents = "{ broken player input";
        const auto written = Comet::write_text_file_atomic(file, contents);
        ASSERT_TRUE(written) << written.error();
        const auto timestamp = std::filesystem::last_write_time(file);
        const auto loaded = Comet::PlayerInputSettings::load(id(20), file);
        ASSERT_FALSE(loaded);
        error = loaded.error();
        frame();
        frame();
        expect_visible();
        EXPECT_NE(rendered_text.find(file.string()), std::string::npos);
        frame(true);
        EXPECT_TRUE(blocked);
        EXPECT_TRUE(error.empty());
        EXPECT_FALSE(ImGui::IsPopupOpen("###Input Settings Error", ImGuiPopupFlags_AnyPopupId));
        frame();
        EXPECT_FALSE(blocked);
        const auto preserved = Comet::read_text_file(file);
        ASSERT_TRUE(preserved) << preserved.error();
        EXPECT_EQ(preserved.value(), contents);
        EXPECT_EQ(std::filesystem::last_write_time(file), timestamp);
    }
}
#endif
