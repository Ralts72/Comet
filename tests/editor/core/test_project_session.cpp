#include "project/project_session.h"

#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace CometEditor::Tests {
    TEST(ProjectSessionTest, RestoresLastSceneAndDistinguishesNewUnsavedScene) {
        Comet::Tests::TemporaryDirectory directory;
        ASSERT_TRUE(std::filesystem::create_directory(directory.path() / "assets"));
        const Comet::ProjectPaths paths(directory.path());
        ProjectSession session(paths);
        ASSERT_TRUE(session.load());
        EXPECT_FALSE(session.last_scene());

        ASSERT_TRUE(session.record_scene("scenes/edited.scene"));
        ASSERT_TRUE(session.last_scene());
        EXPECT_EQ(*session.last_scene(), "scenes/edited.scene");
        ProjectSession reopened(paths);
        ASSERT_TRUE(reopened.load());
        EXPECT_EQ(reopened.last_scene(), session.last_scene());

        ASSERT_TRUE(reopened.record_scene({}));
        ASSERT_TRUE(reopened.last_scene());
        EXPECT_TRUE(reopened.last_scene()->empty());
        ProjectSession new_scene(paths);
        ASSERT_TRUE(new_scene.load());
        ASSERT_TRUE(new_scene.last_scene());
        EXPECT_TRUE(new_scene.last_scene()->empty());
    }

    TEST(ProjectSessionTest, InvalidPathsAndStateDoNotOverwriteLastScene) {
        Comet::Tests::TemporaryDirectory directory;
        ASSERT_TRUE(std::filesystem::create_directory(directory.path() / "assets"));
        const Comet::ProjectPaths paths(directory.path());
        ProjectSession session(paths);
        ASSERT_TRUE(session.record_scene("scenes/current.scene"));
        const auto file = paths.editor_state() / "session.json";
        const auto original = Comet::read_text_file(file).value();
        for(const auto& path : {"../outside.scene", "/outside.scene", "texture.png"}) {
            EXPECT_FALSE(session.record_scene(path));
            EXPECT_EQ(Comet::read_text_file(file).value(), original);
        }
        ASSERT_TRUE(Comet::write_text_file_atomic(file, "{"));
        ProjectSession malformed(paths);
        EXPECT_FALSE(malformed.load());
        EXPECT_FALSE(malformed.last_scene());
        EXPECT_EQ(Comet::read_text_file(file).value(), "{");
    }
}
