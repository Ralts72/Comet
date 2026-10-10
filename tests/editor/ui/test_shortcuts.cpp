#ifdef COMET_TEST_EDITOR_UI
#include "ui/shortcuts.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    using Action = EditorShortcuts::Action;

    TEST(EditorShortcutsTest, EditorDefaultsAreValid) {
        const EditorShortcuts defaults;
        EXPECT_EQ(defaults.label(Action::NewScene, false), "Ctrl+N");
        EXPECT_EQ(defaults.label(Action::Redo, true), "Cmd+Shift+Z / Cmd+Y");
        EXPECT_EQ(defaults.label(Action::FocusSelection, false), "F");
    }

    TEST(EditorShortcutsTest, OverridesOnlySpecifiedActionsAndAllowsDisabling) {
        const auto parsed = EditorShortcuts::parse(R"({
            "editor": {
                "shortcuts": {
                    "scene.save": ["Primary+Shift+S", "F5"],
                    "edit.redo": [],
                    "viewport.focus_selection": ["Alt+G"]
                }
            }
        })");
        ASSERT_TRUE(parsed) << parsed.error();
        const auto& shortcuts = parsed.value();
        EXPECT_EQ(shortcuts.label(Action::SaveScene, false), "Ctrl+Shift+S / F5");
        EXPECT_EQ(shortcuts.label(Action::SaveScene, true), "Cmd+Shift+S / F5");
        EXPECT_EQ(shortcuts.label(Action::FocusSelection, true), "Option+G");
        EXPECT_EQ(shortcuts.label(Action::Undo, false), "Ctrl+Z");
        EXPECT_EQ(shortcuts.label(Action::CopyEntity, false), "Ctrl+C");
        EXPECT_EQ(shortcuts.label(Action::PasteEntity, true), "Cmd+V");
        EXPECT_EQ(shortcuts.label(Action::DeleteSelection, true), "Cmd+Backspace");
        EXPECT_TRUE(shortcuts.label(Action::Redo, false).empty());
        const auto defaults = EditorShortcuts::parse("{}");
        ASSERT_TRUE(defaults);
        EXPECT_EQ(defaults.value().label(Action::Undo, true), "Cmd+Z");
    }

    TEST(EditorShortcutsTest, RejectsMalformedUnknownAndConflictingBindings) {
        for(const auto* contents : {"", "null", "[]", R"({"editor": [})", R"({"editor": []})",
                R"({"editor": {"shortcuts": null}})",
                R"({"editor": {"shortcuts": {"scene.save": [null]}}})",
                R"({"editor": {"shortcuts": {"scene.save": [5]}}})",
                R"({"editor": {"shortcuts": {"scene.save": [["F5"]]}}})",
                R"({"editor": {"shortcuts": {"scene.save": [{"key": "F5"}]}}})",
                R"({"editor": {"shortcuts": {"typo": ["F5"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": "Primary+S"}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["Primary+"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["Ctrl+S"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["Escape"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["Primary+Primary+S"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["Primary+Z"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["Primary+C"]}}})",
                R"({"editor": {"shortcuts": {"edit.copy_entity": ["Primary+V"]}}})",
                R"({"editor": {"shortcuts": {"edit.delete_selection": ["Primary+C"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["F"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["Primary+Shift+S", "Shift+Primary+S"]}}})",
                R"({"editor": {"shortcuts": {"scene.save": ["F5"], "scene.save": ["F6"]}}})",
                R"({"editor": {"shortcuts": {}, "shortcuts": {}}})",
                R"({"editor": {}, "editor": {}})"}) {
            SCOPED_TRACE(contents);
            const auto result = EditorShortcuts::parse(contents);
            ASSERT_FALSE(result);
            EXPECT_FALSE(result.error().empty());
        }
    }

    TEST(EditorShortcutsTest, ValidatesAfterMergingAndDoesNotPartiallyApplyFailure) {
        auto parsed = EditorShortcuts::parse(
            R"({"editor": {"shortcuts": {"scene.save": ["Primary+O"], "scene.open": ["Primary+S"]}}})");
        ASSERT_TRUE(parsed);
        auto shortcuts = std::move(parsed).value();
        EXPECT_EQ(shortcuts.label(Action::SaveScene, false), "Ctrl+O");
        auto replacement = EditorShortcuts::parse(
            R"({"editor": {"shortcuts": {"scene.save": ["F5"], "scene.open": ["Escape"]}}})");
        ASSERT_FALSE(replacement);
        if(replacement)
            shortcuts = std::move(replacement).value();
        EXPECT_EQ(shortcuts.label(Action::SaveScene, false), "Ctrl+O");
    }

    TEST(EditorShortcutsTest, LoadReportsMissingFileAndConfigurationLocation) {
        Comet::Tests::TemporaryDirectory directory;
        const auto path = directory.path() / "shortcuts.json";
        auto missing = EditorShortcuts::load(path);
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().find(path.string()), std::string::npos);
        ASSERT_TRUE(Comet::write_text_file_atomic(
            path, R"({"editor": {"shortcuts": {"scene.save": ["Escape"]}}})"));
        auto invalid = EditorShortcuts::load(path);
        ASSERT_FALSE(invalid);
        EXPECT_NE(invalid.error().find(path.string()), std::string::npos);
        EXPECT_NE(invalid.error().find("editor.shortcuts.scene.save"), std::string::npos);
        EXPECT_NE(invalid.error().find("Escape"), std::string::npos);
    }

    TEST(EditorShortcutsTest, UserOverridesPersistAndLeaveBuiltinDefaults) {
        Comet::Tests::TemporaryDirectory directory;
        const auto path = directory.path() / "shortcuts.json";
        auto texts = EditorShortcuts{}.binding_texts();
        texts[static_cast<std::size_t>(Action::SaveScene)] = {"Alt+S", "Primary+Shift+S"};
        texts[static_cast<std::size_t>(Action::Redo)].clear();
        auto edited = EditorShortcuts::from_texts(std::move(texts));
        ASSERT_TRUE(edited) << edited.error();
        ASSERT_TRUE(edited.value().save_overrides(path));

        const auto contents = Comet::read_text_file(path);
        ASSERT_TRUE(contents) << contents.error();
        EXPECT_EQ(contents.value().find("scene.new"), std::string::npos);
        EXPECT_NE(contents.value().find(R"("edit.redo": [])"), std::string::npos);
        auto loaded = EditorShortcuts::load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().label(Action::NewScene, false), "Ctrl+N");
        EXPECT_EQ(loaded.value().label(Action::SaveScene, false), "Alt+S / Ctrl+Shift+S");
        EXPECT_TRUE(loaded.value().label(Action::Redo, false).empty());
    }

    TEST(EditorShortcutsTest, RejectsConflictingEditedBindings) {
        auto texts = EditorShortcuts{}.binding_texts();
        texts[static_cast<std::size_t>(Action::SaveScene)] = {"Primary+O"};
        auto edited = EditorShortcuts::from_texts(std::move(texts));
        ASSERT_FALSE(edited);
        EXPECT_NE(edited.error().find("scene.open"), std::string::npos);
        EXPECT_NE(edited.error().find("scene.save"), std::string::npos);
    }

    TEST(EditorShortcutsTest, RestoringDefaultsWritesAValidEmptyOverride) {
        Comet::Tests::TemporaryDirectory directory;
        const auto path = directory.path() / "shortcuts.json";
        const EditorShortcuts defaults;
        ASSERT_TRUE(defaults.save_overrides(path));
        const auto loaded = EditorShortcuts::load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().binding_texts(), defaults.binding_texts());
    }
}
#endif
