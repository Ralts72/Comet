#ifdef COMET_TEST_EDITOR_UI
#include "project/input_settings_panel.h"
#include "project/project_creation.h"
#include "project/project_settings.h"
#include "asset/database.h"
#include "common/file_io.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "support/imgui_context.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace CometEditor::Tests {
    class ProjectInputUiTest: public ::testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext imgui{{1200, 800}};
        InputSettingsPanel panel;
        Comet::InputActions original;
        bool shortcut_triggered = false;
        std::string rendered_text;

        void SetUp() override {
            auto actions = Comet::InputActions::create(
                {{"jump", Comet::InputActions::Type::Button, {{Comet::Input::Key::Space}},
                     "gameplay"},
                    {"interact", Comet::InputActions::Type::Button, {{Comet::Input::Key::S}}}},
                {{"gameplay"}, {"menu", false, 100, true}});
            ASSERT_TRUE(actions);
            original = std::move(actions).value();
            panel.request(original);
            frame();
            frame();
        }

        void frame() {
            ImGui::NewFrame();
            ImGui::LogToBuffer(0);
            shortcut_triggered = ImGui::Shortcut(ImGuiKey_S, ImGuiInputFlags_RouteGlobal);
            ImGui::SetNextWindowPos({10, 20});
            panel.render();
            shortcut_triggered |= ImGui::Shortcut(ImGuiKey_S, ImGuiInputFlags_RouteGlobal);
            rendered_text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
            ImGui::Render();
        }

        ImGuiWindow* window() { return ImGui::FindWindowByName("Project Settings - Input"); }

        ImGuiWindow* child(const char* name) {
            for(auto* child : ImGui::GetCurrentContext()->Windows)
                if(child->ParentWindow == window() && child->ChildId == window()->GetID(name))
                    return child;
            return nullptr;
        }

        ImGuiWindow* details() { return child("ActionDetails"); }

        ImGuiID binding_id(const char* label) {
            const int first = 0;
            auto id = ImHashData(&first, sizeof(first), details()->ID);
            id = ImHashStr("Bindings", 0, id);
            id = ImHashData(&first, sizeof(first), id);
            return ImHashStr(label, 0, id);
        }

        void press(const ImGuiKey key) {
            ImGui::GetIO().AddKeyEvent(key, true);
            frame();
            ImGui::GetIO().AddKeyEvent(key, false);
        }

        void button(const char* label) {
            ImGui::ActivateItemByID(window()->GetID(label));
            frame();
        }

        void reopen(const Comet::InputActions& actions) {
            button("Close");
            panel.request(actions);
            frame();
            frame();
        }

        ImGuiID context_id(const int index, const char* label) {
            const auto group = ImHashData(&index, sizeof(index), child("ContextList")->ID);
            return ImHashStr(label, 0, group);
        }

        void select_action_type(const char* type) {
            ASSERT_NE(details(), nullptr);
            const int first = 0;
            const auto action_id = ImHashData(&first, sizeof(first), details()->ID);
            ImGui::FocusWindow(details());
            ImGui::ActivateItemByID(ImHashStr("##Type", 0, action_id));
            frame();
            auto* combo = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(combo, nullptr);
            ImGui::ActivateItemByID(combo->GetID(type));
            frame();
        }

        void select_binding_source(const char* source) {
            ASSERT_NE(details(), nullptr);
            ImGui::FocusWindow(details());
            ImGui::ActivateItemByID(binding_id("##Source"));
            frame();
            auto* combo = ImGui::FindWindowByName("##Combo_00");
            ASSERT_NE(combo, nullptr);
            ASSERT_TRUE(combo->Active);
            ImGui::ActivateItemByID(combo->GetID(source));
            frame();
        }

        void record() {
            ASSERT_NE(details(), nullptr);
            ImGui::FocusWindow(details());
            ImGui::ActivateItemByID(binding_id("Record Key"));
            frame();
            ASSERT_EQ(ImGui::GetActiveID(), window()->GetID("KeyCapture"));
        }

        void edit_text(ImGuiWindow* owner, const ImGuiID id, const char* text) {
            ImGui::FocusWindow(owner);
            ImGui::ActivateItemByID(id);
            frame();
            ASSERT_EQ(ImGui::GetActiveID(), id);
            auto& io = ImGui::GetIO();
            const auto modifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(modifier, true);
            press(ImGuiKey_A);
            io.AddKeyEvent(modifier, false);
            frame();
            if(*text == '\0')
                press(ImGuiKey_Backspace);
            else
                io.AddInputCharactersUTF8(text);
            frame();
            ASSERT_EQ(ImGui::GetActiveID(), id);
            ASSERT_STREQ(ImGui::GetInputTextState(id)->TextA.Data, text);
            press(ImGuiKey_Enter);
            frame();
        }

        void edit_control(const char* text) { edit_text(details(), binding_id("##Control"), text); }
    };

    TEST_F(ProjectInputUiTest, ContextDraftsPreserveMembershipAndDefaultStateAcrossRenameAndSave) {
        button("Save");
        const auto unchanged = panel.take_request();
        ASSERT_TRUE(unchanged);
        EXPECT_EQ(*unchanged, original);
        button("Input Contexts");
        auto* contexts = child("ContextList");
        ASSERT_NE(contexts, nullptr);
        const int first = 0;
        const auto group_id = ImHashData(&first, sizeof(first), contexts->ID);
        edit_text(contexts, ImHashStr("##ContextName", 0, group_id), "");
        EXPECT_NE(rendered_text.find("Invalid draft; binding relationships are unavailable."),
            std::string::npos);
        EXPECT_EQ(rendered_text.find("No overlapping bindings."), std::string::npos);
        button("Save");
        EXPECT_FALSE(panel.take_request());
        edit_text(contexts, ImHashStr("##ContextName", 0, group_id), "player_controls");
        ImGui::ActivateItemByID(ImHashStr("Initially Enabled", 0, group_id));
        frame();
        edit_text(contexts, ImHashStr("Priority", 0, group_id), "-2");
        ImGui::ActivateItemByID(ImHashStr("Consume Input", 0, group_id));
        frame();
        button("Save");
        auto renamed = panel.take_request();
        ASSERT_TRUE(renamed);
        ASSERT_EQ(renamed->contexts().size(), 2u);
        EXPECT_EQ(renamed->contexts()[0].name, "player_controls");
        EXPECT_FALSE(renamed->contexts()[0].enabled);
        EXPECT_EQ(renamed->contexts()[0].priority, -2);
        EXPECT_TRUE(renamed->contexts()[0].consume);
        EXPECT_FALSE(renamed->contexts()[1].enabled);
        EXPECT_EQ(renamed->contexts()[1].priority, 100);
        EXPECT_TRUE(renamed->contexts()[1].consume);
        EXPECT_EQ(renamed->actions()[0].context, "player_controls");
        EXPECT_TRUE(renamed->actions()[1].context.empty());
        ImGui::ActivateItemByID(ImHashStr("Remove Context", 0, group_id));
        frame();
        button("Save");
        EXPECT_EQ(panel.take_request(), renamed);
        ImGui::ActivateItemByID(contexts->GetID("Add Context"));
        frame();
        button("Save");
        auto added = panel.take_request();
        ASSERT_TRUE(added);
        ASSERT_EQ(added->contexts().size(), 3u);
        EXPECT_EQ(added->contexts().back().name, "context_1");
        EXPECT_TRUE(added->contexts().back().enabled);
        EXPECT_EQ(added->contexts().back().priority, 0);
        EXPECT_FALSE(added->contexts().back().consume);
        EXPECT_EQ(added->actions(), renamed->actions());
    }

    TEST_F(ProjectInputUiTest, ContextSelectionAndRemovalKeepRemainingActionReferences) {
        const int first = 0;
        const auto action_id = ImHashData(&first, sizeof(first), details()->ID);
        ImGui::FocusWindow(details());
        ImGui::ActivateItemByID(ImHashStr("Context", 0, action_id));
        frame();
        auto* combo = ImGui::FindWindowByName("##Combo_00");
        ASSERT_NE(combo, nullptr);
        ImGui::ActivateItemByID(combo->GetID("menu"));
        frame();
        button("Input Contexts");
        auto* contexts = child("ContextList");
        ASSERT_NE(contexts, nullptr);
        const auto group_id = ImHashData(&first, sizeof(first), contexts->ID);
        ImGui::ActivateItemByID(ImHashStr("Remove Context", 0, group_id));
        frame();
        button("Save");
        auto saved = panel.take_request();
        ASSERT_TRUE(saved);
        ASSERT_EQ(saved->contexts().size(), 1u);
        EXPECT_EQ(saved->contexts()[0].name, "menu");
        EXPECT_FALSE(saved->contexts()[0].enabled);
        EXPECT_EQ(saved->actions()[0].context, "menu");
        EXPECT_TRUE(saved->actions()[1].context.empty());
    }

    TEST_F(ProjectInputUiTest, RecordingConsumesShortcutsAndAllowsSharedBindings) {
        record();
        press(ImGuiKey_S);
        EXPECT_FALSE(shortcut_triggered);
        frame();
        press(ImGuiKey_S);
        EXPECT_TRUE(shortcut_triggered);
        frame();
        button("Save");
        auto saved = panel.take_request();
        ASSERT_TRUE(saved);
        EXPECT_EQ(std::get<Comet::Input::Key>(saved->actions()[0].bindings[0].control),
            Comet::Input::Key::S);
        EXPECT_EQ(saved->actions()[0].bindings[0], saved->actions()[1].bindings[0]);
        EXPECT_FALSE(panel.take_request());

        record();
        press(ImGuiKey_Escape);
        frame();
        button("Save");
        auto cancelled = panel.take_request();
        ASSERT_TRUE(cancelled);
        EXPECT_EQ(*cancelled, *saved);
    }

    TEST_F(ProjectInputUiTest, LosingFocusOrClosingCancelsRecording) {
        record();
        ImGui::FocusWindow(nullptr);
        frame();
        press(ImGuiKey_K);
        frame();
        button("Save");
        auto saved = panel.take_request();
        ASSERT_TRUE(saved);
        EXPECT_EQ(*saved, original);

        record();
        button("Close");
        EXPECT_FALSE(panel.is_open());
        EXPECT_NE(ImGui::GetActiveID(), window()->GetID("KeyCapture"));
    }

    TEST_F(ProjectInputUiTest, InvalidDraftAndFailedSaveRemainEditableWhileCloseDiscardsDraft) {
        edit_control("S");
        EXPECT_NE(rendered_text.find("key/S: interact ["), std::string::npos);
        EXPECT_NE(
            rendered_text.find("Shared (common action bypasses consumption)"), std::string::npos);
        edit_control("unknown_control_longer_than_the_initial_string_capacity");
        EXPECT_NE(rendered_text.find("Invalid draft; binding relationships are unavailable."),
            std::string::npos);
        EXPECT_EQ(rendered_text.find("key/S: interact ["), std::string::npos);
        EXPECT_EQ(
            rendered_text.find("Shared (common action bypasses consumption)"), std::string::npos);
        button("Save");
        EXPECT_FALSE(panel.take_request());
        EXPECT_TRUE(panel.is_open());

        edit_control("RightControl");
        EXPECT_EQ(rendered_text.find("Invalid draft; binding relationships are unavailable."),
            std::string::npos);
        EXPECT_NE(rendered_text.find("No overlapping bindings."), std::string::npos);
        button("Save");
        auto saved = panel.take_request();
        ASSERT_TRUE(saved);
        EXPECT_EQ(std::get<Comet::Input::Key>(saved->actions()[0].bindings[0].control),
            Comet::Input::Key::RightControl);
        panel.complete(Comet::Result<void>::failure("Project file changed since it was loaded"));
        frame();
        EXPECT_TRUE(panel.is_open());
        button("Save");
        auto retry = panel.take_request();
        ASSERT_TRUE(retry);
        EXPECT_EQ(*retry, *saved);

        button("Close");
        EXPECT_FALSE(panel.is_open());
        EXPECT_FALSE(panel.take_request());
        panel.request(original);
        frame();
        button("Save");
        auto reopened = panel.take_request();
        ASSERT_TRUE(reopened);
        EXPECT_EQ(*reopened, original);
    }

    TEST_F(ProjectInputUiTest, ReselectingBindingSourcePreservesTheWholeConfiguration) {
        using Actions = Comet::InputActions;
        struct Case {
            const char* source;
            Actions::Type type;
            Actions::Binding binding;
        };
        const Case cases[]{{"motion", Actions::Type::Delta, {Actions::Motion::ScrollY, -2}},
            {"key", Actions::Type::Axis, {Comet::Input::Key::Left, -1}},
            {"gamepad_axis", Actions::Type::Axis,
                {Comet::Input::GamepadAxis::RightY, -0.5f, 0.2f}}};
        for(const auto& test : cases) {
            SCOPED_TRACE(test.source);
            const auto configured = Actions::create(
                {{"controlled", test.type, {test.binding}, "gameplay"}}, original.contexts());
            ASSERT_TRUE(configured);
            reopen(configured.value());
            ASSERT_NO_FATAL_FAILURE(select_binding_source(test.source));
            EXPECT_FALSE(panel.take_request());
            button("Save");
            const auto saved = panel.take_request();
            EXPECT_TRUE(saved);
            if(saved)
                EXPECT_EQ(*saved, configured.value());
        }

        ASSERT_NO_FATAL_FAILURE(select_binding_source("key"));
        button("Save");
        EXPECT_FALSE(panel.take_request());
        edit_control("Right");
        button("Save");
        const auto changed = panel.take_request();
        ASSERT_TRUE(changed);
        EXPECT_EQ(changed->actions()[0].bindings[0], Actions::Binding{Comet::Input::Key::Right});
        EXPECT_EQ(changed->actions()[0].context, "gameplay");
        EXPECT_EQ(changed->contexts(), original.contexts());
    }

    TEST_F(ProjectInputUiTest, AxisToButtonNormalizesHiddenMultipliersAndPreservesBindings) {
        using Actions = Comet::InputActions;
        const auto configured = Actions::create(
            {{"move", Actions::Type::Axis, {{Comet::Input::Key::A, -1}, {Comet::Input::Key::D, 2}},
                 "gameplay"},
                {"interact", Actions::Type::Button, {{Comet::Input::Key::S}}}},
            original.contexts());
        ASSERT_TRUE(configured);
        const auto snapshot = configured.value();
        reopen(configured.value());

        select_action_type("Button");
        EXPECT_FALSE(panel.take_request());
        button("Save");
        const auto saved_button = panel.take_request();
        ASSERT_TRUE(saved_button);
        auto expected = snapshot.actions();
        expected[0].type = Actions::Type::Button;
        for(auto& binding : expected[0].bindings)
            binding.scale = 1;
        EXPECT_EQ(saved_button->actions(), expected);
        EXPECT_EQ(saved_button->contexts(), snapshot.contexts());
        EXPECT_EQ(configured.value(), snapshot);

        select_action_type("Axis");
        button("Save");
        const auto saved_axis = panel.take_request();
        ASSERT_TRUE(saved_axis);
        expected[0].type = Actions::Type::Axis;
        EXPECT_EQ(saved_axis->actions(), expected);
        EXPECT_EQ(saved_axis->contexts(), snapshot.contexts());

        select_action_type("Delta");
        button("Save");
        EXPECT_FALSE(panel.take_request());
        EXPECT_TRUE(panel.is_open());
        EXPECT_NE(rendered_text.find("Invalid draft; binding relationships are unavailable."),
            std::string::npos);
        select_action_type("Axis");
        button("Save");
        EXPECT_EQ(panel.take_request(), saved_axis);
        EXPECT_EQ(configured.value(), snapshot);
    }

    TEST_F(ProjectInputUiTest, BindingRelationshipsFollowContextDraftWithoutSaving) {
        const auto actions = Comet::InputActions::create(
            {{"jump", Comet::InputActions::Type::Button, {{Comet::Input::Key::Space}}, "gameplay"},
                {"confirm", Comet::InputActions::Type::Button, {{Comet::Input::Key::Space}},
                    "menu"}},
            {{"gameplay"}, {"menu", false, 100, true}});
        ASSERT_TRUE(actions);
        reopen(actions.value());
        EXPECT_NE(rendered_text.find("Binding Relationships"), std::string::npos);
        EXPECT_NE(rendered_text.find(
                      "Pairwise rules when both contexts are enabled; not current Play state."),
            std::string::npos);
        EXPECT_NE(
            rendered_text.find("Other consuming contexts can still block non-common actions."),
            std::string::npos);
        EXPECT_NE(rendered_text.find("key/Space: confirm [menu]"), std::string::npos);
        EXPECT_NE(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        EXPECT_NE(rendered_text.find("Initially disabled: menu"), std::string::npos);
        EXPECT_FALSE(panel.take_request());

        button("Input Contexts");
        auto* contexts = child("ContextList");
        ASSERT_NE(contexts, nullptr);
        ImGui::ActivateItemByID(context_id(1, "Initially Enabled"));
        frame();
        EXPECT_EQ(rendered_text.find("Initially disabled: menu"), std::string::npos);
        EXPECT_NE(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        ImGui::ActivateItemByID(context_id(1, "Consume Input"));
        frame();
        EXPECT_NE(rendered_text.find("Shared"), std::string::npos);
        EXPECT_EQ(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        EXPECT_FALSE(panel.take_request());

        ImGui::ActivateItemByID(context_id(1, "Consume Input"));
        frame();
        edit_text(contexts, context_id(1, "Priority"), "0");
        EXPECT_NE(rendered_text.find("Shared"), std::string::npos);
        EXPECT_EQ(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        edit_text(contexts, context_id(0, "Priority"), "200");
        ImGui::ActivateItemByID(context_id(0, "Consume Input"));
        frame();
        EXPECT_NE(
            rendered_text.find("Consumes this control from the other action"), std::string::npos);
        EXPECT_EQ(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        EXPECT_FALSE(panel.take_request());
        EXPECT_EQ(actions.value().contexts()[0].priority, 0);
        EXPECT_FALSE(actions.value().contexts()[1].enabled);
    }

    TEST_F(ProjectInputUiTest, BindingRelationshipsNormalizeControlsAndPreserveLegalSharing) {
        const auto actions = Comet::InputActions::create(
            {{"jump", Comet::InputActions::Type::Button,
                 {{Comet::Input::Key::F1}, {Comet::Input::Key::Right}}, "gameplay"},
                {"confirm", Comet::InputActions::Type::Button, {{Comet::Input::Key::F1}}, "menu"},
                {"mouse", Comet::InputActions::Type::Button, {{Comet::Input::MouseButton::Right}}}},
            {{"gameplay", true, 0, true}, {"menu", true, 0, true}});
        ASSERT_TRUE(actions);
        reopen(actions.value());
        edit_control("F01");
        EXPECT_NE(rendered_text.find("key/F1: confirm [menu]"), std::string::npos);
        EXPECT_NE(rendered_text.find("Shared"), std::string::npos);
        EXPECT_EQ(
            rendered_text.find("Consumes this control from the other action"), std::string::npos);
        EXPECT_EQ(
            rendered_text.find("This control is consumed by the other action"), std::string::npos);
        EXPECT_EQ(rendered_text.find(": mouse ["), std::string::npos);
        button("Save");
        const auto shared = panel.take_request();
        ASSERT_TRUE(shared);
        EXPECT_EQ(*shared, actions.value());

        const int first = 0;
        const auto action_id = ImHashData(&first, sizeof(first), details()->ID);
        ImGui::FocusWindow(details());
        ImGui::ActivateItemByID(ImHashStr("Context", 0, action_id));
        frame();
        auto* combo = ImGui::FindWindowByName("##Combo_00");
        ASSERT_NE(combo, nullptr);
        ImGui::ActivateItemByID(combo->GetID("Common (Always Enabled)"));
        frame();
        EXPECT_NE(
            rendered_text.find("Shared (common action bypasses consumption)"), std::string::npos);
        EXPECT_FALSE(panel.take_request());
        button("Save");
        const auto common = panel.take_request();
        ASSERT_TRUE(common);
        EXPECT_TRUE(common->actions()[0].context.empty());
        EXPECT_EQ(common->actions()[0].bindings, shared->actions()[0].bindings);
    }

    TEST(ProjectSettingsUiTest, InputSettingsOpensAsNonModalPanel) {
        Comet::Tests::ImGuiTestContext imgui;
        InputSettingsPanel panel;
        EXPECT_FALSE(panel.is_open());

        panel.request(Comet::InputActions{});
        ImGui::NewFrame();
        panel.render();
        ImGui::Render();

        EXPECT_TRUE(panel.is_open());
        const auto* window = ImGui::FindWindowByName("Project Settings - Input");
        ASSERT_NE(window, nullptr);
        EXPECT_FALSE(window->Flags & ImGuiWindowFlags_Popup);
        EXPECT_FALSE(window->Flags & ImGuiWindowFlags_Modal);
        EXPECT_FALSE(panel.take_request());
    }

    TEST(ProjectSettingsUiTest, OnlySuccessfulChangedInputProducesRuntimeUpdate) {
        Comet::Tests::ImGuiTestContext imgui{{1200, 800}};
        Comet::Tests::TemporaryDirectory directory;
        const auto root = directory.path() / "Project";
        ASSERT_TRUE(create_project(root));
        auto loaded = Comet::Project::load(root);
        ASSERT_TRUE(loaded);
        auto project = std::move(loaded).value();
        ProjectSettings settings(project);
        settings.request_input();
        const auto frame = [&] {
            ImGui::NewFrame();
            settings.render(true);
            ImGui::Render();
        };
        frame();
        frame();
        auto* window = ImGui::FindWindowByName("Project Settings - Input");
        ASSERT_NE(window, nullptr);
        ImGui::ActivateItemByID(window->GetID("Save"));
        frame();
        EXPECT_FALSE(settings.update().input_changed);

        ImGuiWindow* actions = nullptr;
        for(auto* child : GImGui->Windows)
            if(child->ParentWindow == window && child->ChildId == window->GetID("ActionList"))
                actions = child;
        ASSERT_NE(actions, nullptr);
        ImGui::ActivateItemByID(actions->GetID("Add Action"));
        frame();
        ImGui::ActivateItemByID(window->GetID("Save"));
        frame();
        EXPECT_TRUE(settings.update().input_changed);
        EXPECT_FALSE(settings.update().input_changed);
        EXPECT_EQ(project.input_actions().actions().size(), 1);
        EXPECT_EQ(Comet::Project::load(root).value().input_actions(), project.input_actions());

        const auto external = Comet::read_text_file(root / "project.json").value() + "\n";
        ASSERT_TRUE(Comet::write_text_file_atomic(root / "project.json", external));
        ImGui::ActivateItemByID(actions->GetID("Add Action"));
        frame();
        ImGui::ActivateItemByID(window->GetID("Save"));
        frame();
        EXPECT_FALSE(settings.update().input_changed);
        EXPECT_EQ(project.input_actions().actions().size(), 1);
        EXPECT_EQ(Comet::read_text_file(root / "project.json").value(), external);
    }

    TEST(ProjectSettingsTest, StartupSceneRequiresKnownOrSavedSceneAndReadableContents) {
        Comet::Tests::TemporaryDirectory directory;
        const auto root = directory.path() / "Project";
        ASSERT_TRUE(create_project(root));
        auto project = Comet::Project::load(root);
        ASSERT_TRUE(project) << project.error();
        Comet::AssetDatabase assets(project.value().paths());
        ASSERT_TRUE(assets.scan().succeeded());
        ProjectSettings settings(project.value());
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        const auto initial = project.value().startup_scene();
        const auto original = Comet::read_text_file(root / "assets" / initial);
        ASSERT_TRUE(original);
        const std::filesystem::path next = "scenes/new.scene";
        ASSERT_TRUE(Comet::write_text_file_atomic(root / "assets" / next, original.value()));

        EXPECT_FALSE(settings.set_startup_scene(next, {}, assets, serializer));
        EXPECT_EQ(project.value().startup_scene(), initial);
        ASSERT_TRUE(settings.set_startup_scene(next, next, assets, serializer));
        EXPECT_EQ(Comet::Project::load(root).value().startup_scene(), next);
        ASSERT_TRUE(settings.set_startup_scene(initial, {}, assets, serializer));

        ASSERT_TRUE(Comet::write_text_file_atomic(root / "assets" / next, "{"));
        EXPECT_FALSE(settings.set_startup_scene(next, next, assets, serializer));
        EXPECT_FALSE(settings.set_startup_scene({}, {}, assets, serializer));
        EXPECT_FALSE(
            settings.set_startup_scene("../outside.scene", "../outside.scene", assets, serializer));
        EXPECT_EQ(project.value().startup_scene(), initial);
        EXPECT_EQ(Comet::Project::load(root).value().startup_scene(), initial);
    }

    TEST(ProjectSettingsTest, StartupSceneWriteConflictPreservesLoadedSettingsAndExternalFile) {
        Comet::Tests::TemporaryDirectory directory;
        const auto root = directory.path() / "Project";
        ASSERT_TRUE(create_project(root));
        auto project = Comet::Project::load(root);
        ASSERT_TRUE(project) << project.error();
        Comet::AssetDatabase assets(project.value().paths());
        ProjectSettings settings(project.value());
        const auto components = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(components);
        const auto initial = project.value().startup_scene();
        const auto original = Comet::read_text_file(root / "assets" / initial);
        ASSERT_TRUE(original);
        const std::filesystem::path next = "scenes/new.scene";
        ASSERT_TRUE(Comet::write_text_file_atomic(root / "assets" / next, original.value()));
        const auto manifest = root / "project.json";
        const auto external = Comet::read_text_file(manifest).value() + "\n";
        ASSERT_TRUE(Comet::write_text_file_atomic(manifest, external));

        const auto saved = settings.set_startup_scene(next, next, assets, serializer);
        ASSERT_FALSE(saved);
        EXPECT_NE(saved.error().find("changed since it was loaded"), std::string::npos);
        EXPECT_EQ(project.value().startup_scene(), initial);
        EXPECT_EQ(Comet::read_text_file(manifest).value(), external);
    }
}
#endif
