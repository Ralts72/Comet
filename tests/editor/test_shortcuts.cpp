#ifdef COMET_TEST_EDITOR_UI
#include "ui/shortcuts.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    using Action = EditorShortcuts::Action;

    TEST(EditorShortcutsTest, ProjectProfileIsValid) {
        EXPECT_NO_THROW(static_cast<void>(
            EditorShortcuts::load(std::filesystem::path(PROJECT_ROOT_DIR)
                                  / "config/profiles/editor-dev.yaml")));
    }

    TEST(EditorShortcutsTest, OverridesOnlySpecifiedActionsAndAllowsDisabling) {
        const auto shortcuts = EditorShortcuts::parse(R"(
diagnostics: {log_level: info}
editor:
  shortcuts:
    scene.save: ["Primary+Shift+S", "F5"]
    edit.redo: []
    viewport.focus_selection: ["Alt+G"]
)");
        EXPECT_EQ(shortcuts.label(Action::SaveScene, false), "Ctrl+Shift+S / F5");
        EXPECT_EQ(shortcuts.label(Action::SaveScene, true), "Cmd+Shift+S / F5");
        EXPECT_EQ(shortcuts.label(Action::FocusSelection, true), "Option+G");
        EXPECT_EQ(shortcuts.label(Action::Undo, false), "Ctrl+Z");
        EXPECT_TRUE(shortcuts.label(Action::Redo, false).empty());
        EXPECT_EQ(
            EditorShortcuts::parse("diagnostics: {}").label(Action::Undo, true), "Cmd+Z");
    }

    TEST(EditorShortcutsTest, RejectsMalformedUnknownAndConflictingBindings) {
        for(const auto* yaml : {"editor: []", "editor: {shortcuts: null}",
                "editor: {shortcuts: {typo: [F5]}}",
                "editor: {shortcuts: {scene.save: Primary+S}}",
                "editor: {shortcuts: {scene.save: [Primary+]}}",
                "editor: {shortcuts: {scene.save: [Ctrl+S]}}",
                "editor: {shortcuts: {scene.save: [Escape]}}",
                "editor: {shortcuts: {scene.save: [Primary+Primary+S]}}",
                "editor: {shortcuts: {scene.save: [Primary+Z]}}",
                "editor: {shortcuts: {scene.save: [F]}}",
                "editor: {shortcuts: {scene.save: [Primary+Shift+S, Shift+Primary+S]}}",
                "editor: {shortcuts: {scene.save: [F5], scene.save: [F6]}}"}) {
            SCOPED_TRACE(yaml);
            EXPECT_THROW(static_cast<void>(EditorShortcuts::parse(yaml)), std::exception);
        }
    }

    TEST(EditorShortcutsTest, ValidatesAfterMergingAndDoesNotPartiallyApplyFailure) {
        auto shortcuts = EditorShortcuts::parse(
            "editor: {shortcuts: {scene.save: [Primary+O], scene.open: [Primary+S]}}");
        EXPECT_EQ(shortcuts.label(Action::SaveScene, false), "Ctrl+O");
        EXPECT_THROW(shortcuts = EditorShortcuts::parse(
                         "editor: {shortcuts: {scene.save: [F5], scene.open: [Escape]}}"),
            std::exception);
        EXPECT_EQ(shortcuts.label(Action::SaveScene, false), "Ctrl+O");
    }
}
#endif
